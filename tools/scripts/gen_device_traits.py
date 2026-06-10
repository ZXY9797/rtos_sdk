#!/usr/bin/env python3
"""
DeviceTrait 特化生成器 — YAML 驱动版。

从 DT 绑定 YAML 的 cxx-driver 节读取驱动映射，
解析 devicetree_generated.h，为匹配的设备树节点生成 C++ 模板特化。

加新驱动只需：
  1. 写 .h + .cc
  2. 在 binding YAML 的 child-binding（或节点自身）添加 cxx-driver 节

用法:
    python gen_device_traits.py <devicetree_generated.h> <output.h> <bindings_dir>
"""

import os
import re
import sys
import yaml

DEFAULT_INIT_PRIORITY = 25
DEFAULT_INIT_LEVEL = 'pre-kernel-2'

INIT_LEVELS = {
    'early': 'INITCALL_LEVEL_EARLY',
    'pre-kernel-1': 'INITCALL_LEVEL_PRE_KERNEL_1',
    'pre-kernel-2': 'INITCALL_LEVEL_PRE_KERNEL_2',
    'pre-kernel-3': 'INITCALL_LEVEL_PRE_KERNEL_3',
    'post-kernel': 'INITCALL_LEVEL_POST_KERNEL',
    'application': 'INITCALL_LEVEL_APPLICATION',
}


def normalize_init_level(value):
    if value is None:
        value = DEFAULT_INIT_LEVEL
    if not isinstance(value, str):
        raise ValueError(f'init-level must be a string, got {value!r}')

    key = value.strip().lower().replace('_', '-')
    if key.startswith('initcall-level-'):
        key = key[len('initcall-level-'):]

    aliases = {
        'prekernel1': 'pre-kernel-1',
        'prekernel2': 'pre-kernel-2',
        'prekernel3': 'pre-kernel-3',
        'pre-kernel1': 'pre-kernel-1',
        'pre-kernel2': 'pre-kernel-2',
        'pre-kernel3': 'pre-kernel-3',
        'postkernel': 'post-kernel',
    }
    key = aliases.get(key, key)

    if key not in INIT_LEVELS:
        valid = ', '.join(sorted(INIT_LEVELS))
        raise ValueError(f'unknown init-level {value!r}; expected one of: {valid}')

    return key


# ─── 解析 binding YAML 的 cxx-driver 节 ────────────────────────────────

