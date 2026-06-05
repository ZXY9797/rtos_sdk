from genericpath import exists
import os
import sys
import getopt
import struct
import re
import shutil

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from elftools.elf.sections import Section
from elftools.elf.constants import SH_FLAGS
from elftools.elf import enums

import compress_algo

# 变量
print_level = 2
endian_format_prefix = ""

# 常量
# compress_prefix = "compress_tool: "
compress_prefix = ""
symbol_flash_size_name = "__flash_size__"
symbol_sct_load_config_name = "__sct_load_config__"
noinit_section_tab=".noinit"
LOAD_CONFIG_MAGIC = 0x20220315

# 结构体定义
'''
typedef struct
{
    uint32_t lma;  ///< 加载地址
    uint32_t vma;  ///< 执行地址
    uint32_t len;  ///< 压缩后的长度
    uint8_t  type; ///< 加载类型
    uint8_t  reserve[3];
} sct_load_node_t;
'''
sct_load_node_t_format = "IIIBxxx"
sct_load_node_t_format_size = 16
'''
typedef struct
{
    uint32_t         magic;
    sct_load_node_t *node_head;
    uint32_t         node_num;
    uint32_t         sum_check;
} sct_load_config_t;
'''
sct_load_config_t_format = "IIII"
sct_load_config_t_format_size = 16

class compress_failed_exception(Exception):
    def __init__(self, f, user_log, del_file=False):
        self.f = f
        self.user_log = user_log
        self.del_file = del_file

def print_error(log_str):
    print("%s\033[1;31m%s\033[0m"%(compress_prefix, log_str))

def print_warn(log_str):
    if print_level >= 1:
        print("%s\033[1;33m%s\033[0m"%(compress_prefix, log_str))

def print_info(log_str):
    if print_level >= 2:
        print("%s\033[1;32m%s\033[0m"%(compress_prefix, log_str))

def print_debug(log_str):
    if print_level >= 3:
        print("%s\033[1;34m%s\033[0m"%(compress_prefix, log_str))

def check_elf_type(f, head):
    global endian_format_prefix
    # 只支持32bit的elf
    if head['e_ident'].EI_CLASS != 'ELFCLASS32':
        raise compress_failed_exception(f, "Compress Tool just support ELFCLASS32, this file is %s"%(head['e_ident'].EI_CLASS))

    # 检查大小端
    if head['e_ident'].EI_DATA == 'ELFDATA2LSB':
        endian_format_prefix = "<"
        print_debug("elf data is little endian")
    elif head['e_ident'].EI_DATA == 'ELFDATA2MSB':
        endian_format_prefix = ">"
        print_debug("elf data is big endian")
    else:
        raise compress_failed_exception(f, "Compress Tool not support data endian %s"%(head['e_ident'].EI_DATA))

def get_symbol_by_name(elf_file, symbol_name):
    for section in elf_file.iter_sections():
        if isinstance(section, SymbolTableSection):
            symbol = section.get_symbol_by_name(symbol_name)
            if symbol == None:
                return None
            return symbol[0].entry
    return None

def get_key_word(f, elf_file):
    flash_size_sym = get_symbol_by_name(elf_file, symbol_flash_size_name)
    if flash_size_sym == None:
        raise compress_failed_exception(f, "not find the symbol '%s', you must define it in your link script!"%(symbol_flash_size_name))

    print_debug("flash size: %d(%dk) byte"%(flash_size_sym['st_value'], flash_size_sym['st_value'] / 1024))

    load_config_sym = get_symbol_by_name(elf_file, symbol_sct_load_config_name)
    if load_config_sym == None:
        raise compress_failed_exception(f, "not find the symbol '%s', you have to make sure it exists "%(symbol_sct_load_config_name))
    print_debug("%s addr: 0x%x"%(symbol_sct_load_config_name, load_config_sym['st_value']))

    return flash_size_sym, load_config_sym

def get_section_lma(elf_file, section):
    for seg in elf_file.iter_segments():
        if seg['p_type'] != 'PT_LOAD':
            continue
        if (section.header['sh_addr'] >= seg['p_vaddr']) and (section.header['sh_addr'] < seg['p_vaddr'] + seg['p_filesz']):
            return seg['p_paddr']

