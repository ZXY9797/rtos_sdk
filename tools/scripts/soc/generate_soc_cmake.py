#!/usr/bin/env python3
import sys
import re
import os

import yaml

def find_soc_in_yaml(soc_name_input, yaml_data):
    """
    在YAML数据中查找SOC信息，支持模糊匹配并忽略大小写

    Args:
        soc_name_input: 输入的SOC名称
        yaml_data: 加载的YAML数据

    Returns:
        tuple: (family, series, exact_soc_name) 或 None
    """
    # 将输入转换为小写用于比较
    soc_name_input_lower = soc_name_input.lower()

    exact_match = None
    partial_match = None

    if 'family' in yaml_data:
        for family in yaml_data['family']:
            family_name = family.get('name', '')
            if 'series' in family:
                for series in family['series']:
                    series_name = series.get('name', '')
                    if 'socs' in series:
                        for soc in series['socs']:
                            soc_name = soc.get('name', '')
                            soc_name_lower = soc_name.lower()

                            # 精确匹配（忽略大小写）
                            if soc_name_lower == soc_name_input_lower:
                                return family_name, series_name, soc_name

                            # 部分匹配（忽略大小写，处理名称变体）
                            # 移除尾部的'x'字符进行更宽松的匹配
                            base_input = soc_name_input_lower.rstrip('x')
                            base_soc = soc_name_lower.rstrip('x')

                            if (base_input.startswith(base_soc) or
                                base_soc.startswith(base_input) or
                                base_input in base_soc or
                                base_soc in base_input):
                                partial_match = (family_name, series_name, soc_name)

    # 如果没有精确匹配，返回部分匹配
    return partial_match

def parse_soc_info(compatible_str, yaml_file_path, fuzzy_match=False):
    """
    解析SOC信息，支持模糊匹配并忽略大小写

    Args:
        compatible_str: 兼容性字符串
        yaml_file_path: YAML文件路径
        fuzzy_match: 是否启用模糊匹配

    Returns:
        tuple: (vendor, family, series, soc_name)
    """
    try:
        # 解析兼容性字符串
        parts = compatible_str.split(',')
        if len(parts) != 2:
            raise ValueError(f"Invalid compatible string format: {compatible_str}")

        vendor = parts[0].strip()
        soc_name_input = parts[1].strip()

        # 加载YAML文件
        with open(yaml_file_path, 'r') as file:
            data = yaml.safe_load(file)

        # 查找SOC信息
        if fuzzy_match:
            result = find_soc_in_yaml(soc_name_input, data)
        else:
            # 非模糊匹配也要忽略大小写
            result = None
            soc_name_input_lower = soc_name_input.lower()

            if 'family' in data:
                for family in data['family']:
                    family_name = family.get('name', '')
                    if 'series' in family:
                        for series in family['series']:
                            series_name = series.get('name', '')
                            if 'socs' in series:
                                for soc in series['socs']:
                                    soc_name = soc.get('name', '')
                                    soc_name_lower = soc_name.lower()
                                    if soc_name_lower == soc_name_input_lower:
                                        result = (family_name, series_name, soc_name)
                                        break
                                if result:
                                    break
                        if result:
                            break

        if result:
            family, series, exact_soc_name = result
            return vendor, family, series, exact_soc_name
        else:
            print(f"Error: SOC '{soc_name_input}' not found in YAML file", file=sys.stderr)
            return None

    except Exception as e:
        print(f"Error parsing SOC info: {e}", file=sys.stderr)
        return None

