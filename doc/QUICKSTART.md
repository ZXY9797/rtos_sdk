# 快速开始

## 环境要求

```bash
# 基础工具
python3           # 设备树解析 + 驱动特化生成
cmake (>= 3.20)   # 构建系统
ninja             # 构建后端

# ARM 交叉编译器（按项目选择）
# 默认/GR5525: xPack GCC 9.3.1（工程对该版本使用 -std=c++2a）
# demo (GD32): 也可显式 -Dt=armgcc 使用现代 arm-gnu-toolchain
```

## 构建 Demo（GD32 FOC 电机控制）

```bash
# 配置（未指定 -Dt 时默认 armgcc9）
cmake -B out -GNinja -Dp=demo

# 编译
ninja -C out

# 产物
ls out/demo.elf  out/demo.bin  out/demo.hex
```

Demo 源码在 `app/product/demo/main.cc`，产品目录结构见 [APP_LAYOUT.md](APP_LAYOUT.md)。该工程面向 GD32F503，展示 FOC 电机控制（PWM + ADC + CLI 调试）。

## 构建 Demo BLE（GR5525 HID 键盘 + UART 透传）

```bash
# 设置 GCC 9.3.1 工具链路径（Goodix libble_sdk.a 要求）
export ARMGCC9_TOOLCHAIN_PATH=/path/to/xpack-arm-none-eabi-gcc-9.3.1-1.4

# 配置（使用 armgcc9 工具链）
cmake -B out_ble -GNinja -Dp=demo_ble -Dt=armgcc9

# 编译
ninja -C out_ble

# 产物
ls out_ble/demo_ble.elf  out_ble/demo_ble.bin
```

Demo BLE 源码在 `app/product/demo_ble/`，产品目录结构见 [APP_LAYOUT.md](APP_LAYOUT.md)。该工程面向 Goodix GR5525RGNI，展示 BLE HID 键盘 + UART 透传。

> **为什么需要 GCC 9.3.1？** 当前仓库的 Goodix `libble_sdk.a` 与
> GR5525 ROM symbol 表固定匹配 SDK `v1.0.3_patch_2`。CMake 会校验两者
> SHA-256，并使用 GCC 9.3.1 兼容工具链。构建仍采用严格链接，不允许未解析符号、
> 重复定义或 semihosting 运行库。

## CMake 参数说明

| 参数 | 说明 | 示例 |
|:---|:---|:---|
| `-Dp=<project>` | 项目名（对应 app/ 下的目录） | `-Dp=demo` |
| `-Dt=<toolchain>` | 工具链（armgcc、armgcc9） | `-Dt=armgcc9` |
| `-G<generator>` | 构建系统 | `-GNinja` |
| `-DFIRMWARE_TYPE=<type>` | 固件类型：app/preloader/loader/upgrade | `-DFIRMWARE_TYPE=loader` |
| `-DLOADER_PAYLOAD_FILE=<bin>` | 构建 upgrade 时必须指定同产品已构建 loader BIN | `-DLOADER_PAYLOAD_FILE=out/demo_loader/demo_loader.bin` |

配置输入、DTS/Kconfig/CMake 职责和严格链接规则见
[BUILD_SYSTEM.md](BUILD_SYSTEM.md)。

## 常用命令

```bash
# 查看编译命令
ninja -C out -v

# 只编译指定目标
ninja -C out demo.elf

# 查看设备树预处理结果
ninja -C out show_dts

# 构建并打包 preloader + loader + app
python bootloader/scripts/build_3in1.py demo --out out/demo_3in1

# 检查最终 ELF 不含未解析符号
arm-none-eabi-nm -uC out/demo.elf
```

不要复用不同产品或不同 `FIRMWARE_TYPE` 的同一构建目录；每个产品/固件类型使用
独立目录。Kconfig 输入内容或文件集合改变时会自动重算，无法满足依赖的手写
`CONFIG_...=y` 会在配置阶段失败。

## 烧录

烧录方式取决于目标板和调试器：

烧录、擦除和复位会改变目标板状态，执行前应核对芯片、映像基址和当前调试策略。

```bash
# J-Link
JLinkExe -device GD32F503 -if SWD -speed 4000
> loadfile out/demo.bin 0x08000000
> r
> g

# OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f5x.cfg \
  -c "program out/demo.bin 0x08000000 verify reset exit"

# pyocd
pyocd flash -t gd32f503 out/demo.bin
```

## 调试

```bash
# GDB 远程调试
arm-none-eabi-gdb out/demo.elf
(gdb) target remote :3333
(gdb) load
(gdb) break main
(gdb) continue
```
