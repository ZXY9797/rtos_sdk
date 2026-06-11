# Bootloader 构建与目录约定

本文档描述当前 bootloader 目录职责、产品配置位置、CMake 构建参数、产物命名和三合一固件生成方式。

## 目录职责

`bootloader/loader`、`bootloader/preloader`、`bootloader/upgrade` 只放所有项目共用的源码、Kconfig 和 CMake 入口。

项目相关文件全部放在 `bootloader/product/<product>/` 下，包括：

- `config/board.dts`
- `config/prj.conf`
- `linker.ld`
- flash 分区表实现

当前结构：

```text
bootloader/
  include/boot/                 公共头文件
  loader/                       公共 loader 源码
    protocol/
    src/
    upgrade_logic/
  preloader/                    公共 preloader 源码
    src/
  upgrade/                      公共 loader-upgrade app 源码
    include/upgrade/
    src/
  product/
    demo/
      common/flash_map.cc
      preloader/config/
      preloader/linker.ld
      loader/config/
      loader/linker.ld
      upgrade/config/
      upgrade/linker.ld
    demo_ble/
      common/flash_map.cc
      preloader/config/
      preloader/linker.ld
      loader/config/
      loader/linker.ld
      upgrade/config/
      upgrade/linker.ld
  scripts/
    build_3in1.py
    merge_firmware.py
```

app 的项目 linker 也放在 app product 目录：

```text
app/product/demo/linker.ld
app/product/demo_ble/linker.ld
```

## CMake 构建入口

统一使用产品名 `-Dp=<product>`，用 `-DFIRMWARE_TYPE=<type>` 决定构建哪个固件类型。

支持的固件类型：

```text
app
preloader
loader
upgrade
```

示例：

```powershell
cmake -S . -B out\demo_3in1\preloader -GNinja -Dp=demo -DFIRMWARE_TYPE=preloader
ninja -C out\demo_3in1\preloader

cmake -S . -B out\demo_3in1\loader -GNinja -Dp=demo -DFIRMWARE_TYPE=loader
ninja -C out\demo_3in1\loader

cmake -S . -B out\demo_3in1\upgrade -GNinja -Dp=demo -DFIRMWARE_TYPE=upgrade
ninja -C out\demo_3in1\upgrade

cmake -S . -B out\demo_3in1\app -GNinja -Dp=demo -DFIRMWARE_TYPE=app
ninja -C out\demo_3in1\app
```

`-DFIRMWARE_TYPE` 未传时默认是 `app`。

## 产物命名

boot 固件产物带产品名前缀：

```text
demo_preloader.bin
demo_loader.bin
demo_upgrade.bin

demo_ble_preloader.bin
demo_ble_loader.bin
demo_ble_upgrade.bin
```

app 固件产物保持产品名：

```text
demo.bin
demo_ble.bin
```

对应 `.elf`、`.hex`、`.map` 也使用同样的输出名。

## 三合一固件构建

推荐使用：

```powershell
python bootloader\scripts\build_3in1.py demo --out out\demo_3in1
python bootloader\scripts\build_3in1.py demo_ble --out out\demo_ble_3in1
```

脚本会在同一个 out 根目录下构建全部产物：

```text
out/<product>_3in1/
  preloader/
  loader/
  app/
  <product>_3in1.bin
```

`upgrade` 不参与三合一合并，但可以按相同规则单独构建：

```powershell
cmake -S . -B out\demo_ble_3in1\upgrade -GNinja -Dp=demo_ble -DFIRMWARE_TYPE=upgrade
ninja -C out\demo_ble_3in1\upgrade
```

## 手动合并三合一

也可以直接调用合并脚本：

```powershell
python bootloader\scripts\merge_firmware.py `
  --layout demo `
  --preloader out\demo_3in1\preloader\demo_preloader.bin `
  --loader out\demo_3in1\loader\demo_loader.bin `
  --app out\demo_3in1\app\demo.bin `
  --output out\demo_3in1\demo_3in1.bin
```

```powershell
python bootloader\scripts\merge_firmware.py `
  --layout demo_ble `
  --preloader out\demo_ble_3in1\preloader\demo_ble_preloader.bin `
  --loader out\demo_ble_3in1\loader\demo_ble_loader.bin `
  --app out\demo_ble_3in1\app\demo_ble.bin `
  --output out\demo_ble_3in1\demo_ble_3in1.bin
```

## Flash 布局

### demo, GD32F503

