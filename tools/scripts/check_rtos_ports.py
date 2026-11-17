#!/usr/bin/env python3
"""Compile selected RTOS/CMSIS ports; this is not a board startup/runtime test."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--build', type=Path, default=ROOT / 'out/port-contracts')
    args = parser.parse_args()
    build = args.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    (build / 'soc.h').write_text('#include <stdint.h>\nextern uint32_t SystemCoreClock;\n', encoding='utf-8')
    (build / 'devicetree_generated.h').write_text(
        '#define DT_N_INST_0_arm_v7m_nvic DT_N_S_soc_S_nvic\n'
        '#define DT_N_S_soc_S_nvic_P_arm_num_irq_priority_bits 4\n', encoding='utf-8')
    results = []

    def compile_file(source, label, cpu, fpu, includes, definitions=()):
        command = [str(args.compiler), '-c', str(source), '-o', str(build / (label + '.o')),
                   '-std=gnu11', '-O2', '-mthumb', f'-mcpu={cpu}', f'-mfpu={fpu}', '-mfloat-abi=hard',
                   '-DCONFIG_SYS_CLOCK_TICKS_PER_SEC=1000', '-DCONFIG_FREERTOS_HEAP_SIZE=8192',
                   '-DCONFIG_NUM_IRQ_PRIO_BITS=4', '-Werror=implicit-function-declaration']
        command.extend('-I' + str(path) for path in includes)
        command.extend('-D' + item for item in definitions)
        subprocess.run(command, cwd=ROOT, check=True)
        results.append(label)

    freertos = ROOT / 'embedded/osal/rtos/freertos'
    rtthread = ROOT / 'embedded/osal/rtos/rt-thread'
    ports = [('m4', 'cortex-m4', 'fpv4-sp-d16', 'GCC/ARM_CM4F'),
             ('m7', 'cortex-m7', 'fpv5-d16', 'GCC/ARM_CM7/r0p1'),
             ('m33', 'cortex-m33', 'fpv5-sp-d16', 'GCC/ARM_CM33_NTZ/non_secure')]
    for core, cpu, fpu, portable in ports:
        (build / 'soc.h').write_text(
            '#include <stdint.h>\nextern uint32_t SystemCoreClock;\n'
            'typedef enum { SysTick_IRQn=-1, PendSV_IRQn=-2, SVCall_IRQn=-5, '
            'MemoryManagement_IRQn=-12 } IRQn_Type;\n'
            '#define __MPU_PRESENT 1\n#define __FPU_PRESENT 1\n#define __NVIC_PRIO_BITS 4\n'
            '#define __DSP_PRESENT 1\n'
            f'#define __ICACHE_PRESENT {int(core == "m7")}\n#define __DCACHE_PRESENT {int(core == "m7")}\n'
            f'#include <core_c{core}.h>\n', encoding='utf-8')
        port = freertos / 'portable' / portable
        cmsis = ROOT / 'embedded/modules/cmsis/CMSIS/Core/Include'
        includes = [build, ROOT / 'embedded/osal/freertos', freertos / 'include', port, cmsis]
        compile_file(port / 'port.c', 'freertos-' + core, cpu, fpu, includes)
        if (port / 'portasm.c').exists():
            compile_file(port / 'portasm.c', 'freertos-asm-' + core, cpu, fpu, includes)
        rt_port = rtthread / 'libcpu/arm' / cpu
        rt_includes = [build, cmsis, ROOT / 'embedded/osal/rt-thread', rtthread / 'include', rtthread / 'libcpu/arm/common']
        compile_file(rt_port / 'cpuport.c', 'rtthread-' + core, cpu, fpu, rt_includes)
        compile_file(rt_port / 'context_gcc.S', 'rtthread-context-' + core, cpu, fpu, rt_includes)
        for name in ['trustzone.c', 'cpu_cache.c']:
            if (rt_port / name).exists():
                compile_file(rt_port / name, 'rtthread-' + core + '-' + name, cpu, fpu, rt_includes)
        source = build / ('cmsis-' + core + '.c')
        source.write_text('typedef enum { SysTick_IRQn=-1, PendSV_IRQn=-2, SVCall_IRQn=-5, '
                          'MemoryManagement_IRQn=-12 } IRQn_Type;\n'
                          f'#include <core_c{core}.h>\nvoid cmsis_smoke(void) {{ __DSB(); }}\n', encoding='utf-8')
        compile_file(source, 'cmsis-' + core, cpu, fpu,
                     [ROOT / 'embedded/modules/cmsis/CMSIS/Core/Include'],
                     ['__MPU_PRESENT=1', '__FPU_PRESENT=1', '__NVIC_PRIO_BITS=4', '__DSP_PRESENT=1',
                      f'__ICACHE_PRESENT={int(core == "m7")}', f'__DCACHE_PRESENT={int(core == "m7")}'])

    (build / 'soc.h').write_text('#include <stm32h7xx.h>\n', encoding='utf-8')
    includes = [build, ROOT / 'embedded/include', ROOT / 'embedded/modules/cmsis',
                ROOT / 'embedded/modules/cmsis/CMSIS/Core/Include',
                ROOT / 'embedded/soc/st/stm32cube/stm32h7xx/soc',
                ROOT / 'embedded/soc/st/stm32cube/stm32h7xx/drivers/include',
                ROOT / 'embedded/soc/st/stm32cube/common_ll/include']
    compile_file(ROOT / 'embedded/soc/st/common/stm32_backup_domain.c', 'stm32-backup-domain',
                 'cortex-m7', 'fpv5-d16', includes,
                 ['STM32H743xx', 'USE_FULL_LL_DRIVER', 'CONFIG_ARM=1', 'CONFIG_CPU_CORTEX_M=1',
                  'CONFIG_SOC_SERIES_STM32H7X=1',
                  'CONFIG_CPU_CORTEX_M7=1', 'CONFIG_ARMV7_M_ARMV8_M_MAINLINE=1',
                  'CONFIG_CPU_HAS_ARM_MPU=1', 'CONFIG_CPU_HAS_FPU=1',
                  'CONFIG_CPU_HAS_ICACHE=1', 'CONFIG_CPU_HAS_DCACHE=1'])
    (build / 'report.json').write_text(json.dumps({'verification': 'compile only; no runtime proof',
                                                   'compiled': results}, indent=2) + '\n', encoding='utf-8')
    print(f'{len(results)} RTOS/CMSIS/STM32 port translation units compiled')


if __name__ == '__main__':
    main()
