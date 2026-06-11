# Boot 启动与升级关系

本文档描述当前代码中 `preloader`、`loader`、`upgrade`、`app` 四类固件的职责边界和运行关系。

说明以当前实现为准，不把后续计划当成现状。

## 总体职责

| 固件 | 主要职责 | 不负责的事情 |
| --- | --- | --- |
| `preloader` | 第一阶段启动选择，只读 `boot_ctrl` 并跳转到一个目标分区 | 不做 flash 拷贝、不跑 DFU、不升级 loader、不做镜像 SHA 校验 |
| `loader` | 第二阶段启动与 DFU，校验 app，接收升级包，必要时处理 app AB 拷贝 | 不执行 loader 自升级写入 |
| `upgrade` | loader 自升级执行固件，处理 `loader_upgrade` 路径 | 不跑通用 DFU 协议，不负责正常 app 启动 |
| `app` | 产品业务固件，发布产品元数据，必要时调用公开 boot API 设置启动标记 | 不直接编译 bootloader 私有源码 |

相关源码：

- `bootloader/preloader/src/preloader_main.cc`
- `bootloader/loader/src/boot_logic.cc`
- `bootloader/loader/protocol/protocol.cc`
- `bootloader/loader/upgrade_logic/upgrade_state.cc`
- `bootloader/loader/upgrade_logic/flash_copy.cc`
- `bootloader/upgrade/src/upgrade_main.cc`
- `bootloader/common/src/boot_ctrl.cc`

## preloader

`preloader` 是最小第一阶段。它当前只做三件事：

1. 从 `FLASH_AREA_STORAGE` 读取 `boot_ctrl`。
2. 根据标记选择一个目标分区。
3. 设置 MSP/VTOR 并跳转。

当前跳转表：

| `boot_ctrl` | 跳转目标 |
| --- | --- |
| `BOOT_CTRL_UPGRADE_LOADER` | `FLASH_AREA_UPGRADE` |
| `BOOT_CTRL_ENTER_DFU` | `FLASH_AREA_BOOTLOADER` |
| `BOOT_CTRL_UPGRADE_APP` | `FLASH_AREA_BOOTLOADER` |
| `BOOT_CTRL_NORMAL` | `FLASH_AREA_SLOT0` |
| 其他值 | `FLASH_AREA_BOOTLOADER` |

preloader 不再包含 `upgrade_copy` 逻辑。`FLASH_AREA_UPGRADE` 到 `FLASH_AREA_SLOT0` 的拷贝不属于 preloader。

## loader

`loader` 是第二阶段启动与 DFU 固件。它负责：

- 读取 `boot_ctrl` 并处理进入 DFU 的请求。
- 校验 `FLASH_AREA_SLOT0` 中的 app。
- 在 AB 模式下，必要时校验 `FLASH_AREA_UPGRADE` 并拷贝到 `FLASH_AREA_SLOT0`。
- 运行 DFU 协议，接收、写入、校验升级数据。
- 跳转到有效 app。

当前启动决策：

1. 读取 `boot_ctrl`。
2. `BOOT_CTRL_ENTER_DFU`：清除 `boot_ctrl`，进入 DFU。
3. `BOOT_CTRL_UPGRADE_LOADER`：清除 `boot_ctrl` 和 `loader_upgrade`，进入 DFU。这是兜底路径；正常情况下 preloader 应先跳到 `upgrade`。
4. 校验 `FLASH_AREA_SLOT0`，有效则跳 app。
5. `CONFIG_BOOT_MODE_AB` 下，校验 `FLASH_AREA_UPGRADE`，有效则拷贝到 `FLASH_AREA_SLOT0`，再校验 slot0 并跳 app。
6. 无有效 app 时进入 DFU。

DFU 写入目标：

| 模式 | 写入目标 |
| --- | --- |
| `CONFIG_BOOT_MODE_AB` | `FLASH_AREA_UPGRADE` |
| direct 模式 | `FLASH_AREA_SLOT0` |

当前需要注意：`BOOT_CTRL_UPGRADE_APP` 目前只让 preloader 跳到 loader，loader 内部还没有专门消费该标记的强制 app 升级分支。因此在 AB 模式下，如果 `FLASH_AREA_SLOT0` 仍然有效，loader 会先跳 slot0，不会强制把 `FLASH_AREA_UPGRADE` 覆盖到 slot0。

## upgrade

`upgrade` 是 loader 自升级执行固件，链接到 `FLASH_AREA_UPGRADE`。当 `boot_ctrl == BOOT_CTRL_UPGRADE_LOADER` 时，preloader 会跳到该分区运行它。

当前职责：

1. 读取 `loader_upgrade` 标记。
2. 如果 `loader_upgrade` 已设置，将 `FLASH_AREA_UPGRADE` 中的镜像按镜像头长度拷贝到 `FLASH_AREA_SLOT0`。
3. 校验内嵌的新 loader payload。
4. 擦除 `FLASH_AREA_BOOTLOADER`。
5. 写入新的 loader payload。
6. 清除 `loader_upgrade`。
7. 系统复位。

也就是说，`loader_upgrade` 标记下的 `FLASH_AREA_UPGRADE -> FLASH_AREA_SLOT0` 拷贝逻辑属于 `upgrade`，不属于 `preloader`。

