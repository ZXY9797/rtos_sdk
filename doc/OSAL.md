# OSAL 设计与使用约束

## 目标

OSAL 为应用和组件提供统一的 C++ 接口，并支持 FreeRTOS、RT-Thread 和
baremetal 三种后端。后端由 Kconfig 显式选择；没有匹配项时 CMake 直接失败，禁止
静默切换到另一个内核。

公共接口位于 `embedded/include/osal/`，后端类型和容量常量分别位于
`embedded/osal/<backend>/osal_types.h`。业务代码不得包含 FreeRTOS 或 RT-Thread
私有头文件。

## 配置

```text
CONFIG_RTOS_FREERTOS=y
# 或 CONFIG_RTOS_RT_THREAD=y
# loader 固件由构建系统强制选择 CONFIG_RTOS_BAREMETAL
```

关键配置包括：

- `CONFIG_SYS_CLOCK_TICKS_PER_SEC`：内核 tick 频率；
- `CONFIG_NUM_IRQ_PRIO_BITS`：NVIC 实现的优先级位数；
- `CONFIG_FREERTOS_HEAP_SIZE`：FreeRTOS heap_4 容量；
- `CONFIG_RTTHREAD_HEAP_SIZE`：RT-Thread 系统堆容量；
- `CONFIG_OSAL_TICKLESS_IDLE`：启用产品级 tickless provider；
- `CONFIG_APP_WATCHDOG`：启用系统健康监控和硬件看门狗 provider。

组件不得只判断“选择了某个 RTOS”，而应依赖 `OSAL_HAS_THREADS`、
`OSAL_HAS_STATIC_THREADS`、`OSAL_HAS_STATIC_PERIODIC_THREADS`、
`OSAL_HAS_MESSAGE_QUEUES`、`OSAL_HAS_EVENT_GROUPS`、
`OSAL_HAS_SOFT_TIMERS` 或 `OSAL_HAS_STREAM_BUFFERS` 等能力符号。baremetal
只声明实际实现的能力，线程类 API 返回失败，不能用 no-op 冒充成功。

`CONFIG_NUM_IRQ_PRIO_BITS` 会同时进入 OSAL、FreeRTOSConfig 和设备树生成代码。
后端实现还会与 CMSIS 的 `__NVIC_PRIO_BITS` 做编译期一致性检查。

## 初始化与 C++ 生命周期

应用启动顺序固定为：

```text
reset/C runtime
  -> osal_init()
  -> .preinit_array / .init_array
  -> EARLY / PRE_KERNEL initcall
  -> scheduler start
  -> POST_KERNEL / APPLICATION initcall
```

因此全局 C++ 对象的构造不会早于 OSAL 基础初始化，也不会晚于依赖它们的 initcall。
链接脚本必须 `KEEP` 构造数组和所有 initcall section；最终应用 ELF 会自动执行策略
检查。

## 线程生命周期

`Thread` 和 `PeriodicThread` 采用协作式停止：

1. 所有线程入口周期性检查 `stop_requested()`；
2. 所有永久阻塞必须使用有限超时，或能被 shutdown 显式唤醒；
3. owner 先调用 `request_stop()`，再 `join()`/`shutdown()`；
4. 线程不得删除自己；native task 仅由 owner 在确认退出后删除；
5. 超时退出会进入统一 fatal 路径，不能带着悬空上下文继续运行。

`Thread` 和 `PeriodicThread` 对象在析构或 `delete` 前必须显式调用 `shutdown()` 并检查
返回值，或调用带有界等待和 fatal 处理的 `destroy()`。析构函数本身不做有界等待；若
owner 违反生命周期契约且 native task 仍存活，系统会进入统一 fatal 路径，防止释放仍
被任务访问的对象和栈。

应用线程应优先使用调用方提供的静态栈。动态创建只允许在初始化阶段，并且必须检查
返回值。`PeriodicThread::start()` 支持调用方持有线程对象和栈，外部触发模式的信号量
control block 也内联在对象中；RTOS backend 的普通 `Mutex`/`Semaphore` native
control block 也内联在 C++ 对象中，baremetal 使用固定静态池。Link、日志、watchdog
以及 demo 的 CLI/控制环/SensorCore/LED 线程均走调用方对象与栈的静态路径。运行期
禁止无界堆分配。