def parse_yaml_comments(filepath):
    """解析 binding YAML 的 cxx-driver 节。

    YAML 格式:
        compatible: "gpio-leds"
        cxx-driver:
          template: GpioPort
          header: drivers/gpio.h
          args:
            - phandle-reg: gpios
            - field: gpios/pin
            - field: gpios/flags
    """
    with open(filepath, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)

    if not isinstance(data, dict):
        return None, None

    compatible = data.get('compatible')
    cxx_raw = data.get('cxx-driver')

    if not compatible or not isinstance(cxx_raw, dict):
        return None, None

    template = cxx_raw.get('template')
    header = cxx_raw.get('header')
    raw_args = cxx_raw.get('args', [])
    init_level = cxx_raw.get('init-level', DEFAULT_INIT_LEVEL)
    init_priority = cxx_raw.get('init-priority', DEFAULT_INIT_PRIORITY)

    if not template or not header:
        return None, None

    try:
        init_level = normalize_init_level(init_level)
    except ValueError as e:
        raise ValueError(f'invalid init-level in {filepath}: {e}')

    try:
        init_priority = int(init_priority)
    except (TypeError, ValueError):
        raise ValueError(f'invalid init-priority in {filepath}: {init_priority!r}')

    if init_priority < 0 or init_priority > 999:
        raise ValueError(f'init-priority out of range in {filepath}: {init_priority}')

    args = []
    for item in raw_args:
        if isinstance(item, str):
            # 无值参数：node-reg, parent-reg, irq, opt_irq
            if item == 'node-reg':
                args.append(('node_reg', None))
            elif item == 'parent-reg':
                args.append(('parent_reg', None))
            elif item == 'irq':
                args.append(('irq', None))
            elif item == 'opt_irq':
                args.append(('opt_irq', None))
        elif isinstance(item, dict) and len(item) == 1:
            key, val = next(iter(item.items()))
            if key == 'phandle-reg':
                args.append(('phandle_reg', val))
            elif key == 'phandle-ord':
                args.append(('phandle_ord', val))
            elif key == 'phandle-parent-reg':
                args.append(('phandle_parent_reg', val))
            elif key == 'node-reg':
                args.append(('node_reg', None))
            elif key == 'node-prop':
                args.append(('prop_val', val))
            elif key == 'parent-reg':
                args.append(('parent_reg', None))
            elif key == 'field':
                parts = val.split('/')
                if len(parts) == 2:
                    args.append(('prop_field_val', parts[0], parts[1]))
            elif key == 'prop':
                args.append(('prop_val', val))
            elif key == 'literal':
                args.append(('literal', val))

    cxx = {
        'template': template,
        'header': header,
        'args': args,
        'init_level': init_level,
        'init_priority': init_priority,
    }

    isr_handler = cxx_raw.get('isr-handler')
    if isr_handler:
        cxx['isr_handler'] = isr_handler

    device_base = cxx_raw.get('device-base', False)
    cxx['device_base'] = bool(device_base)

    # 解析 init-cfg（init() 参数映射）
    init_cfg_raw = cxx_raw.get('init-cfg')
    if isinstance(init_cfg_raw, dict):
        cfg = {
            'type': init_cfg_raw.get('type', 'Config'),
            'rx_buffer_size': 0,
            'fields': {},
        }
        rx_buf = init_cfg_raw.get('rx-buffer-size')
        if isinstance(rx_buf, dict):
            cfg['rx_buffer_size'] = rx_buf.get('default', 0)
        fields_raw = init_cfg_raw.get('fields', {})
        if isinstance(fields_raw, dict):
            for fname, fdef in fields_raw.items():
                if isinstance(fdef, dict):
                    cfg['fields'][fname] = {
                        'type': fdef.get('type', 'int'),
                        'prop': fdef.get('prop'),
                        'default': fdef.get('default'),
                        'cast': fdef.get('cast'),
                        'scale': fdef.get('scale'),
                    }
        cxx['init_cfg'] = cfg

    return compatible, cxx


def normalize_compat(s):
    """compatible 字符串标准化：连字符/点/逗号 → 下划线。"""
    return s.replace('-', '_').replace('.', '_').replace(',', '_')


def scan_bindings(bindings_dir):
    """扫描所有 binding YAML，提取 compatible → cxx-driver 映射。"""
    compat_map = {}  # normalized_compatible -> { template, header, args }

    if not os.path.isdir(bindings_dir):
        return compat_map

    for root, dirs, files in os.walk(bindings_dir):
        for fname in sorted(files):
            if not fname.endswith('.yaml'):
                continue
            fpath = os.path.join(root, fname)
            compatible, cxx = parse_yaml_comments(fpath)
            if compatible and cxx and 'template' in cxx and 'args' in cxx:
                cxx['compatible'] = compatible  # 保留原始格式用于显示
                compat_map[normalize_compat(compatible)] = cxx

    return compat_map


# ─── 解析 devicetree_generated.h ───────────────────────────────────────