## app

`app` 是正常产品固件，位于 `FLASH_AREA_SLOT0`。

app 侧当前职责：

- 发布 `boot::ProductInfo` 到 `.product_info` 段。
- 成功启动后可调用 `boot::confirm_image()`。
- 可通过公开 boot API 设置启动控制标记，例如 `boot_ctrl_write(...)` 或 `loader_upgrade_write(...)`。

`loader_upgrade_write(nonzero)` 会同时把 `boot_ctrl` 设置为 `BOOT_CTRL_UPGRADE_LOADER`。

## boot_ctrl 与 loader_upgrade

boot 控制记录位于 `FLASH_AREA_STORAGE`，当前由 `bootloader/common/src/boot_ctrl.cc` 管理。

记录中主要字段：

- `boot_ctrl`
- `loader_upgrade`
- `copy_progress`
- `crc32`

标记含义：

| 标记 | 含义 |
| --- | --- |
| `BOOT_CTRL_NORMAL` | 正常启动 |
| `BOOT_CTRL_ENTER_DFU` | 进入 loader DFU |
| `BOOT_CTRL_UPGRADE_APP` | 让 preloader 进入 loader，预期用于 app 升级处理 |
| `BOOT_CTRL_UPGRADE_LOADER` | 让 preloader 进入 upgrade，执行 loader 自升级 |

`loader_upgrade_clear()` 会清除 `loader_upgrade`；如果 `boot_ctrl` 当前是 `BOOT_CTRL_UPGRADE_LOADER`，也会把 `boot_ctrl` 恢复为 `BOOT_CTRL_NORMAL`。

## 主要流程

### 正常启动

```text
reset
  -> preloader
  -> boot_ctrl == BOOT_CTRL_NORMAL
  -> jump FLASH_AREA_SLOT0
  -> app
```

### 进入 DFU

```text
reset
  -> preloader
  -> boot_ctrl == BOOT_CTRL_ENTER_DFU
  -> jump FLASH_AREA_BOOTLOADER
  -> loader 清除 boot_ctrl
  -> loader DFU loop
```

### app AB 升级

```text
loader DFU
  -> 擦除 / 写入 / 校验 FLASH_AREA_UPGRADE
  -> reboot
  -> preloader 按 boot_ctrl 选择目标
  -> loader 在当前 fallback 路径下可能拷贝 FLASH_AREA_UPGRADE 到 FLASH_AREA_SLOT0
  -> loader 从 FLASH_AREA_SLOT0 跳 app
```

当前限制：`BOOT_CTRL_UPGRADE_APP` 还没有在 loader 中实现“无论 slot0 是否有效都强制拷贝”的逻辑。

### loader 自升级

```text
请求 loader 升级
  -> loader_upgrade_write(nonzero)
  -> boot_ctrl = BOOT_CTRL_UPGRADE_LOADER
  -> reset
  -> preloader 跳 FLASH_AREA_UPGRADE
  -> upgrade 运行
  -> loader_upgrade 已设置时：
       拷贝 FLASH_AREA_UPGRADE 到 FLASH_AREA_SLOT0
  -> 校验内嵌 loader payload
  -> 擦除 / 写入 FLASH_AREA_BOOTLOADER
  -> 清除 loader_upgrade
  -> reset
```

如果没有进入 `upgrade`，loader 中对 `BOOT_CTRL_UPGRADE_LOADER` 有兜底处理：清除标记并进入 DFU。

## Flash 分区

### demo

Flash base：`0x08000000`

| 分区 | 偏移 | 绝对地址 | 大小 |
| --- | ---: | ---: | ---: |
| `FLASH_AREA_PRELOADER` | `0x00000000` | `0x08000000` | 16 KB |
| `FLASH_AREA_BOOTLOADER` | `0x00004000` | `0x08004000` | 24 KB |
| `FLASH_AREA_SLOT0` | `0x0000A000` | `0x0800A000` | 160 KB |
| `FLASH_AREA_UPGRADE` | `0x00032000` | `0x08032000` | 152 KB |
| `FLASH_AREA_STORAGE` | `0x00058000` | `0x08058000` | 32 KB |

### demo_ble

Flash base：`0x00200000`

| 分区 | 偏移 | 绝对地址 | 大小 |
| --- | ---: | ---: | ---: |
| `FLASH_AREA_PRELOADER` | `0x00002000` | `0x00202000` | 16 KB |
| `FLASH_AREA_BOOTLOADER` | `0x00006000` | `0x00206000` | 24 KB |
| `FLASH_AREA_SLOT0` | `0x0000C000` | `0x0020C000` | 160 KB |
| `FLASH_AREA_UPGRADE` | `0x00034000` | `0x00234000` | 784 KB |
| `FLASH_AREA_STORAGE` | `0x000F8000` | `0x002F8000` | 32 KB |

## 维护规则

- preloader 只保留跳转选择逻辑。
- loader 保留 DFU、app 校验、app 启动和 app AB 拷贝逻辑。
- upgrade 保留 loader 自升级执行逻辑。
- app 只使用公开 boot API，不直接编译 bootloader 私有源码。
- boot 控制记录统一由 `bootloader/common` 管理。