OSAL 统一规定：`0` 是最低线程优先级，`kPriorityMax` 是最高线程优先级。FreeRTOS
直接使用该数值；RT-Thread backend 会反向映射到其“数值越小优先级越高”的 native
语义。业务代码不得把 OSAL priority 直接传给内核 API。

## IPC 与内存

静态 `MessageQueue` 和 `StreamBuffer` 的数据区与 control block 均由调用方/对象内联
提供，后端不得偷偷申请控制块。`MessageQueue::storage_size()` 会计入后端对齐和每槽
元数据，保证声明深度等于实际深度；创建接口会检查乘法溢出、容量、对齐和触发阈值。

`StreamBuffer` 与 FreeRTOS 原生契约一致：一个实例只允许一个 producer 和一个
consumer；多 producer 或多 consumer 必须在调用方串行化。ISR 和线程不能同时充当
同一侧而不加保护。

`trigger_level` 只控制“接收方在空缓冲区上阻塞后”的唤醒阈值；缓冲区已经有数据时，
接收立即返回现有字节。动态缓冲区的可用容量等于请求容量；静态缓冲区保留 1 字节
区分 full/empty，可用容量为 `storage_size - 1`。三种后端都拒绝无法达到的触发阈值。

`Kernel::memory_stats()`、`Thread::stack_stats()` 和
`PeriodicThread::stack_stats()` 提供堆最低余量及线程最小剩余栈；不支持的 backend
显式返回 `available=false`。`MessageQueue::stats()` 提供当前深度、高水位和发送失败
计数。发布定容必须读取这些接口的长稳实测结果，不能只用配置值估算。

FreeRTOS 与 RT-Thread 都以 `CONFIG_SYS_CLOCK_TICKS_PER_SEC` 作为唯一 tick 频率配置源。
RT-Thread 的同优先级线程固定使用 1 tick 时间片，并开启内核栈越界检查；这两个 native
策略不进入公共 `ThreadConfig`，避免业务代码依赖特定内核。栈检查仍需在目标板压力场景
验证，不能替代每个线程的最小剩余栈观测。

## IRQ 规则

- 只有转换后的 native 优先级位于 `kMinRtosCallableIrqPriority` 到
  `kLowestIrqPriority`（含边界）的 ISR 才能调用 OSAL FromISR API；
- DTS 使用逻辑硬件 IRQ 优先级；生成器先加 Cortex-M fault/SVC 保留偏移，再校验 native
  优先级是否可调用 RTOS，同时以 `Irq::kLowestPriority` 阻止最低值加偏移后回绕为最高
  硬件优先级；
- 设备树生成器同时检查 priority 上界和 syscall 下界；
- ISR 只做确认中断、搬运有限数据和唤醒任务；
- `IsrContext` 统一收集是否需要调度，ISR 退出前只请求一次切换；
- ISR 禁止阻塞、日志格式化、堆分配和普通 mutex。

## 低功耗

默认不开启 tickless。启用 `CONFIG_OSAL_TICKLESS_IDLE` 后，产品必须提供
`app/product/<product>/common/osal_low_power.cc`，实现经 SoC 验证的
`vPortSuppressTicksAndSleep()`。该 provider 必须处理：

- 唤醒源配置与竞态；
- tick 补偿和最大睡眠时长；
- 时钟关闭/恢复顺序；
- 睡眠前调用 `hal::suspend_devices()`，唤醒后调用 `hal::resume_devices()`，并处理失败；
- 调试器连接、临界区和不可睡设备；
- 实测时基漂移与唤醒延迟。

缺少 provider 时配置失败；框架不提供假睡眠实现。

## 后端验证

至少执行以下组合：

```powershell
cmake -S . -B out\demo_freertos -GNinja -Dp=demo
cmake --build out\demo_freertos --parallel

cmake -S . -B out\demo_rtthread -GNinja -Dp=demo `
  -DEXTRA_CONF_FILE=tests/config/rtthread.conf
cmake --build out\demo_rtthread --parallel
```

编译和 ELF 策略检查只能证明源代码、链接和静态契约成立。调度延迟、ISR 嵌套、栈
水位、看门狗窗口、tickless 漂移仍必须在真实目标板上验证。
