# 板卡、应用与 Boot 镜像配置

硬件连接在板卡层声明，设备启用及使用参数由各镜像选择，软件功能由 Kconfig 控制。
App 与 Boot 都引用公共硬件定义，Boot 不继承 App 的设备组合。

## 目录

```text
products/gimbal/
└── product.cmake                   # App/Boot 共用的产品元数据与默认 BOARD

boards/
├── common/                         # 参考板共用的硬件与 pinctrl 描述
└── gd32_gimbal/
    ├── board.dts                   # 物理硬件；外设默认 disabled
    ├── pinctrl.dtsi                # 本板引脚状态描述
    └── board_defconfig             # 最小 SoC/时钟默认值

app/product/gimbal/
├── Kconfig                         # 应用配置项定义
├── linker.ld
└── config/
    ├── prj.conf                    # App 软件配置
    ├── app.overlay                 # 设备启用、采样/PWM 参数、领域设备组合
    └── profiles/{debug,release}.conf

bootloader/product/gimbal/
├── layout.json                     # 全镜像、链接与打包共用的布局
├── common/                         # 按需提供产品 Flash/安全适配
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

`bootloader/{preloader,loader,upgrade}` 保留通用实现。产品 `common` 只在有实际 provider
时创建，配置文件不代替硬件适配。当前 upgrade 没有 release profile，生产 Loader
现场升级仍由原有安全门禁拒绝。

新 PCB 或器件连接变化放在独立板卡或明确的板卡 revision 中。同一块板的设备启用、
波特率、PWM 周期等放在镜像 overlay；电机/Hall 等应用组合也位于 App overlay。
堆栈、内核、日志和 watchdog 放在 `.conf`；校准值和用户参数保持在产品参数/NVS 中。

## 加载规则

所有镜像先加载 `products/<product>/product.cmake`，内置产品的默认 BOARD 只在这里定义。
显式 BOARD 优先；App 与各 Boot 镜像继续分别选择自己的配置、overlay 和 profile。
独立应用原有的 `product.cmake` 扩展入口保持兼容，可按需设置应用特有的构建选项。

```text
设备树：
BOARD/board.dts
  -> 当前镜像 config/<FIRMWARE_TYPE>.overlay（存在时）
  -> 显式 DTC_OVERLAY_FILE 列表，按给定顺序追加
  -> layout.json 生成的 boot_partitions.dts

Kconfig：
BOARD/board_defconfig（存在时）
  -> CONF_FILE 列表，未指定时使用当前镜像 config/prj.conf
  -> 当前镜像 config/profiles/<PROFILE>.conf（PROFILE 非 default 时）
  -> 显式 EXTRA_CONF_FILE 列表
```

不扫描并合并所有 `.conf` 或 `.overlay`，profile 不隐式选择硬件 overlay。
缺失的显式输入和非 default profile 会使配置失败。增加或删除默认 overlay 会触发重新配置。
显式片段相对路径优先相对 SDK 根目录解析，其次相对当前镜像 config；独立应用可使用绝对路径。

`CONFIG_DIR` 同时替换基础配置、默认 overlay 和 profile 的查找目录。仅替换配置目录仍使用
产品默认 BOARD，显式 BOARD 优先。为兼容外部旧工程，未指定 BOARD 且显式 CONFIG_DIR 内
提供完整 board.dts 时，仍可使用该完整 DTS；内置产品已迁移为公共 board 与镜像 overlay。

## 使用

```powershell
python tools/sdk.py configure -p gimbal -B out/gimbal-release --profile release
python tools/sdk.py build -B out/gimbal-release

# 在默认 App overlay 后追加测试配置；分号列表必须整体引用
cmake -S . -B out/gimbal-test -G Ninja -Dp=gimbal `
  '-DDTC_OVERLAY_FILE=D:/my-product/test.overlay'

# Boot profile 来自本产品、本镜像的 config/profiles
cmake -S . -B out/gimbal-loader -G Ninja -Dp=gimbal `
  -DFIRMWARE_TYPE=loader -DPROFILE=debug
cmake --build out/gimbal-loader --parallel 8
```

生产三合一命令对 preloader、loader、App 分别选择 release profile，检查最终生产配置，
并验证三份元数据的产品、镜像类型、板卡和 canonical layout 一致。工厂构建会清理缓存中的
App/BOARD、配置片段及模块覆盖。生产 Boot 缺失真实安全 provider 时会拒绝构建。

`build-info.json` 记录 BOARD、实际加载的配置片段及设备树入口顺序和摘要。最终硬件配置
查看 `firmware.dts`，最终软件配置查看 `.config`。生成后的 Flash 分区也会与 layout.json
核对，overlay 不能额外加入与该产品布局不一致的分区。

## 迁移说明

- App 根目录 `release.conf` 移到 `config/profiles/release.conf`，调用方改用 `PROFILE=release`。
- 根 `boards` 下的 Boot DTS 移为产品镜像 overlay；旧产品 `config/board.dts` 不再保留副本。
- GD32 demo 与 gimbal 通过 `boards/common` 复用硬件基础，gimbal 不再继承 demo 的完整 DTS。
- 保留原有 App/Loader pinmux、DMA 描述和各镜像时钟差异；这不代表已完成硬件正确性确认。
- OSAL 示例仅启用串口及其 DMA，移除了原来随 demo 板描述继承的电机、IMU、CAN 等设备。
- 三个产品的 `layout.json`、链接脚本和产品标识保持原样。
- debug profile 启用架构断言；断言 hook 统一按 C ABI 导出，App 和 Boot 共用故障停机路径。

该迁移验证源码、生成配置及构建产物，不代替目标板运行、引脚电气和 Boot 断电验收。
