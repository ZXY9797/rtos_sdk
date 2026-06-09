include(extensions)

# 包含内核定义
dt_arch_type(arch_core 0)
include(${arch_core})

# 编译选项
set(CFCOMMON "${MCPU_FLAGS} ${VFP_FLAGS} ${SYSTEM_PATH} --specs=nano.specs -specs=rdimon.specs --specs=nosys.specs -Wall -fmessage-length=0 -ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS   "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H}")
# ARMGCC9_CXX2A: 由 armgcc9.cmake 设置，GCC 9.x 不支持 -std=c++20
if(ARMGCC9_CXX2A)
    set(CMAKE_CXX_FLAGS "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H} -fno-exceptions -fno-rtti -std=c++2a")
else()
    set(CMAKE_CXX_FLAGS "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H} -fno-exceptions -fno-rtti -std=c++20")
endif()
set(CMAKE_ASM_FLAGS "${MCPU_FLAGS} ${VFP_FLAGS} -x assembler-with-cpp -imacros ${AUTOCONF_H}")

add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-D_ASMLANGUAGE>)

# 获得SOC名字
dt_get_soc_name(soc_name)
set(LINKER_SCRIPT ${BSP_DIR}/linkscript/${soc_name}.ld)
message(STATUS "Link script: ${LINKER_SCRIPT}")

# -u Default_Handler: 强制链接器从 libarch.a 中拉入 vector_table.S.obj
# （向量表 section 无外部引用，--gc-sections 会将其丢弃）
# -u main: 强制链接器将 main 视为根符号，防止 --gc-sections 在
# --unresolved-symbols=ignore-in-object-files 模式下丢弃整个应用代码
set(VECTOR_TABLE_LINK_FLAG "-Wl,-u,Default_Handler,-u,main")

# ARMGCC9_UNRESOLVED: 由 armgcc9.cmake 设置，忽略 BLE 控制器 ROM 未定义符号
# --allow-multiple-definition: CMake --whole-archive + 循环依赖导致 soc.a 重复列出
if(ARMGCC9_UNRESOLVED)
    set(UNRESOLVED_FLAG "-Wl,--unresolved-symbols=ignore-in-object-files -Wl,--allow-multiple-definition")
else()
    set(UNRESOLVED_FLAG "")
endif()

set(CMAKE_C_LINK_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections,--print-memory-usage ${UNRESOLVED_FLAG} ${VECTOR_TABLE_LINK_FLAG}"
)
set(CMAKE_CXX_LINK_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections,--print-memory-usage ${UNRESOLVED_FLAG} ${VECTOR_TABLE_LINK_FLAG}"
)

# 搜索规则
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
