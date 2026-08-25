# 产品级 RTOS 架构基线

本文定义当前代码框架可承诺的能力、产品必须补充的 provider，以及发布前必须完成的
目标板验证。不能把“编译通过”表述为“已经具备量产运行证据”。

## 架构边界

```text
Application / Product policy
  -> Components (link, BLE, NVS, FOC ...)
  -> Device model + OSAL
  -> Driver API
  -> SoC backend / vendor HAL
  -> registers and hardware

DTS: hardware topology and immutable resources
Kconfig: feature and policy selection
CMake: source selection, generation and fail-closed integration gates
```

应用和组件只能依赖设备接口与 OSAL，不应直接依赖 vendor HAL。驱动 backend 消费
DTS 生成的寄存器、IRQ、DMA、pinctrl 和父设备引用。Kconfig 决定功能是否存在，CMake
只负责把选中的实现闭环到目标中，禁止用 CMake option 再建立一套平行配置。

## 当前已落地的框架能力

| 领域 | 当前保证 |
|---|---|
| 启动 | OSAL 初始化后执行 C++ 构造，再按 level 执行 initcall；ELF 自动检查 section |
| 后端 | FreeRTOS、RT-Thread、baremetal 显式选择；未知后端/内核直接配置失败 |
| IRQ | NVIC 位数单一配置源，CMSIS 编译断言，DTS 生成器检查上下界 |
| 线程 | 协作停止、有限等待、owner join/delete；框架与 demo 关键线程使用静态栈/TCB；超时 fatal |
| IPC | mutex/semaphore/静态队列/流缓冲内联控制块，容量与溢出校验 |
| 设备 | 显式生命周期和错误状态；生成 registry；逆序 suspend/顺序 resume |
| 通信 | 幂等 ACK replay、完整报文精确比对、可插拔 AEAD 和原子诊断统计 |
| 故障 | assert、异常、栈溢出、分配失败、init 失败统一持久化并复位 |
| 健康监控 | 可选静态 watchdog monitor；只有全部 client 新鲜时才喂硬件狗 |
| 构建 | DTS/Kconfig/CMake/layout 闭环，应用 ELF 策略门禁，host 单元测试 |
| Loader | baremetal、sector-swap/trial 回滚、容量及生产 provider 门禁 |

当前产品级构建矩阵覆盖 GD32 `demo` 和 Goodix `demo_ble`。STM32 FDCAN backend 已闭环
到 HAL 源码、显式时序和共享时钟生命周期，但仓库尚无 STM32 产品板可执行完整链接与
实板验收。STM32 UART/SPI/PWM/I2C/Flash 直寄存器 backend 必须显式开启
`CONFIG_STM32_DIRECT_REGISTER_DRIVERS`；它们只用于 bring-up，在产品补齐 clock/reset
owner、板级 DTS 和硬件测试前不会默认选中。

## 必须由产品实现的能力

框架不能安全猜测以下硬件行为，因此在对应功能开启时采用 fail-closed：

### 应用硬件看门狗

产品启用 `CONFIG_APP_WATCHDOG` 后必须提供：

```text
app/product/<product>/common/app_watchdog.cc
```

实现 `embedded/include/system/watchdog.h` 声明的三个产品接口：

```cpp
bool app_watchdog_start(uint32_t timeout_ms);
void app_watchdog_feed(void);
bool app_watchdog_stop(void);
```

`start()` 必须完成硬件初始化并启动独立看门狗，`feed()` 必须是有界且不可阻塞的操作，
`stop()` 仅用于产品明确允许关闭看门狗的受控清理路径。量产配置还必须开启
`CONFIG_APP_PRODUCTION`，后者会强制看门狗能力存在，并要求 watchdog 启动前至少已有
一个业务 client。每个关键业务线程注册 client 并按自身 deadline 上报，monitor 不能
替业务线程伪造心跳。