def parse_generated_header(filepath):
    """解析 devicetree_generated.h，提取所有节点信息。"""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # ordinal
    ord_re = re.compile(r'^#define\s+(DT_N_(?:S_\w+)+)_ORD\s+(\d+)', re.MULTILINE)
    ordinals = {}
    for m in ord_re.finditer(content):
        ordinals[m.group(1)] = int(m.group(2))

    # reg address: node_REG_IDX_0_VAL_ADDRESS <number>
    reg_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_REG_IDX_0_VAL_ADDRESS\s+(\d+)',
        re.MULTILINE)
    reg_addrs = {}
    for m in reg_re.finditer(content):
        reg_addrs[m.group(1)] = int(m.group(2))

    # compatible 匹配: node_COMPAT_MATCHES_xxx 1
    compat_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_COMPAT_MATCHES_(\S+)\s+1',
        re.MULTILINE)
    node_compats = {}  # node_id -> [compat1, compat2, ...]
    for m in compat_re.finditer(content):
        node_compats.setdefault(m.group(1), []).append(m.group(2))

    # 节点拥有的属性: node_P_prop_EXISTS 1
    prop_exists_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)_EXISTS\s+1', re.MULTILINE)
    node_props = {}
    for m in prop_exists_re.finditer(content):
        node_props.setdefault(m.group(1), set()).add(m.group(2))

    # phandle 引用: node_P_prop_IDX_0_PH target
    ph_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)_IDX_0_PH\s+(DT_N_(?:S_\w+)+)',
        re.MULTILINE)
    phandles = {}
    for m in ph_re.finditer(content):
        phandles[(m.group(1), m.group(2))] = m.group(3)

    # 属性字段值: node_P_prop_IDX_0_VAL_field <number>
    val_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)_IDX_\d+_VAL_(\w+)\s+(\d+)',
        re.MULTILINE)
    prop_fields = {}
    for m in val_re.finditer(content):
        prop_fields[(m.group(1), m.group(2), m.group(3))] = int(m.group(4))

    # 直接整数属性
    direct_props = {}
    for line in content.splitlines():
        m = re.match(r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)\s+(\d+)\s*$', line)
        if not m:
            continue
        prop = m.group(2)
        if any(prop.endswith(s) for s in ('_EXISTS', '_LEN', '_NUM')):
            continue
        if any(f'_{p}_' in prop for p in ('IDX', 'FOREACH', 'ENUM', 'PHA')):
            continue
        direct_props[(m.group(1), prop)] = int(m.group(3))

    # 直接字符串属性，例如 node_P_init_level "pre-kernel-3"
    string_prop_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)\s+"([^"]*)"',
        re.MULTILINE)
    string_props = {}
    for m in string_prop_re.finditer(content):
        string_props[(m.group(1), m.group(2))] = m.group(3)

    # alias
    alias_re = re.compile(
        r'^#define\s+DT_N_ALIAS_(\w+)\s+(DT_N_(?:S_\w+)+)', re.MULTILINE)
    aliases = {}
    for m in alias_re.finditer(content):
        aliases[m.group(2)] = m.group(1)

    # IRQ numbers: node_IRQ_IDX_0_VAL_irq <number>
    irq_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_IRQ_IDX_\d+_VAL_irq\s+(\d+)',
        re.MULTILINE)
    irq_nums = {}
    for m in irq_re.finditer(content):
        irq_nums[m.group(1)] = int(m.group(2))

    # 节点 status 属性: node_P_status "okay" / "disabled"
    status_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_status\s+"(\w+)"', re.MULTILINE)
    node_status = {}
    for m in status_re.finditer(content):
        node_status[m.group(1)] = m.group(2)

    # 构建父节点 → compatible 映射
    # 父节点 ID 是子节点 ID 的前缀（去掉最后一段 _S_xxx）
    parent_compat = {}  # node_id -> compatible (从父节点继承)
    for node_id, compats in node_compats.items():
        # 这个节点自己有 compatible
        for other_id in ordinals:
            if other_id != node_id and other_id.startswith(node_id + '_S_'):
                # other_id 是 node_id 的子节点
                if compats:
                    parent_compat[other_id] = compats[0]

    return {
        'ordinals': ordinals,
        'reg_addrs': reg_addrs,
        'node_compats': node_compats,
        'node_props': node_props,
        'phandles': phandles,
        'prop_fields': prop_fields,
        'direct_props': direct_props,
        'string_props': string_props,
        'aliases': aliases,
        'parent_compat': parent_compat,
        'node_status': node_status,
        'irq_nums': irq_nums,
    }


# ─── 生成特化代码 ─────────────────────────────────────────────────────

