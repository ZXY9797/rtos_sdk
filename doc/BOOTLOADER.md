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

默认 `CONFIG_BOOT_DFU_UART=y`，DTS 必须提供 `uart0` alias。UART 设备先完成构造和
POST_KERNEL 初始化，transport 后注册协议回调。回调的接收长度参数是输入容量和输出
实际长度，协议层会再次校验边界。

可配置 `CONFIG_BOOT_DFU_CUSTOM=y`，此时产品必须提供：

```text
bootloader/product/<product>/common/boot_transport.cc
```

该文件需要注册 transport，并实现 `boot::transport_shutdown()`。应用跳转前 loader 会
先关闭 transport，防止 UART、DMA 或中断继续访问应用接管后的内存。

## 升级模式

当前默认模式是 staged-copy，不是真正的双执行槽 A/B：

- `slot0` 是唯一执行分区；
- `upgrade` 是下载和恢复暂存区；
- 暂存镜像完整校验后，按 Flash sector 擦写到 slot0；
- 每 32 KiB 写入持久化 checkpoint，复位后校验前缀并继续；
- 每个 sector 写后回读比较，最终再次执行完整镜像校验；
- 没有保留旧应用，因此新应用启动后的自动回滚不在当前能力范围内。

`CONFIG_BOOT_MODE_AB` 仅为兼容旧配置名，代码使用语义正确的
`CONFIG_BOOT_MODE_STAGED_COPY`。直接覆盖模式只允许开发使用，生产配置会在 CMake
阶段拒绝。

## 镜像校验和安全策略

镜像校验依次覆盖：

1. magic、128-byte header、长度、load address 和状态位；
2. 分区边界和 uint32 地址溢出；
3. payload SHA-256；
4. 可选 ECDSA-P256 签名；
5. MSP 范围/8-byte 对齐、Thumb reset handler 和入口范围；
6. ProductInfo magic、CRC 和 product ID；
7. monotonic security version。

`CONFIG_BOOT_PRODUCTION=y` 自动要求签名、staged-copy、watchdog 和产品安全 provider。
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
生成 BIN 后还会执行独立的物理分区检查。当前完整驱动和 DFU 已链接后的结果为：

| 产品 | loader BIN | 分区 | 占用 | 余量 |
| --- | ---: | ---: | ---: | ---: |
| demo / GD32F503 | 15,020 B | 24,576 B | 61.12% | 9,556 B |
| demo_ble / GR5525 | 13,328 B | 24,576 B | 54.23% | 11,248 B |

loader 使用 `-Os`、`-nostartfiles`、无异常/RTTI/thread-safe statics，主栈默认 2 KiB。
loader 自有函数若静态栈帧超过 768 B，编译会失败。最终栈大小仍须结合中断嵌套和
目标板 stack watermark 验证。

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
