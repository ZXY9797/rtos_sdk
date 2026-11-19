# 快速开始

## 环境要求

```bash
# 基础工具
python3           # 设备树解析 + 驱动特化生成
cmake (>= 3.20)   # 构建系统
ninja             # 构建后端

# ARM 交叉编译器（按项目选择）
# 默认/GR5525: xPack GCC 9.3.1（工程对该版本使用 -std=c++2a）
# demo_foc (GD32): 也可显式 -Dt=armgcc 使用现代 arm-gnu-toolchain
```

## 构建 Demo（GD32 FOC 电机控制）

```bash
python -m pip install -r tools/requirements-ci.txt
python tools/sdk.py configure -p demo_foc -B out/demo_foc
python tools/sdk.py build -B out/demo_foc
python tools/sdk.py inspect -B out/demo_foc --kind resources
```

Demo 源码在 `app/product/demo_foc/main.cc`，产品目录结构见 [APP_LAYOUT.md](APP_LAYOUT.md)。该工程面向 GD32F503，展示 FOC 电机控制（PWM + ADC + CLI 调试）。

## 构建 Demo BLE（GR5525 HID 键盘 + UART 透传）

```bash
# 设置 GCC 9.3.1 工具链路径（Goodix libble_sdk.a 要求）
export ARMGCC9_TOOLCHAIN_PATH=/path/to/xpack-arm-none-eabi-gcc-9.3.1-1.4

# 配置（使用 armgcc9 工具链）
python tools/sdk.py configure -p demo_ble -B out/demo_ble

# 编译
python tools/sdk.py build -B out/demo_ble

# 产物
ls out/demo_ble/demo_ble.elf out/demo_ble/demo_ble.bin
```

Demo BLE 源码在 `app/product/demo_ble/`，产品目录结构见 [APP_LAYOUT.md](APP_LAYOUT.md)。该工程面向 Goodix GR5525RGNI，展示 BLE HID 键盘 + UART 透传。

> **为什么需要 GCC 9.3.1？** 当前仓库的 Goodix `libble_sdk.a` 与
> GR5525 ROM symbol 表固定匹配 SDK `v1.0.3_patch_2`。CMake 会校验两者
> SHA-256，并使用 GCC 9.3.1 兼容工具链。构建仍采用严格链接，不允许未解析符号、
> 重复定义或 semihosting 运行库。

## CMake 参数说明

| 参数 | 说明 | 示例 |
|:---|:---|:---|
| `-Dp=<project>` | 项目名（对应 app/ 下的目录） | `-Dp=demo_foc` |
| `-Dt=<toolchain>` | 工具链（armgcc、armgcc9） | `-Dt=armgcc9` |
| `-G<generator>` | 构建系统 | `-GNinja` |
| `-DFIRMWARE_TYPE=<type>` | 固件类型：app/preloader/loader/upgrade | `-DFIRMWARE_TYPE=loader` |
| `-DLOADER_PAYLOAD_FILE=<bin>` | 构建 upgrade 时必须指定同产品已构建 loader BIN | `-DLOADER_PAYLOAD_FILE=out/demo_loader/demo_loader.bin` |

配置输入、DTS/Kconfig/CMake 职责和严格链接规则见
[BUILD_SYSTEM.md](BUILD_SYSTEM.md)。

## 常用命令

```bash
# 查看编译命令
ninja -C out/demo_foc -v

# 只编译指定目标
ninja -C out/demo_foc demo_foc.elf

# 查看设备树生成的设备依赖
python tools/sdk.py inspect -B out/demo_foc --kind devices

# 构建并打包 preloader + loader + app
python bootloader/scripts/build_3in1.py demo_foc --out out/demo_3in1

# 检查最终 ELF 不含未解析符号
arm-none-eabi-nm -uC out/demo_foc/demo_foc.elf
```

每个产品或镜像使用独立构建目录即可同时保留各自产物。Kconfig 输入内容或文件集合改变时会自动重算，无法满足依赖的手写
`CONFIG_...=y` 会在配置阶段失败。

## 烧录

demo_foc 的工厂镜像 `out/demo_3in1/demo_foc_3in1.bin` 包含 preloader、loader 和已打包的 App，
其烧录基址来自 [layout.json](../app/product/demo_foc/config/layout.json) 的 `flash_base`。
当前 GD32F503 布局的工厂镜像基址为 `0x08000000`，App 镜像分区起点为 `0x0800A000`。
选择与目标匹配的工厂镜像和调试器后，用调试器的写入及回读校验功能完成烧录。

## 调试

```bash
# 连接已有调试会话，加载与已烧录 App 对应的符号
arm-none-eabi-gdb out/demo_foc/demo_foc.elf
(gdb) target remote :3333
(gdb) break main
(gdb) continue
```