def resolve_arg(arg, node_id, dt_data):
    """将参数模板解析为 C++ 源码片段。"""
    arg_type = arg[0]

    if arg_type == 'phandle_reg':
        prop = arg[1].replace('-', '_')
        target = dt_data['phandles'].get((node_id, prop))
        if target and target in dt_data['reg_addrs']:
            return f'DT_REG_ADDR({target})'
        return None

    elif arg_type == 'phandle_ord':
        prop = arg[1].replace('-', '_')
        target = dt_data['phandles'].get((node_id, prop))
        if target and target in dt_data['ordinals']:
            return str(dt_data['ordinals'][target])
        return None

    elif arg_type == 'phandle_parent_reg':
        prop = arg[1].replace('-', '_')
        target = dt_data['phandles'].get((node_id, prop))
        if target:
            last_sep = target.rfind('_S_')
            if last_sep > 0:
                parent_id = target[:last_sep]
                if parent_id in dt_data['reg_addrs']:
                    return f'DT_REG_ADDR({parent_id})'
        return None

    elif arg_type == 'node_reg':
        if node_id in dt_data['reg_addrs']:
            return f'DT_REG_ADDR({node_id})'
        return None

    elif arg_type == 'parent_reg':
        # 从子节点 ID 推导父节点 ID（去掉最后一段 _S_xxx）
        last_sep = node_id.rfind('_S_')
        if last_sep > 0:
            parent_id = node_id[:last_sep]
            if parent_id in dt_data['reg_addrs']:
                return f'DT_REG_ADDR({parent_id})'
        return None

    elif arg_type == 'prop_field_val':
        prop, field = arg[1].replace('-', '_'), arg[2]
        if (node_id, prop, field) in dt_data['prop_fields']:
            return f'{node_id}_P_{prop}_IDX_0_VAL_{field}'
        return None

    elif arg_type == 'prop_val':
        prop = arg[1]
        prop_norm = prop.replace('-', '_')
        if (node_id, prop_norm) in dt_data['direct_props']:
            return f'{node_id}_P_{prop_norm}'
        if (node_id, prop_norm, prop_norm) in dt_data['prop_fields']:
            return f'{node_id}_P_{prop_norm}_VAL_{prop_norm}'
        return None

    elif arg_type == 'literal':
        return f'"{arg[1]}"'

    elif arg_type == 'irq':
        irq = dt_data['irq_nums'].get(node_id)
        if irq is not None:
            return str(irq)
        return None

    elif arg_type == 'opt_irq':
        irq = dt_data['irq_nums'].get(node_id)
        return str(irq) if irq is not None else '-1'

    return None


def get_node_compatible(node_id, dt_data):
    """获取节点的 compatible（自身或从父节点继承）。"""
    # 自身有 compatible
    compats = dt_data['node_compats'].get(node_id, [])
    if compats:
        return compats[0]
    # 从父节点继承
    return dt_data['parent_compat'].get(node_id)


def is_node_enabled(node_id, dt_data):
    """节点是否启用（status = "okay" 或无 status 属性）。"""
    status = dt_data['node_status'].get(node_id)
    if status is None:
        return True  # 无 status 属性，默认启用
    return status == 'okay'