flash 起始地址：`0x08000000`

| 分区 | 偏移 | 绝对地址 | 大小 |
| --- | ---: | ---: | ---: |
| preloader | `0x00000000` | `0x08000000` | 16KB |
| loader | `0x00004000` | `0x08004000` | 24KB |
| app slot0 | `0x0000A000` | `0x0800A000` | 160KB |
| upgrade | `0x00032000` | `0x08032000` | 152KB |
| storage | `0x00058000` | `0x08058000` | 32KB |

### demo_ble, GR5525

flash 起始地址：`0x00200000`

| 分区 | 偏移 | 绝对地址 | 大小 |
| --- | ---: | ---: | ---: |
| reserved | `0x00000000` | `0x00200000` | 8KB |
| preloader | `0x00002000` | `0x00202000` | 16KB |
| loader | `0x00006000` | `0x00206000` | 24KB |
| app slot0 | `0x0000C000` | `0x0020C000` | 160KB |
| upgrade | `0x00034000` | `0x00234000` | 784KB |
| storage | `0x000F8000` | `0x002F8000` | 32KB |

## Flash 驱动对接

bootloader 通过 `boot_common` 的 flash 分区接口访问 flash，产品侧只提供分区布局；具体芯片读写由 `embedded/drivers/flash` 适配。

- GD32 使用 `flash_gd32.cc` 对接 GD32 FMC 擦写接口。
- STM32 使用 `flash_stm32.cc` 对接 STM32 HAL flash 接口。
- Goodix GR5525 使用 `flash_goodix.cc` 直接调用 ROM/SDK 提供的 `hal_exflash_init/read/write/erase` 符号，不在仓库内重复实现 `gr55xx_hal_exflash.*`。

GR5525 的 exflash 符号由 CMake 通过 linker `--defsym` 绑定到 ROM 地址；这些符号只覆盖外部 flash 基础读写擦除。完整 BLE app 链接还依赖 Goodix BLE SDK 里的 ROM symbol 处理和工具链兼容性。

## DFU 通信协议

loader 的升级通信使用实际 Link 帧格式，不发送裸 ACK。应答帧遵循：

```text
CMD_SET, cmd_id, len_lo, len_hi, payload..., crc_hi, crc_lo
```

ACK payload 当前为 1 字节 `status`，CRC 使用 `boot_proto::crc16_ccitt()` 覆盖 `CMD_SET` 到 payload 的全部字节，最后按高字节在前写入。

## demo_ble bin 空洞修复

GR5525 app 链接脚本里把 `FPB` 和 `KE_MSG_TABLE` 等 RAM 区域放在 `NOLOAD` section，避免 `objcopy -Obinary` 把 FLASH 到 RAM 的地址空洞展开成几百 MB 的 `.bin`。

当前 `demo_ble.bin` 验证大小约 91KB：

```text
out\demo_ble_3in1\app\demo_ble.bin  92991 bytes
```

## 已验证命令

```powershell
python -m py_compile bootloader\scripts\merge_firmware.py bootloader\scripts\build_3in1.py
python bootloader\scripts\build_3in1.py demo --out out\demo_3in1
python bootloader\scripts\build_3in1.py demo_ble --out out\demo_ble_3in1
cmake -S . -B out\demo_3in1\upgrade -GNinja -Dp=demo -DFIRMWARE_TYPE=upgrade
ninja -C out\demo_3in1\upgrade
cmake -S . -B out\demo_ble_3in1\upgrade -GNinja -Dp=demo_ble -DFIRMWARE_TYPE=upgrade
ninja -C out\demo_ble_3in1\upgrade
```

验证产物：

```text
out\demo_3in1\preloader\demo_preloader.bin           8919 bytes
out\demo_3in1\loader\demo_loader.bin                16638 bytes
out\demo_3in1\upgrade\demo_upgrade.bin               9079 bytes
out\demo_3in1\app\demo.bin                          78428 bytes
out\demo_3in1\demo_3in1.bin                        204800 bytes

out\demo_ble_3in1\preloader\demo_ble_preloader.bin   8359 bytes
out\demo_ble_3in1\loader\demo_ble_loader.bin        16266 bytes
out\demo_ble_3in1\upgrade\demo_ble_upgrade.bin       8519 bytes
out\demo_ble_3in1\app\demo_ble.bin                  92991 bytes
out\demo_ble_3in1\demo_ble_3in1.bin                212992 bytes
```