def section_filter(elf_file):
    alloc_section = []
    fix_section = []
    zero_section = []
    remap_section = []
    copy_section = []

    for section in elf_file.iter_sections():
        # fixed sections. like .text
        if section.header['sh_flags'] & SH_FLAGS.SHF_ALLOC != SH_FLAGS.SHF_ALLOC:
            continue
        alloc_section.append(section)

        # zero section .bss
        if section.header['sh_type'] == "SHT_NOBITS":
            # 处理noinit段，有.noinit标记的段不进行清零操作
            if re.search( noinit_section_tab, elf_file._get_section_name(section.header)) == None:
                zero_section.append(section)
            else:
                print_debug("%s is noinit section"%(elf_file._get_section_name(section.header)))
            continue

        # init_array/fini_array/preinit_array sections should be treated as fix sections
        # because they contain function pointers that should not be compressed or moved
        if section.header['sh_type'] in ('SHT_INIT_ARRAY', 'SHT_FINI_ARRAY', 'SHT_PREINIT_ARRAY'):
            fix_section.append(section)
            continue

        # .data section or some ram func section
        for seg in elf_file.iter_segments():
            if seg['p_type'] != 'PT_LOAD':
                continue
            if (section.header['sh_addr'] >= seg['p_vaddr']) and (section.header['sh_addr'] < seg['p_vaddr'] + seg['p_filesz']):
                if seg['p_vaddr'] == seg['p_paddr']:
                    fix_section.append(section)
                break

    # 获取到FIX SECTION的结束地址后处理remap，以便可以区分copy_section和remap_section
    fix_section_end_addr = fix_section[len(fix_section) - 1].header[ 'sh_addr' ] + fix_section[len(fix_section) - 1].header[ 'sh_size' ]
    # 创建一个集合来快速查找已经在 fix_section 中的 section
    fix_section_set = set(id(sec) for sec in fix_section)
    for section in elf_file.iter_sections():
        if section.header['sh_flags'] & SH_FLAGS.SHF_ALLOC != SH_FLAGS.SHF_ALLOC:
            continue
        # 跳过已经在 fix_section 中的 section（如 SHT_INIT_ARRAY 等）
        if id(section) in fix_section_set:
            continue
        for seg in elf_file.iter_segments():
            if seg['p_type'] != 'PT_LOAD':
                continue
            if (section.header['sh_addr'] >= seg['p_vaddr']) and (section.header['sh_addr'] < seg['p_vaddr'] + seg['p_filesz']):
                # 需要remap的类型
                if seg['p_vaddr'] != seg['p_paddr']:
                    # 对于在fix section结束地址前的remap类型section，强制使用copy方式进行搬移，否则会造成额外的空间浪费
                    if seg['p_paddr'] < fix_section_end_addr:
                        copy_section.append(section)
                    else:
                        remap_section.append(section)
                break

    return alloc_section, fix_section, zero_section, remap_section, copy_section

def overwrite_section(f, elf_file, section):
    sec_index = 0
    for sec in elf_file.iter_sections():
        if sec.header['sh_name'] == section.header['sh_name']:
            break
        sec_index += 1
    sec_offset = sec_index * elf_file.header['e_shentsize'] + elf_file.header['e_shoff']
    if section.header['sh_type'] == 'SHT_PROGBITS':
        sh_type = 1
    elif section.header['sh_type'] == 'SHT_NOBITS':
        sh_type = 8
    elif section.header['sh_type'] in ('SHT_INIT_ARRAY', 'SHT_FINI_ARRAY', 'SHT_PREINIT_ARRAY'):
        # Treat init/fini/preinit arrays as progbits for rewriting
        sh_type = 1
    else:
        raise compress_failed_exception(f, "not support rewrite section type %s for section %s"%(section.header['sh_type'], elf_file._get_section_name(section.header)))
    data = struct.pack("%sIIIIIIIIII" % (compress_prefix),
                       section.header['sh_name'],
                       sh_type,
                       section.header['sh_flags'],
                       section.header['sh_addr'],
                       section.header['sh_offset'],
                       section.header['sh_size'],
                       section.header['sh_link'],
                       section.header['sh_info'],
                       section.header['sh_addralign'],
                       section.header['sh_entsize'])
    f_seek_current = f.tell()
    f.seek(sec_offset)
    f.write(data)
    f.seek(f_seek_current)