def generate_specializations(dt_data, compat_map):
    """为所有匹配且启用的节点生成特化代码。"""
    specs = []
    used_headers = set()

    for node_id, ord_val in dt_data['ordinals'].items():
        compat = get_node_compatible(node_id, dt_data)
        if not compat or compat not in compat_map:
            continue

        enabled = is_node_enabled(node_id, dt_data)

        driver = compat_map[compat]
        args_code = []
        skip = False

        for arg in driver['args']:
            resolved = resolve_arg(arg, node_id, dt_data)
            if resolved is None:
                skip = True
                break
            args_code.append(resolved)

        if skip:
            continue

        alias = dt_data['aliases'].get(node_id, '')
        spec = {
            'ord': ord_val,
            'alias': alias,
            'type': driver['template'],
            'args': args_code,
            'header': driver['header'],
            'enabled': enabled,
            'node_id': node_id,
            'init_level': driver.get('init_level', DEFAULT_INIT_LEVEL),
            'init_priority': driver.get('init_priority', DEFAULT_INIT_PRIORITY),
        }
        if 'init_cfg' in driver:
            spec['init_cfg'] = driver['init_cfg']

        # ISR handler：从 raw args 中找到 irq 参数的位置
        isr_handler = driver.get('isr_handler')
        if isr_handler:
            irq_index = None
            for i, arg in enumerate(driver['args']):
                if arg[0] == 'irq':
                    irq_index = i
                    break
            if irq_index is not None:
                spec['isr_handler'] = isr_handler
                spec['irq_arg_index'] = irq_index

        if driver.get('device_base', False):
            spec['device_base'] = True

        specs.append(spec)
        used_headers.add(driver['header'])

    specs.sort(key=lambda s: s['ord'])
    return specs, used_headers


def check_init_dependencies(specs):
    """检查 phandle 依赖的设备是否先初始化。

    如果设备 A 通过 phandle 引用设备 B，且两者在同一 init level 内，
    则 A 的 init-priority 必须大于 B（数值越大越晚初始化）。
    """
    spec_by_ord = {s['ord']: s for s in specs}
    warnings = []

    level_order = {v: i for i, v in enumerate(INIT_LEVELS.values())}

    for spec in specs:
        if not spec['enabled']:
            continue
        for arg in spec.get('args', []):
            if arg[0] in ('phandle_ord', 'phandle_parent_reg'):
                target_ord = int(arg[1]) if arg[0] == 'phandle_ord' else None
                if target_ord is not None:
                    target = spec_by_ord.get(target_ord)
                    if target and target['enabled']:
                        src = spec['alias'] or f"ord={spec['ord']}"
                        dst = target['alias'] or f"ord={target['ord']}"
                        # 同级检查：priority 必须更大
                        if (spec['init_level'] == target['init_level'] and
                                spec['init_priority'] < target['init_priority']):
                            warnings.append(
                                f"WARNING: {src} (prio={spec['init_priority']}) "
                                f"depends on {dst} (prio={target['init_priority']}) "
                                f"but initializes earlier!")
                        # 跨级检查：A 的 level 不能早于 B 的 level
                        src_lvl = level_order.get(spec['init_level'], 99)
                        dst_lvl = level_order.get(target['init_level'], 99)
                        if src_lvl < dst_lvl:
                            warnings.append(
                                f"ERROR: {src} (level={spec['init_level']}) "
                                f"depends on {dst} (level={target['init_level']}) "
                                f"but initializes at an earlier level!")

    for w in warnings:
        print(w)

    return len(warnings) == 0