仓库内两个 demo 已接入业务 client：`demo` 监控主循环和慢速控制环，`demo_ble` 监控
主循环和 BLE scheduler。client 在 `APPLICATION` priority 90 注册，watchdog monitor
在 priority 99 启动，因此量产策略会在任何业务线程运行前完成 client 完整性检查。

### Tickless 低功耗

启用 `CONFIG_OSAL_TICKLESS_IDLE` 后必须提供：

```text
app/product/<product>/common/osal_low_power.cc
```

该文件负责 SoC 睡眠、唤醒源和 tick 补偿，并在睡眠前后调用统一设备
`suspend/resume`。未提供时 CMake 失败；任一设备恢复失败必须进入产品 fatal/reset
策略，不能继续在部分恢复状态运行。

### Link 安全 Provider

启用 `CONFIG_LINK_SECURITY`（包括使用 `LINK_SECURE_HANDLER()`）后必须提供：

```text
app/product/<product>/common/link_security.cc
```

实现真实 AEAD、受保护密钥和 nonce/replay 策略。所有 `SecurityContext` 字段都必须作为
AAD；Router 只把 retransmit 位视为可变传输元数据。缺文件、空 provider 或认证失败均
fail closed。逐命令安全要求使用 `LINK_SECURE_HANDLER()`。

### Loader 安全与恢复

生产 loader 必须提供真实签名校验、防回滚版本存储、独立看门狗、掉电安全 Flash
操作和认证 DFU transport。裸 UART transport 仅用于开发；产品 transport 必须在向
通用协议层交付明文帧前完成会话认证、授权、解密、重放限制和失败限速。当前单执行槽
sector-swap 能在一次未确认试运行后恢复旧应用，但不等于两个可独立启动槽的完整 A/B；
各 phase/sector 的掉电矩阵和故障注入仍是产品验收项。

## 统一故障模型

所有不可恢复错误进入 `hal::fault::panic()`，记录：

- fault magic/version/CRC；
- fatal reason、detail、源代码行；
- PC/LR/xPSR/MSP/PSP 和 Cortex-M fault status；
- 有界 context 字符串及 hash。

记录位于 `.noinit`，复位后由诊断逻辑读取。生产版本必须定义上传、清除、重复故障
抑制和隐私策略。若设备无法复位，fatal path 保持停机而不是继续执行受损状态。

## 发布门禁

每次发布至少完成：

1. `demo`、`demo_ble` 的 FreeRTOS 应用构建；
2. `demo` 的 RT-Thread 交叉后端构建；
3. preloader、loader、upgrade 和 3-in-1 构建；
4. boot Python 测试、设备树生成器测试和 host CTest；
5. 最终 ELF 无未解析符号，构造数组/initcall/`.noinit` 策略检查通过；
6. map/size/BIN 分区容量复核；
7. 真实板卡上的 IRQ、压力、栈水位、长稳、复位原因和故障记录验证；
8. 开启看门狗后的任务卡死、调度器卡死和喂狗窗口验证；
9. 开启低功耗后的唤醒源、时基漂移和外设恢复验证；
10. loader 的签名错误、版本回滚、镜像损坏、swap/rollback 各阶段掉电注入；
11. Link 明文降级、tag/AAD 篡改、nonce 重放、sequence 冲突和重复命令幂等性。

## 已知边界

- 当前仓库中的 demo 产品没有伪造硬件 watchdog/tickless provider，相关功能默认关闭；
- demo 产品也没有量产 Link 密钥 provider，开启全局安全策略的负向构建应失败；
- 仓库没有 STM32 产品目标；FDCAN 目前只有 backend 编译契约，STM32 直寄存器驱动默认
  fail closed，不能作为量产支持声明；
- 主机测试不能覆盖真实 ISR 抢占和弱内存顺序；
- 静态内存策略降低运行期碎片风险，但每个产品仍需基于 map 和栈水位实测定容；
- 设备 ready 只表示软件初始化状态，不代表外部器件的所有模拟/射频指标合格；
- “具备产品级框架门禁”不等于某块硬件已经完成产品认证。
