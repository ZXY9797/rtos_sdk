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

## Staged-copy 安装与掉电恢复

默认模式使用一个执行槽和一个暂存区：

```text
DFU writes upgrade
  -> validate upgrade image
  -> boot_ctrl = UPGRADE_APP, progress = 0
  -> reset
  -> validate upgrade again
  -> erase/copy/readback slot0 sector by sector
  -> persist checkpoint every 32 KiB and at image end
  -> validate slot0 again
  -> commit confirmed security version
  -> clear boot_ctrl
  -> mark staging non-bootable
  -> handoff to slot0
```

断电后 loader 读取 checkpoint，并比较该位置之前的 source/destination。前缀一致时从
checkpoint 继续；不一致或记录非法时从 0 重新复制。最后一个不足 sector 的镜像块也
会先擦除完整 sector，再只写有效数据并回读有效范围。

这不是 A/B 回滚：安装时旧 slot0 被覆盖。staging 只保存新镜像，不能在新应用启动失败
后恢复旧版本。如果产品要求启动次数、健康判定和自动回滚，必须增加第二个可执行槽和
独立的 trial/confirmed 状态机，不能复用当前名称假装具备该能力。

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
boot_ctrl 两阶段提交断电、每个 copy sector 随机断电、Flash 读写失败、重复 DFU 包、
越界/乱序包、UART 丢包、watchdog 窗口、pending 未确认、OTP 提交失败、handoff 后首个
中断、MSP watermark 和连续多次升级。
