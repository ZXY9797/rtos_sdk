# 快速开始

## 环境要求

```bash
# 基础工具
python3           # 设备树解析 + 驱动特化生成
cmake (>= 3.20)   # 构建系统
ninja             # 构建后端

# ARM 交叉编译器（按项目选择）
# demo (GD32):     arm-gnu-toolchain >= 13.x（支持 -std=c++20）
# demo_ble (GR5525): xPack GCC 9.3.1（兼容 Goodix libble_sdk.a）
```

## 构建 Demo（GD32 FOC 电机控制）

```bash
# 配置（默认 armgcc 工具链）
cmake -B out -GNinja -Dp=demo

# 编译
ninja -C out

# 产物
ls out/demo.elf  out/demo.bin  out/demo.hex
```

Demo 源码在 `app/demo/main.cc`，面向 GD32F503，展示 FOC 电机控制（PWM + ADC + CLI 调试）。

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

Demo BLE 源码在 `app/demo_ble/`，面向 Goodix GR5525RGNI，展示 BLE HID 键盘 + UART 透传。

> **为什么需要 GCC 9.3.1？** Goodix 预编译的 `libble_sdk.a` 使用 GCC 9.3.1 编译，与 GCC 15.x 的 binutils 存在二进制兼容性问题（dangerous relocation）。`-Dt=armgcc9` 会自动启用兼容标记（`-std=c++2a`、`--unresolved-symbols=ignore-in-object-files`）。

## CMake 参数说明

| 参数 | 说明 | 示例 |
|:---|:---|:---|
| `-Dp=<project>` | 项目名（对应 app/ 下的目录） | `-Dp=demo` |
| `-Dt=<toolchain>` | 工具链（armgcc、armgcc9） | `-Dt=armgcc9` |
| `-G<generator>` | 构建系统 | `-GNinja` |

## 常用命令

```bash
# 清理构建
rm -rf out/

# 查看编译命令
ninja -C out -v

# 只编译指定目标
ninja -C out demo.elf

# 查看设备树预处理结果
ninja -C out show_dts
```

## 烧录

烧录方式取决于目标板和调试器：

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
