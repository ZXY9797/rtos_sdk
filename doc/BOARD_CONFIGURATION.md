# 项目硬件、应用与 Boot 镜像配置

硬件连接直接在项目 config/ 中声明，不再单独维护 boards/ 或板卡选择层。
设备启用及使用参数由各镜像 overlay 选择，软件功能由 Kconfig 控制。
App 与 Boot 引用本项目同一份硬件定义，Boot 不继承 App 的设备组合。

## 目录

```text
app/product/gimbal/
├── Kconfig                         # 应用配置项定义
├── linker.ld
├── config/
│   ├── board.dts                   # 本项目物理硬件；外设默认 disabled
│   ├── pinctrl.dtsi                # 本项目引脚状态描述
│   ├── board_defconfig             # App/Boot 共用的最小 SoC/时钟默认值
│   ├── layout.json                 # 全镜像、链接与打包共用的布局
│   ├── prj.conf                    # App 软件配置
│   ├── app.overlay                 # 设备启用、采样/PWM 参数、领域设备组合
│   └── profiles/{debug,release}.conf
└── boot/
    ├── common/                     # 按需提供产品 Flash/安全适配
    ├── preloader/
    │   ├── linker.ld
    │   └── config/
    │       ├── prj.conf
    │       ├── preloader.overlay
    │       └── profiles/{debug,release}.conf
    ├── loader/
    │   ├── linker.ld
    │   └── config/
    │       ├── prj.conf
    │       ├── loader.overlay
    │       └── profiles/{debug,release}.conf
    └── upgrade/
        ├── linker.ld
        └── config/
            ├── prj.conf
            └── upgrade.overlay
```

`bootloader/{preloader,loader,upgrade}` 保留通用实现。产品 `boot/common` 只在有实际 provider
时创建，配置文件不代替硬件适配。当前 upgrade 没有 release profile，生产 Loader
现场升级仍由原有安全门禁拒绝。

新 PCB 或器件连接变化直接修改所属项目的 config/board.dts、pinctrl.dtsi。同一块板的设备启用、
波特率、PWM 周期等放在镜像 overlay；电机/Hall 等应用组合也位于 App overlay。
堆栈、内核、日志和 watchdog 放在 `.conf`；校准值和用户参数保持在产品参数/NVS 中。

## 加载规则

用 `-Dp=<product>` 选择项目，App 与全部 Boot 镜像默认读取
`app/product/<product>/config/board.dts` 和同目录的 board_defconfig。
App 与各 Boot 镜像继续分别选择自己的软件配置、overlay 和 profile。
App 使用项目 `config/`，Boot 使用项目 `boot/<image>/config/`；链接脚本在各镜像目录。
分区布局统一读取项目 `config/layout.json`，CMake、镜像打包与三合一合并共用该文件。
独立应用原有的 `product.cmake` 扩展入口保持兼容，可按需设置应用特有的构建选项。

```text
设备树：
项目 config/board.dts
  -> 当前镜像 config/<FIRMWARE_TYPE>.overlay（存在时）
  -> 显式 DTC_OVERLAY_FILE 列表，按给定顺序追加
  -> layout.json 生成的 boot_partitions.dts

Kconfig：
项目 config/board_defconfig（存在时）
  -> CONF_FILE 列表，未指定时使用当前镜像 config/prj.conf
  -> 当前镜像 config/profiles/<PROFILE>.conf（PROFILE 非 default 时）
  -> 显式 EXTRA_CONF_FILE 列表
```

不扫描并合并所有 `.conf` 或 `.overlay`，profile 不隐式选择硬件 overlay。
缺失的显式输入和非 default profile 会使配置失败。增加或删除默认 overlay 会触发重新配置。
显式片段相对路径优先相对 SDK 根目录解析，其次相对当前镜像 config；独立应用可使用绝对路径。

`CONFIG_DIR` 同时替换基础配置、默认 overlay 和 profile 的查找目录。仅替换软件配置目录时，
仍使用项目硬件；该目录提供完整 board.dts 时则使用它及同目录的 board_defconfig。
独立 APPLICATION 的 config/ 也可提供自己的硬件描述；未提供时复用所选项目的硬件。

## 使用

`sdk.py configure` 每次根据本次参数选择产品、应用、镜像配置和 overlay。相对构建路径相对于调用目录。
省略 `--application` 时使用产品应用，省略 `--backend` 时使用镜像自身的 Kconfig 内核选择。
显式选择内核时，普通功能片段与内核可以组合，例如
`python tools/sdk.py configure -p demo_foc -B out/foc-motion --backend rt-thread --cmake-arg=-DEXTRA_CONF_FILE=tests/config/demo_foc_motion.conf`。

```powershell
python tools/sdk.py configure -p gimbal -B out/gimbal-release --profile release
python tools/sdk.py build -B out/gimbal-release

# 在默认 App overlay 后追加仓库根目录的 test.overlay
cmake -S . -B out/gimbal-test -G Ninja -Dp=gimbal `
  '-DDTC_OVERLAY_FILE=test.overlay'

# Boot profile 来自本产品、本镜像的 config/profiles
cmake -S . -B out/gimbal-loader -G Ninja -Dp=gimbal `
  -DFIRMWARE_TYPE=loader -DPROFILE=debug
cmake --build out/gimbal-loader --parallel 8
```

生产三合一命令对 preloader、loader、App 分别选择 release profile，检查最终生产配置，
并验证三份元数据的产品、镜像类型、硬件标识和 canonical layout 一致。工厂构建会清理缓存中的
App、旧 BOARD、配置片段及模块覆盖。生产 Boot 缺失真实安全 provider 时会拒绝构建。

`build-info.json` 记录实际加载的配置片段及设备树入口顺序和摘要；为兼容既有格式，
board 字段使用项目名称，不再对应独立板卡选择。最终硬件配置
查看 `firmware.dts`，最终软件配置查看 `.config`。生成后的 Flash 分区也会与 layout.json
核对，overlay 不能额外加入与该产品布局不一致的分区。

## 迁移说明

- App 根目录 `release.conf` 移到 `config/profiles/release.conf`，调用方改用 `PROFILE=release`。
- 原 `boards/<board>/` 的硬件文件迁回所属项目的 config/，App/Boot 直接共用。
- demo_foc 与 gimbal 分别维护自己的完整硬件和引脚描述，不引用另一项目或独立公共板卡目录。
- 删除仅映射默认板卡的 `products/<product>/product.cmake`，构建只需选择产品。
- 原 BOARD/BOARD_ROOT 缓存用 `cmake -UBOARD -UBOARD_ROOT` 清除；Python configure 入口自动清理。
- 保留原有 App/Loader pinmux、DMA 描述和各镜像时钟差异；这不代表已完成硬件正确性确认。
- OSAL 示例仅启用串口及其 DMA，移除了原来随 demo_foc 板描述继承的电机、IMU、CAN 等设备。
- 各产品 Boot 文件迁入项目 `boot/`，共享 `layout.json` 迁入项目 `config/`，内容保持原样。
- 原 `bootloader/product/` 不再保留；产品专用 Flash、安全、DFU 和 watchdog provider 按需放入 `boot/common/`。
- debug profile 启用架构断言；断言 hook 统一按 C ABI 导出，App 和 Boot 共用故障停机路径。

该迁移验证源码、生成配置及构建产物，不代替目标板运行、引脚电气和 Boot 断电验收。
