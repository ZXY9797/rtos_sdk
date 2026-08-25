# 产品级 Loader 设计与构建

## 定位

当前 loader 是单线程裸机固件，不启动 FreeRTOS 或 RT-Thread 调度器。它使用
baremetal OSAL 提供的 SysTick、互斥量、信号量、静态 StreamBuffer、ISR 上下文
和低功耗等待，并复用统一设备模型、DTS 生成设备、UART/DMA 驱动和 initcall。

loader 明确禁止动态分配：
`CONFIG_OSAL_BAREMETAL_HEAP_SIZE=0`。构建系统同时强制
`CONFIG_RTOS_BAREMETAL=y`，避免误把线程或调度器能力带入恢复固件。

## 启动顺序

```text
Reset_Handler
  -> soc_reset_hook()
  -> z_prep_c()：VTOR、data/bss、cache/FPU 基础准备
  -> soc_early_init_hook()：产品 SoC 初始化
  -> C/C++ preinit_array、init_array
  -> EARLY/PRE_KERNEL_1..3 initcall
  -> osal_init()：启动裸机 SysTick
  -> POST_KERNEL initcall
       25：DTS 生成的 UART 设备
       80：DFU transport 注册
  -> APPLICATION initcall
  -> boot_logic()
```

loader 源码使用 object library，确保只包含 initcall 或静态构造的翻译单元也会进入
最终 ELF。链接脚本显式保留构造数组以及全部 initcall level；任何初始化失败都会倒序
执行 rollback，记录失败项并停机，生产 watchdog 可随后复位系统。

## DFU 与设备驱动

开发配置默认 `CONFIG_BOOT_DFU_UART=y`，DTS 必须提供 `uart0` alias。UART 设备先完成
构造和 POST_KERNEL 初始化，transport 后注册协议回调。回调的接收长度参数是输入容量
和输出实际长度，协议层会再次校验边界。协议只接受来自合法单播发送者、目的地址为
`0x20` 的明文请求；广播仅允许 `QUERY_STATUS`，广播擦除、写入、校验和复位均静默丢弃。

可配置 `CONFIG_BOOT_DFU_CUSTOM=y`，此时产品必须提供：

```text
bootloader/product/<product>/common/boot_transport.cc
```

该文件需要注册 transport，并实现 `boot::transport_shutdown()`。量产配置强制选择该
transport，并要求 provider 在把帧交给通用协议层前完成会话认证、授权、重放限制和
失败限速；仓库自带的裸 UART transport 只能用于开发。应用跳转前 loader 会先关闭
transport，防止 UART、DMA 或中断继续访问应用接管后的内存。

## 升级模式

当前默认模式保留 `CONFIG_BOOT_MODE_STAGED_COPY` 配置名以兼容已有产品，实际算法是
单执行槽 sector-swap，不是真正的双执行槽 A/B：

- `slot0` 是唯一执行分区；
- `upgrade` 下载新镜像，交换完成后保存旧镜像；
- `scratch` 恰好一个 Flash erase block，逐 sector 完成三阶段交换；
- 每个阶段写后回读，并把 phase/progress 写入独立双 sector journal；
- 新镜像进入 `TRIAL_APP`，下次复位未确认会持久化 `ROLLBACK_APP` 并交换回旧镜像；
- 只有确认后才提交 monotonic security version 并使旧镜像不可启动。

`CONFIG_BOOT_MODE_AB` 仅为兼容旧配置名，代码使用语义正确的
`CONFIG_BOOT_MODE_STAGED_COPY`。直接覆盖模式只允许开发使用，生产配置会在 CMake
阶段拒绝。

工厂三合一镜像把 `CONFIRMED` 应用直接放入 slot0；在线 OTA/DFU 包必须用
`pack_image.py --pending` 生成并写入 upgrade。staged loader 会拒绝 upgrade 中预确认
镜像，避免绕过应用健康检查和 trial rollback；boot journal 丢失时也不会启动未受控的
slot0 `PENDING` 镜像。

## 镜像校验和安全策略

镜像校验依次覆盖：

1. magic、128-byte header、长度、load address 和状态位；
2. 分区边界和 uint32 地址溢出；
3. payload SHA-256；
4. 可选 ECDSA-P256 签名；
5. MSP 范围/8-byte 对齐、Thumb reset handler 和入口范围；
6. ProductInfo magic、CRC 和 product ID；
7. monotonic security version。

`CONFIG_BOOT_PRODUCTION=y` 自动要求签名、sector-swap、watchdog 和产品安全 provider。
产品必须提供：

```text
bootloader/product/<product>/common/boot_security.cc
bootloader/product/<product>/common/boot_watchdog.cc
```

安全 provider 必须实现受保护公钥验签、OTP 或等价的只增版本存储，以及 preloader 对
固定 loader 的信任根校验。watchdog provider 必须实现
`watchdog_service()` 和 `watchdog_prepare_handoff()`。仓库不提供量产私钥、默认公钥或
伪 OTP；示例产品缺少 provider 时生产构建会按设计失败关闭。

## 应用跳转

跳转前重新读取并检查向量，关闭 transport，交接 watchdog，然后清理 SysTick、NVIC
enable/pending、PendSV 和 SysTick pending，更新 VTOR。最终 handoff 使用 naked 汇编
恢复 CONTROL/BASEPRI/FAULTMASK/PRIMASK、切换 MSP 并直接 `bx` 到应用入口，不会在
应用栈上执行 loader 的 C++ 函数尾声。

## 尺寸约束

loader 链接区域就是物理 `loader` 分区，不再把 RAM 或其他 Flash 分区计入可用容量。
生成 BIN 后还会执行独立的物理分区检查。不要在文档中固化某次构建的字节数；发布记录
必须从本次目标 toolchain 生成的 BIN/map 读取 `actual / partition / margin`。sector-swap
增加了状态机代码和固定 512 B 复制/校验缓冲，必须重新测量两个产品的实际余量。

loader 使用 `-Os`、`-nostartfiles`、无异常/RTTI/thread-safe statics，主栈默认 2 KiB。
loader 自有函数若静态栈帧超过 768 B，编译会失败。最终栈大小仍须结合中断嵌套和
目标板 stack watermark 验证。

本次布局从 storage 尾部划出 `scratch`，两个产品的 storage 起始地址保持不变，但可用
容量均减少一个 erase block。demo 的物理 slot0 大于 upgrade，应用链接与工厂打包上限
因此统一取 `min(slot0, upgrade)`。旧镜像若超过该上限会安全进入 DFU，不能在线交换；
升级方案必须检查旧 NVS 是否占用被划出的尾 sector，并选择受控全量刷写或数据迁移。

## 构建和验收

```powershell
python bootloader\scripts\build_3in1.py demo `
  --out out\demo_3in1
python bootloader\scripts\build_3in1.py demo_ble `
  --out out\demo_ble_3in1
python -m unittest discover -s bootloader\scripts\tests `
  -p "test_*.py" -v
```

编译、map、BIN 和主机掉电模型通过，只证明软件静态路径。量产放行仍必须在两类实际
芯片上验证 UART/自定义 DFU、Flash 最坏擦写时间、随机断电恢复、损坏镜像、降级攻击、
OTP 提交失败、watchdog 复位、应用 handoff 和 stack watermark。