def remap_data_compress(f, section, force_copy):
    if section.header['sh_type'] == 'SHT_NOBITS':
        return compress_algo.LOAD_TYPE_ZERO, section.header['sh_size'], None
    f_seek_current = f.tell()
    f.seek(section.header['sh_offset'])
    data = f.read(section.header['sh_size'])
    f.seek(f_seek_current)

    # 此处可以添加任意压缩算法甚至加密算法
    copy_len, copy_data = compress_algo.copy_algo(data)
    rlez_len, rlez_data = compress_algo.rlez_algo(data)

    if copy_len < rlez_len or force_copy:
        return compress_algo.LOAD_TYPE_COPY, copy_len, copy_data
    else:
        return compress_algo.LOAD_TYPE_RLEZ, rlez_len, rlez_data

def get_file_size(f):
    current = f.tell()
    f.seek(0,2)
    f_size = f.tell()
    f.seek(current)
    return f_size

def overwrite_load_config(f, elf_file, sym, data):
    lma = sym['st_value']
    for sec in elf_file.iter_sections():
        if (lma >= sec.header['sh_addr']) and (lma < sec.header['sh_addr'] + sec.header['sh_size']):
            offset = sec.header['sh_offset'] + lma - sec.header['sh_addr']
            break
    # 先检查是不是已经处理过了，是的话就退出
    current = f.tell()
    f.seek(offset)
    magic, node_head, node_num, sumcheck = struct.unpack("%s%s" % (compress_prefix, sct_load_config_t_format),
                                                         f.read(sct_load_config_t_format_size))
    print_debug("magic: 0x%x, node_head: 0x%x, node_num: %d sumcheck: %d"%(magic, node_head, node_num, sumcheck))
    if magic == LOAD_CONFIG_MAGIC:
        raise compress_failed_exception(f, "this elf file alerady handle")
    print_debug("load config offset %d"%(offset))
    f.seek(offset)
    f.write(data)
    f.seek(current)

def overwrite_program_section(f, elf_file):
    sec_offset = get_file_size(f)
    current = f.tell()
    program_num = 0
    f.seek(sec_offset)
    sec_offset_temp = sec_offset
    for sec in elf_file.iter_sections():
        if (sec.header['sh_flags'] & SH_FLAGS.SHF_ALLOC == SH_FLAGS.SHF_ALLOC) and sec.header['sh_type'] == 'SHT_PROGBITS':
            f.seek(sec_offset_temp)
            f.write(struct.pack("IIIIIIII",
                                1,         # p_type PT_LOAD
                                sec.header['sh_offset'],  # p_offset
                                sec.header['sh_addr'],  # p_vaddr
                                sec.header['sh_addr'],  # p_paddr
                                sec.header['sh_size'],  # p_filesz
                                sec.header['sh_size'],  # p_memsz
                                5,  # p_flags
                                65536,  # p_align
                                ))
            program_num += 1
            sec_offset_temp += 32
    # 覆写映射
    f.seek(28)  # e_phoff
    f.write(struct.pack("I",sec_offset))
    f.seek(44)    # e_phnum
    f.write(struct.pack("H",program_num))
    f.seek(current)

