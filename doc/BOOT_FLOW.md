# Boot 启动与升级流程

## 固件职责

| 固件 | 职责 |
| --- | --- |
| preloader | 最小信任根；生产模式认证固定 loader 后跳转 |
| loader | 校验/安装/启动应用，提供恢复 DFU 和防降级策略 |
| upgrade | 开发/工厂场景的 loader 自升级执行固件 |
| app | 产品业务；健康启动后确认 pending 镜像 |

Flash 分区由 `bootloader/product/<product>/layout.json` 唯一定义。启动控制记录位于
独立 `boot_ctrl` 分区，采用双 sector 追加日志、CRC、递增 sequence 和最后写 magic
的提交顺序。回收目标 sector 前，另一个 sector 仍保留最近一次有效记录。

## 正常启动

```text
reset
  -> preloader
       production: verify_loader_image(loader)
       development: validate loader vectors
  -> loader
       read boot_ctrl
       check_image(slot0)
       commit security_version if image is confirmed
       quiesce transport/watchdog
       sanitize Cortex-M handoff state
  -> app Reset_Handler
```

镜像、控制状态、版本提交或 handoff 前置检查任一失败时，loader 进入恢复 DFU，不带病
跳转。首次没有 boot_ctrl 日志时使用 NORMAL 默认状态；损坏记录不会被当作升级命令。

## DFU 接收状态机

```text
IDLE
  -> ENTER_LOADER: sector-by-sector erase target
  -> ERASED
  -> FW_TRANSFER: only exact next offset or identical retry
  -> RECEIVING
  -> FW_VERIFY: exact image length + whole-file hash + check_image
  -> RECEIVED
  -> REBOOT
```

收到完整 header 后锁定 `hdr_size + img_size`，后续数据不能越界或带尾随字节。重传只
接受已经写入且 Flash 回读完全一致的数据。所有长 Flash/hash 循环都会周期服务
watchdog；单次 sector 操作和产品签名校验必须小于 watchdog 窗口。

## Sector-swap 安装、试运行与掉电恢复

默认模式使用一个执行槽、一个覆盖可安装镜像上限的交换区和一个擦除块大小的 scratch：

```text
DFU writes upgrade
  -> validate upgrade image
  -> boot_ctrl = UPGRADE_APP, progress = 0
  -> reset
  -> validate upgrade again
  -> for each sector:
       slot0 -> scratch      (SAVE_OLD)
       upgrade -> slot0     (INSTALL_NEW)
       scratch -> upgrade   (STORE_OLD)
       persist phase/progress
  -> validate slot0 again
  -> boot_ctrl = TRIAL_APP
  -> handoff to slot0
  -> next reset:
       confirmed: commit security version, invalidate old image, boot new
       unconfirmed: boot_ctrl = ROLLBACK_APP, reverse the same swap, boot old
```

每一步都执行整 sector 擦除、复制和回读，只有完成后才推进双 sector boot journal 中的
phase/progress。任一步断电都会幂等重做当前阶段；`ROLLBACK_APP` 本身也先持久化，因此
回滚过程再次断电仍可继续。交换长度取新旧镜像 extent 的较大值并向 sector 对齐，确保
旧镜像比新镜像更长时仍完整保留。

这仍不是双执行槽 A/B：只有 `slot0` 可执行，`upgrade` 保存交换后的旧镜像。当前策略给
新应用一次启动机会；应用必须在健康检查完成后确认，下一次复位仍未确认就回滚。需要
多次试运行、两个版本任选启动或无复制切换时，仍应增加第二个可执行槽。

## Pending、Confirmed 和防降级

镜像状态位只执行 Flash 1->0 转换：

```text
PENDING -> CONFIRMED -> NON_BOOTABLE
```

pending 镜像可启动，但不会立即推进 monotonic security version。应用完成硬件和业务
健康检查后调用 `boot::confirm_image()`。下一次启动 loader 看到 confirmed 镜像，才把
security version 提交到 OTP 或等价受保护存储。提交失败时不跳转。

生产 provider 的最低版本读取失败必须返回拒绝值，提交必须幂等、只增且可验证。镜像
签名绑定 canonical header；header 中包含 payload SHA-256，因此签名间接绑定完整
payload。可变状态位、签名字段和 header CRC 在认证摘要中按协议规范化。

## Loader 自升级边界

开发用 upgrade 固件包含 loader payload 的长度和 SHA-256 manifest，写前后都校验。
生产 preloader 不进入这条未签名的开发路径。若产品需要现场更新 loader，必须另行定义：

- 签名的 upgrade 容器和 loader manifest；
- loader 自身 monotonic version；
- 固定信任根和硬件写保护切换策略；
- 写 loader 分区时的掉电恢复副本或 ROM 恢复通道。

在这些机制完成前，打开开发自升级路径不等于产品级 loader 更新。

## 板级验收矩阵

至少覆盖：正常启动、空片、header/payload/signature/ProductInfo 篡改、低版本镜像、
boot_ctrl 两阶段提交断电、每个 swap phase/sector 随机断电、回滚再次断电、Flash
读写失败、重复 DFU 包、越界/乱序包、UART 丢包、watchdog 窗口、pending 未确认、OTP
提交失败、handoff 后首个中断、MSP watermark 和连续多次升级。