def write_output(specs, used_headers, output_path):
    """生成 drivers_generated.h。"""
    lines = [
        '// Auto-generated by gen_device_traits.py. DO NOT EDIT.',
        '#pragma once',
        '',
        '#include <device.h>',
    ]

    for header in sorted(used_headers):
        lines.append(f'#include <{header}>')

    lines.extend([
        '',
        'namespace hal {',
        '',
    ])

    # DeviceTrait 特化 + instance 声明（仅启用的节点）
    for spec in specs:
        if not spec['enabled']:
            continue

        ord_val = spec['ord']
        alias = spec['alias']
        template_type = spec['type']
        args = spec['args']

        comment = f'// {alias}' if alias else f'// ord={ord_val}'
        lines.append(f'{comment}')
        lines.append(f'template <> struct DeviceTrait<{ord_val}> {{')
        if args:
            args_str = ',\n        '.join(args)
            lines.append(f'    using type = {template_type}<')
            lines.append(f'        {args_str}>;')
        else:
            lines.append(f'    using type = {template_type};')
        lines.append(f'    static type instance;')
        lines.append(f'}};')
        lines.append('')

    # device_get 模板
    lines.extend([
        '/// 编译期设备获取 — 返回已初始化的全局实例引用',
        'template <int Ord>',
        'inline auto &device_get() { return DeviceTrait<Ord>::instance; }',
        '',
    ])

    # 模板显式实例化 — hal 命名空间内的类型
    hal_instantiations = []
    ext_instantiations = []
    for spec in specs:
        if not spec['enabled']:
            continue
        template_type = spec['type']
        args = ', '.join(spec['args'])
        if not args:
            continue  # 非模板类不需要显式实例化
        line = f'template class {template_type}<{args}>;'
        if '::' in template_type:
            ext_instantiations.append(line)
        else:
            hal_instantiations.append(line)

    if hal_instantiations:
        lines.append('// 模板显式实例化（仅 DTS 中 status = "okay" 的节点）')
        lines.extend(hal_instantiations)
        lines.append('')

    # 设备注册表 — 用于运行时枚举
    enabled_specs = [s for s in specs if s['enabled']]
    if enabled_specs:
        # 为 DeviceBase 派生类型生成 is_ready 检查函数
        for spec in enabled_specs:
            if spec.get('device_base', False):
                ord_val = spec['ord']
                alias = spec['alias'] or f'ord{ord_val}'
                lines.append(
                    f'inline bool _check_{alias}(void *inst) {{')
                lines.append(
                    f'    return static_cast<DeviceBase *>(inst)->is_ready();')
                lines.append('}')

        lines.append('// 设备注册表 — 用于运行时枚举和调试')
        lines.append('inline const DeviceInfo s_device_registry[] = {')
        for spec in enabled_specs:
            ord_val = spec['ord']
            alias = spec['alias'] or f'ord{ord_val}'
            type_name = spec['type'].split('::')[-1]
            check_fn = f'_check_{alias}' if spec.get('device_base') else 'nullptr'
            lines.append(f'    {{ .ord = {ord_val}, .alias = "{alias}", '
                        f'.type_name = "{type_name}", '
                        f'.instance = &DeviceTrait<{ord_val}>::instance, '
                        f'.is_ready = {check_fn} }},')
        lines.append('};')
        lines.append('')
        lines.append('inline const DeviceInfo *get_device_registry(size_t *count) {')
        lines.append(f'    *count = {len(enabled_specs)};')
        lines.append('    return s_device_registry;')
        lines.append('}')
        lines.append('')

    lines.extend([
        '} // namespace hal',
        '',
    ])

    # 非 hal 命名空间的模板实例化（在全局命名空间中）
    if ext_instantiations:
        lines.append('// 跨命名空间模板显式实例化')
        lines.extend(ext_instantiations)
        lines.append('')

    lines.extend([
        '/// device_get(alias) — 通过 DTS 别名获取已初始化的设备引用',
        '#define device_get(alias) hal::device_get<DT_ORD(DT_ALIAS(alias))>()',
        '',
    ])

    # DT_INST_* 宏（供 .cc 中条件编译使用）
    # 格式：DT_INST_<TYPE>_<HEX_ADDR> — 按驱动类型 + 基地址索引
    lines.append('// 实例化标记宏 — 供驱动 .cc 中条件编译使用')
    for spec in specs:
        if not spec['enabled']:
            continue
        # 从 node_id 末尾提取基地址（如 DT_N_S_soc_S_serial_40011000 → 40011000）
        node_id = spec['node_id']
        addr_hex = node_id.split('_')[-1].upper()
        # 宏名中不允许 ::，替换为 _
        safe_type = spec['type'].replace('::', '_')
        lines.append(f'#define DT_INST_{safe_type}_{addr_hex}')
    lines.append('')

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))


# ─── 生成 drivers_generated.cc ───────────────────────────────────────