def compress_elf(file_path):
    try:
        if os.path.exists(file_path) == False:
            raise compress_failed_exception(None, "can't find file %s"%(file_path))
        shutil.copy(file_path, file_path+"_origin")
        f = open(file_path, 'rb+')
        elf_file_p = ELFFile(f)
        header = elf_file_p.header

        check_elf_type(f, header)
        flash_size_sym, load_config_sym = get_key_word(f, elf_file_p)
        all_sec, fix_sec, zero_sec, remap_sec, copy_sec = section_filter(elf_file_p)

        print_debug("section filter info")
        print_debug("%-32s %-5s %-10s %-10s %10s %15s %8s" % ("name", "type", "lma", "vma", "origin_len", 'compress_len', 'alog'))
        # fix的section不需要额外处理
        for sec in fix_sec:
            print_debug("%-32s %-5s 0x%-8x 0x%-8x %10d %15d %8s" % (elf_file_p._get_section_name(sec.header),
                                                                    "fix", sec['sh_addr'], sec['sh_addr'],
                                                                    sec['sh_size'], sec['sh_size'],
                                                                    compress_algo.load_type_str[0]))

        remap_node_list = []
        remap_data = []
        remap_data_len = 0
        # zero的secton需要remap
        for sec in zero_sec:
            compress_type, compress_len, compress_data = remap_data_compress(f, sec, False)
            remap_node_list.append(
                struct.pack("%s%s" % (compress_prefix, sct_load_node_t_format),
                            0,
                            sec.header['sh_addr'],
                            compress_len,
                            compress_type))
            print_debug("%-32s %-5s 0x%-8x 0x%-8x %10d %15d %8s" % (elf_file_p._get_section_name(sec.header),
                                                                    "zero", 0, sec['sh_addr'],
                                                                    sec['sh_size'], 0,
                                                                    compress_algo.load_type_str[compress_type]))

        # copy的section需要remap
        for sec in copy_sec:
            compress_type, compress_len, compress_data = remap_data_compress(f, sec, True)
            lma_addr_temp = get_section_lma(elf_file_p, sec)
            remap_node_list.append(
                struct.pack("%s%s" % (compress_prefix, sct_load_node_t_format),
                            lma_addr_temp,
                            sec.header['sh_addr'],
                            compress_len,
                            compress_type))
            print_debug("%-32s %-5s 0x%-8x 0x%-8x %10d %15d %8s" % (elf_file_p._get_section_name(sec.header),
                                                                    "copy", lma_addr_temp, sec['sh_addr'],
                                                                    sec['sh_size'], 0,
                                                                    compress_algo.load_type_str[compress_type]))

        # 添加的压缩段追加在fix section的最后
        fix_section_end_addr = fix_sec[len(fix_sec) - 1].header[ 'sh_addr' ] + fix_sec[len(fix_sec) - 1].header[ 'sh_size' ]
        load_node_start_addr = ((fix_section_end_addr+3)//4) * 4
        load_data_start_addr = load_node_start_addr + sct_load_node_t_format_size * (len(remap_node_list) + len(remap_sec))

        # remap的sction需要remap
        for sec in remap_sec:
            lma_addr_temp = load_data_start_addr + remap_data_len
            compress_type, compress_len, compress_data = remap_data_compress(f, sec, False)
            remap_node_list.append(
                struct.pack("%s%s" % (compress_prefix, sct_load_node_t_format),
                            lma_addr_temp,
                            sec.header['sh_addr'],
                            compress_len,
                            compress_type))
            remap_data.append(compress_data)
            remap_data_len += compress_len
            print_debug("%-32s %-5s 0x%-8x 0x%-8x %10d %15d %8s" % (elf_file_p._get_section_name(sec.header),
                                                                    "remap", lma_addr_temp, sec['sh_addr'],
                                                                    sec['sh_size'], compress_len,
                                                                    compress_algo.load_type_str[compress_type]))

        load_node_start_offset = get_file_size(f)

        print_debug("load config node lma 0x%x"%(load_node_start_addr))
        print_debug("load remap data lma 0x%x"%(load_data_start_addr))
        print_debug("load config node offset %d"%(load_node_start_offset))

        # 检查flash够不够用
        flash_used_size = 0
        for sec in fix_sec:
            flash_used_size += sec.header[ 'sh_size' ]
        for sec in copy_sec:
            flash_used_size += sec.header[ 'sh_size' ]
        flash_used_size += 4 #修正load_node_start_addr对齐的长度，按照最差情况处理
        flash_used_size += (sct_load_node_t_format_size * len(remap_node_list))
        flash_used_size += remap_data_len
        if flash_used_size > flash_size_sym['st_value']:
            raise compress_failed_exception("flash code size %d more than %s %d"%(flash_used_size, symbol_flash_size_name, load_config_sym['st_value']))

        if len(remap_sec) != 0:
            overwrite_sec = remap_sec[0]
        elif len(zero_sec) != 0:
            overwrite_sec = zero_sec[0]
        else:
            overwrite_sec = None

        if overwrite_sec != None:
            # 以下开始重写原有的axf
            sum_check = LOAD_CONFIG_MAGIC + load_node_start_addr + len(remap_node_list)
            overwrite_load_config(f, elf_file_p, load_config_sym,
                                struct.pack("%s%s" % (compress_prefix, sct_load_config_t_format),
                                            LOAD_CONFIG_MAGIC,
                                            load_node_start_addr,
                                            len(remap_node_list),
                                            sum_check))
            print_debug("load config write success")

            for sec in remap_sec:
                sec.header['sh_type'] = 'SHT_NOBITS'
                overwrite_section(f, elf_file_p, sec)

            for sec in copy_sec:
                sec.header['sh_addr'] = get_section_lma(elf_file_p, sec)
                overwrite_section(f, elf_file_p, sec)

            # 写入压缩数据段
            current = f.tell()
            f.seek(load_node_start_offset)
            for node in remap_node_list:
                f.write(node)
            for data in remap_data:
                f.write(data)
            print_debug("write %d config node success"%(len(remap_node_list)))
            print_debug("write compress data %d byte success"%(remap_data_len))

            # 重映射压缩数据段
            overwrite_sec.header['sh_type'] = 'SHT_PROGBITS'
            overwrite_sec.header['sh_addr'] = load_node_start_addr
            overwrite_sec.header['sh_size'] = len(remap_node_list) * sct_load_node_t_format_size + remap_data_len
            overwrite_sec.header['sh_offset'] = load_node_start_offset
            overwrite_section(f, elf_file_p, overwrite_sec)
            print_debug("build compress section: lma 0x%x size %d byte" % (overwrite_sec.header['sh_addr'], overwrite_sec.header['sh_size']))

            overwrite_program_section(f, elf_file_p)
            print_debug("write program section success")

            # 统计
            fix_sec_size = 0
            for sec in fix_sec:
                fix_sec_size += sec.header['sh_size']
            remap_sec_size = 0
            for sec in zero_sec:
                remap_sec_size += sec.header['sh_size']
            for sec in copy_sec:
                remap_sec_size += sec.header['sh_size']
            total_sec_size = 0
            for sec in elf_file_p.iter_sections():
                if sec.header['sh_type'] == 'SHT_PROGBITS' and (sec.header['sh_flags'] & SH_FLAGS.SHF_ALLOC == SH_FLAGS.SHF_ALLOC):
                    total_sec_size += sec.header['sh_size']

            print_info("================================================================")
            print_info("gcc compress tool")
            print_info("%-20s %d(%fk)"%("fix section size:", fix_sec_size, fix_sec_size/1024))
            print_info("%-20s %d(%fk)"%("remap section size:", remap_sec_size, remap_sec_size/1024))
            print_info("%-20s %d(%fk)"%("total section size:", total_sec_size, total_sec_size/ 1024))
            print_info("%-20s %.2f%% (%d/%d)"%("flash usage:",total_sec_size / flash_size_sym['st_value'] * 100, total_sec_size , flash_size_sym['st_value']))
            print_info("================================================================")

    # 异常处理
    except compress_failed_exception as user_log:
        print_error(user_log.user_log)
        print_error("read readme.md to learn more")
        if user_log.f != None:
            f.close()
        if user_log.del_file:
            os.remove(file_path)
        exit(1)

help_str = \
"gcc_compress.py -f <elf_file_path> \n\
Option:\n\
-f <elf_file_path> the elf file path will be compress\n\
-g <print_level> 0-error 1-warn 2-info(default) 3-debug\n\
-v print version\n\
"
version_str="1.0.0.1"

if __name__ == '__main__':
    try:
        opts, args = getopt.getopt(sys.argv[1:], "-h:-f:-g:-v", [""])
    except getopt.GetoptError:
        print(help_str)
        sys.exit(2)
    for opt, val in opts:
        if opt in ("-h"):
            print(help_str)
        if opt in ("-f"):
            file_path = val
        if opt in ("-g"):
            print_level = int(val)
        if opt in ("-v"):
            print(version_str)
    if 'file_path' in locals().keys():
        compress_elf(file_path)
    else:
        print(help_str)
        exit(2)