def generate_cmake_file(output_path, vendor, family, series, soc_name, additional_info=None):
    """
    生成包含SOC信息的CMake文件

    Args:
        output_path: 输出的CMake文件路径
        vendor: SOC厂商
        family: SOC系列
        series: SOC子系列
        soc_name: SOC名称
        additional_info: 额外的SOC信息字典
    """
    # 生成标准大写版本
    vendor_upper = vendor.upper()
    family_upper = family.upper()
    series_upper = series.upper()
    soc_name_upper = soc_name.upper()

    # 构建CMake变量部分
    cmake_variables = f"""# SOC信息 - 自动生成
# 请不要手动编辑此文件

set(SOC_VENDOR "{vendor}")
set(SOC_FAMILY "{family}")
set(SOC_SERIES "{series}")
set(SOC_NAME "{soc_name}")
"""

    # 添加额外信息
    if additional_info:
        for key, value in additional_info.items():
            cmake_variables += f'set({key} "{value}")\n'

    # 添加CMake逻辑 - 使用标准大写版本
    cmake_content = cmake_variables + f"""
# 设置SOC相关变量（标准大写版本）
set(SOC_VENDOR_UPPER "{vendor_upper}")
set(SOC_FAMILY_UPPER "{family_upper}")
set(SOC_SERIES_UPPER "{series_upper}")
set(SOC_NAME_UPPER "{soc_name_upper}")

set(SOC_VENDOR_${{SOC_VENDOR_UPPER}} TRUE)
set(SOC_FAMILY_${{SOC_FAMILY_UPPER}} TRUE)
set(SOC_SERIES_${{SOC_SERIES_UPPER}} TRUE)
set(SOC_NAME_${{SOC_NAME_UPPER}} TRUE)

# 打印SOC信息
message(STATUS "SOC Information:")
message(STATUS "  Vendor: ${{SOC_VENDOR}}")
message(STATUS "  Family: ${{SOC_FAMILY}}")
message(STATUS "  Series: ${{SOC_SERIES}}")
message(STATUS "  Name: ${{SOC_NAME}}")
message(STATUS "  Upper Case: ${{SOC_VENDOR_UPPER}}, ${{SOC_FAMILY_UPPER}}, ${{SOC_SERIES_UPPER}}, ${{SOC_NAME_UPPER}}")
"""

    try:
        # 确保输出目录存在
        os.makedirs(os.path.dirname(output_path), exist_ok=True)

        # 写入CMake文件
        with open(output_path, 'w') as f:
            f.write(cmake_content)

        print(f"Generated CMake file: {output_path}", file=sys.stderr)
        return True
    except Exception as e:
        print(f"Error writing CMake file: {e}", file=sys.stderr)
        return False

def main():
    if len(sys.argv) < 4 or len(sys.argv) > 5:
        print("Usage: python generate_soc_cmake_simple.py <compatible_string> <yaml_file> <output_cmake_file> [--fuzzy]")
        print("Example: python generate_soc_cmake_simple.py \"st,stm32h743xx\" soc.yml soc_info.cmake")
        print("Example: python generate_soc_cmake_simple.py \"ST,STM32H743\" soc.yml soc_info.cmake --fuzzy")
        sys.exit(1)

    compatible_str = sys.argv[1]
    yaml_file = sys.argv[2]
    output_cmake_file = sys.argv[3]
    fuzzy_match = (len(sys.argv) == 5 and sys.argv[4] == '--fuzzy')

    # 解析SOC信息
    result = parse_soc_info(compatible_str, yaml_file, fuzzy_match)

    if result:
        vendor, family, series, soc_name = result

        # 可以添加额外的SOC信息
        additional_info = {
            "SOC_COMPATIBLE": f"{vendor},{soc_name}",
            "SOC_ARCH": "cortex-m" if family.lower() == "stm32" else "unknown"
        }

        # 生成CMake文件
        success = generate_cmake_file(output_cmake_file, vendor, family, series, soc_name, additional_info)

        if success:
            print(f"Successfully generated SOC CMake file: {output_cmake_file}", file=sys.stderr)
            sys.exit(0)
        else:
            print(f"Failed to generate CMake file", file=sys.stderr)
            sys.exit(1)
    else:
        print("Failed to parse SOC information", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()