def _resolve_init_cfg_field(spec, field_name, field_def, dt_data):
    """将 init-cfg 字段解析为 C++ 值表达式。返回 (value_str, cast_type_or_None)。"""
    field_type = field_def.get('type', 'int')
    prop_name = field_def.get('prop')
    default_val = field_def.get('default')
    cast_type = field_def.get('cast')
    scale = field_def.get('scale')

    # string 类型：从 DTS 字符串属性读取
    if field_type == 'string':
        if prop_name:
            prop_normalized = prop_name.replace('-', '_')
            node_id = spec['node_id']
            if (node_id, prop_normalized) in dt_data.get('string_props', {}):
                val = dt_data['string_props'][(node_id, prop_normalized)]
                return f'"{val}"', None
        if default_val is not None:
            return f'"{default_val}"', None
        return None, None

    # 获取原始整数值
    raw_val = None
    if prop_name:
        # YAML 中用连字符，DTS 宏中用下划线
        prop_normalized = prop_name.replace('-', '_')
        node_id = spec['node_id']
        if (node_id, prop_normalized) in dt_data.get('direct_props', {}):
            raw_val = dt_data['direct_props'][(node_id, prop_normalized)]
    if raw_val is None:
        raw_val = default_val
    if raw_val is None:
        return None, None

    # scale: 整数 × 缩放因子 → 浮点字面量
    if scale is not None:
        float_val = float(raw_val) * float(scale)
        return f'{float_val}f', None

    return str(raw_val), cast_type


def resolve_init_priority(spec, dt_data):
    """Return DTS node init-priority override, or the binding default."""
    node_id = spec['node_id']
    prop_key = (node_id, 'init_priority')
    if prop_key in dt_data.get('direct_props', {}):
        prio = dt_data['direct_props'][prop_key]
    else:
        prio = spec.get('init_priority', DEFAULT_INIT_PRIORITY)

    try:
        prio = int(prio)
    except (TypeError, ValueError):
        raise ValueError(f'invalid init-priority for {node_id}: {prio!r}')

    if prio < 0 or prio > 999:
        raise ValueError(f'init-priority out of range for {node_id}: {prio}')

    return prio


def resolve_init_level(spec, dt_data):
    """Return DTS node init-level override, or the binding default."""
    node_id = spec['node_id']
    prop_key = (node_id, 'init_level')
    if prop_key in dt_data.get('string_props', {}):
        level = dt_data['string_props'][prop_key]
    else:
        level = spec.get('init_level', DEFAULT_INIT_LEVEL)

    try:
        level = normalize_init_level(level)
    except ValueError as e:
        raise ValueError(f'invalid init-level for {node_id}: {e}')

    return INIT_LEVELS[level]


