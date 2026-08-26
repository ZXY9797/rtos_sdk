# 构建系统与配置闭环

## 输入和生成顺序

根入口由 `-Dp=<product>` 和 `-DFIRMWARE_TYPE=<type>` 选择产品与固件类型。支持
`app`、`preloader`、`loader`、`upgrade`。

```text
product layout.json
  -> validated CMake layout values
  -> boot_layout.h
  -> boot_flash_map.cc
  -> boot partition DTS overlay

board.dts + bindings
  -> EDT
  -> devicetree_generated.h
  -> generated device/pinctrl sources
  -> Kconfig.dts

root/product Kconfig + prj.conf + optional EXTRA_CONF_FILE
  -> .config
  -> autoconf.h
  -> target-scoped sources/options/includes
  -> ELF -> BIN/HEX -> package
```

相对路径形式的 `EXTRA_CONF_FILE` 优先按仓库根目录解析；若该文件不存在，再按当前产品
配置目录解析，以兼容历史命令。CI 应优先使用仓库根目录相对路径，避免命令含义随产品
切换而变化。

生成器在配置阶段检查 Flash/RAM 范围、分区连续且不重叠、erase/write 对齐、boot_ctrl
至少两个 erase block、scratch 恰好一个 erase block、镜像 header 空间和整数溢出。
CMake、linker、固件常量、DTS overlay 和打包脚本都消费同一份 layout，产品目录不再
维护手写 flash-map 副本。可交换应用上限统一取 `min(slot0, upgrade)`，链接断言和工厂
打包都使用该上限，避免生成“能写入 slot0 但无法保留旧镜像”的包。

## 目标与依赖规则

- 驱动 backend 只有在 Kconfig 和对应 DTS compatible 同时满足时才编译；
- 硬件地址、IRQ、DMA 和 pinctrl 来自 DTS，不写在 CMake；
- 编译选项和生成 include 通过 `sdk_build_config` target 传播；
- vendor include 仅在 vendor target 内作为 system include；
- 最终 executable 直接包含 arch/fault object，避免启动符号受 archive 提取顺序影响；
- 不允许 unresolved-symbol、multiple-definition 或 semihosting 掩盖；
- Goodix ROM symbol 文件固定版本并校验 SHA-256。

## Loader 专用构建策略

loader 的 `app` target 是 object library。这样生成设备、transport 和只含 initcall 的
文件不会因静态 archive 没有普通引用而被丢弃。链接脚本保留：

```text
.preinit_array
.init_array
.initcall_EARLY
.initcall_PRE_KERNEL_1..3
.initcall_POST_KERNEL
.initcall_APPLICATION
```

loader 构建还强制：

- baremetal OSAL；
- 0-byte baremetal heap；
- `-Os`、`-nostartfiles`、无异常/RTTI/thread-safe statics；
- 物理 24 KiB loader Flash region；
- 可配置 `BOOT_STACK_SIZE`，默认 2 KiB；
- loader 自有函数 `-Wstack-usage=768`；
- objcopy 后按实际 BIN 再检查一次分区容量；
- 生产模式禁止 direct overwrite，并要求 security/watchdog provider。
- sector-swap 要求独立 scratch，并在 boot journal 持久化 SAVE/INSTALL/STORE phase。

注意：链接器的 FLASH used size、压缩工具的 section size 和最终 BIN size含义不同。
发布容量以最终 BIN 对物理分区的检查为准，map 用于确认符号和 section 是否真实进入
镜像。

## 构建命令

单独构建 loader：

```powershell
cmake -S . -B out\demo_loader -GNinja `
  -Dp=demo -DFIRMWARE_TYPE=loader
cmake --build out\demo_loader --parallel

cmake -S . -B out\demo_ble_loader -GNinja `
  -Dp=demo_ble -DFIRMWARE_TYPE=loader
cmake --build out\demo_ble_loader --parallel
```

完整三合一：

```powershell
python bootloader\scripts\build_3in1.py demo `
  --out out\demo_3in1
python bootloader\scripts\build_3in1.py demo_ble `
  --out out\demo_ble_3in1
```

量产打包需要非零 security version、64-byte raw `r || s` 签名和对应 64-byte raw
`x || y` 公钥。脚本会离线验签后再打包，但目标产品还必须有与该公钥一致的受保护
provider：

```powershell
python bootloader\scripts\build_3in1.py demo `
  --out out\demo_production `
  --production `
  --security-version 12 `
  --signature keys\app.sig `
  --public-key keys\app-p256.pub
```

上述三合一/工厂包默认生成 `CONFIRMED` slot0 镜像。在线升级包必须额外传入
`--pending`；staged loader 不接受 upgrade 分区中的预确认镜像。

## 自动验证

```powershell
python -m unittest discover -s bootloader\scripts\tests `
  -p "test_*.py" -v
python tools\scripts\tests\test_device_codegen.py -v
cmake -S tests -B out\host_tests -GNinja
cmake --build out\host_tests --parallel
ctest --test-dir out\host_tests --output-on-failure
```

应用固件链接后会自动运行 `tools/scripts/check_app_elf.py`，检查 C++ 构造数组、
initcall、持久化 fault section、fatal 入口和未解析符号。这个检查是构建目标的一部分，
不能通过跳过单独的 CI job 绕开。

CI 同时构建两个产品的 FreeRTOS app、`demo_ble` 最小服务/production 配置、demo
RT-Thread app，以及两个产品的
preloader/loader/upgrade 矩阵；upgrade 构建显式嵌入同一轮刚生成的 loader BIN，禁止
用占位 payload 或跨产品产物。CI 还会验证缺少 app watchdog、Link security 和 boot
security/watchdog provider 时配置按预期失败。RT-Thread 参考配置位于
`tests/config/rtthread.conf`，用于暴露公共头文件泄漏内核/SoC 私有依赖、后端 API
语义漂移和配置符号不闭环等问题。

本地验证 `demo_ble` 量产配置时使用与厂商库匹配的 GCC 9.3.1：

```powershell
$env:ARMGCC9_TOOLCHAIN_PATH='C:/arm-none-eabi-gcc-9.3.1-1.4'
cmake -S . -B out/demo_ble_production -GNinja -Dp=demo_ble -Df=app `
  '-DEXTRA_CONF_FILE=app/product/demo_ble/release.conf'
cmake --build out/demo_ble_production --parallel
```

发布前还应对每个最终 ELF 执行：

```powershell
arm-none-eabi-nm -uC <image.elf>
```

输出必须为空。loader map 还必须能找到生成 UART 对象、实际 IRQ handler、
`soc_early_init_hook`、`handoff_to_image`，以及 UART/transport 两个 POST_KERNEL
initcall。

生产安全 provider 缺失时配置失败是预期行为，不得用弱默认实现绕过。构建、主机掉电
模型和打包通过也不能代替真实目标板上的 Flash、时钟、IRQ、watchdog、断电、栈水位和
handoff 验收。