def write_cc_output(specs, cc_path, dt_data):
    """生成 drivers_generated.cc — 实例定义 + initcall 注册。"""
    lines = [
        '// Auto-generated by gen_device_traits.py. DO NOT EDIT.',
        '',
        '#include <drivers_generated.h>',
        '#include <init.h>',
        '',
        'namespace hal {',
        '',
    ]

    init_funcs = []  # (alias, func_name, level_macro, priority)

    for spec in specs:
        if not spec['enabled']:
            continue

        alias = spec['alias'] or f'ord{spec["ord"]}'
        safe_name = normalize_compat(alias) if alias else f'ord{spec["ord"]}'
        init_cfg = spec.get('init_cfg')

        # RX buffer（UART 类驱动需要）
        if init_cfg and init_cfg.get('rx_buffer_size', 0) > 0:
            buf_size = init_cfg['rx_buffer_size']
            lines.append(f'static uint8_t _dev_{safe_name}_rx_buf[{buf_size}];')
            lines.append('')

        # 实例定义（构造函数由模板参数自动传递，无需显式参数）
        lines.append(f'// {alias}')
        lines.append(
            f'DeviceTrait<{spec["ord"]}>::type '
            f'DeviceTrait<{spec["ord"]}>::instance{{}};'
        )
        lines.append('')

        # init 函数（如果有 init-cfg）
        if init_cfg:
            cfg_type = init_cfg['type']
            func_name = f'_init_{safe_name}'

            lines.append(f'static int {func_name}() {{')
            lines.append(f'    {cfg_type} cfg {{}};')

            for fname, fdef in init_cfg.get('fields', {}).items():
                val, cast_type = _resolve_init_cfg_field(spec, fname, fdef, dt_data)
                if val is not None:
                    if cast_type:
                        lines.append(f'    cfg.{fname} = static_cast<{cast_type}>({val});')
                    else:
                        lines.append(f'    cfg.{fname} = {val};')

            # rx_buffer 特殊处理
            if init_cfg.get('rx_buffer_size', 0) > 0:
                lines.append(f'    cfg.rx_buffer = _dev_{safe_name}_rx_buf;')
                lines.append(
                    f'    cfg.rx_buffer_size = sizeof(_dev_{safe_name}_rx_buf);'
                )

            lines.append(
                f'    return static_cast<int>('
                f'DeviceTrait<{spec["ord"]}>::instance.init(cfg));'
            )
            lines.append('}')
            lines.append('')

            init_funcs.append((
                alias,
                func_name,
                resolve_init_level(spec, dt_data),
                resolve_init_priority(spec, dt_data),
            ))

    lines.extend([
        '} // namespace hal',
        '',
    ])

    # ISR handler 生成（namespace 外，extern "C" linkage）
    for spec in specs:
        if not spec['enabled'] or 'isr_handler' not in spec:
            continue
        irq_index = spec['irq_arg_index']
        irq_num = spec['args'][irq_index]
        ord_val = spec['ord']
        type_name = f'{spec["type"]}<{", ".join(spec["args"])}>'
        isr_method = spec['isr_handler']
        alias = spec['alias'] or f'ord{ord_val}'
        lines.append(f'// {alias} ISR')
        lines.append(f'extern "C" void IRQ{irq_num}_Handler(void) {{')
        lines.append(f'    hal::DeviceTrait<{ord_val}>::'
                      f'instance.{isr_method}();')
        lines.append('}')
        lines.append('')

    # initcall 注册（namespace 外，C linkage）
    for alias, func_name, level_macro, prio in init_funcs:
        lines.append(
            f'SYS_INIT(hal::{func_name}, {level_macro}, {prio});'
        )

    lines.append('')

    with open(cc_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))


# ─── 入口 ─────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 3:
        print(f'用法: {sys.argv[0]} <devicetree_generated.h> <output.h> [bindings_dir] [output.cc]')
        sys.exit(1)

    dt_header = sys.argv[1]
    output_path = sys.argv[2]
    bindings_dir = sys.argv[3] if len(sys.argv) > 3 else None
    cc_path = sys.argv[4] if len(sys.argv) > 4 else None

    # 1. 扫描 binding YAML
    compat_map = {}
    if bindings_dir:
        compat_map = scan_bindings(bindings_dir)
        if compat_map:
            print(f'扫描 binding YAML: {len(compat_map)} 个 cxx-driver 注册')
            for c, d in compat_map.items():
                print(f'  "{d.get("compatible", c)}" -> {d["template"]}')

    # 2. 解析设备树头文件
    dt_data = parse_generated_header(dt_header)

    # 3. 生成特化
    specs, used_headers = generate_specializations(dt_data, compat_map)

    # 3.5 检查 init 依赖
    if specs:
        if not check_init_dependencies(specs):
            print("WARNING: Init dependency issues detected! "
                  "Devices may access uninitialized dependencies.")

    # 4. 写入 .h
    write_output(specs, used_headers, output_path)
    print(f'生成 {output_path}: {len(specs)} 个设备特化')

    # 5. 写入 .cc（如果有 init-cfg 的节点）
    if cc_path:
        has_init = any(s['enabled'] and 'init_cfg' in s for s in specs)
        if has_init:
            write_cc_output(specs, cc_path, dt_data)
            print(f'生成 {cc_path}: initcall 注册')
        else:
            # 生成空 .cc 避免 CMake 报错
            with open(cc_path, 'w', encoding='utf-8') as f:
                f.write('// Auto-generated by gen_device_traits.py. DO NOT EDIT.\n')
                f.write('// No devices with init-cfg found.\n')
            print(f'生成 {cc_path}: 无 initcall（无 init-cfg 节点）')


if __name__ == '__main__':
    main()
