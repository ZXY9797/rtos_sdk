# Cortex-M 异常处理框架

## 设计目标

异常路径必须在栈、调度器、heap、日志锁或外设状态已经损坏时仍保持可预测。因此 fault handler：

- 不分配动态内存，不使用虚函数、RTOS 锁、`printf` 或普通日志后端；
- 先把寄存器、SCB 状态、回溯和 128 字节栈快照写入静态记录；
- 使用 CRC 校验，并最后写 `magic` 提交 `.noinit` 记录；
- 在关中断状态下调用固定容量、启动期冻结的 POD 回调表；
- 处理完成后保持关中断并停机，等待看门狗或调试器接管。

支持 HardFault、MemManage、BusFault、UsageFault、断言以及 FreeRTOS 栈溢出/
内存分配失败钩子。`FaultRecord` 的持久化格式版本编码在 `MAGIC` 低字节中，修改结构
布局时必须升级版本。

## 调用链

```text
vector table
  -> fault.S 保存 r4-r11，选择 MSP/PSP
  -> arm_fault_handler(frame, exc_return, active_sp)
  -> 构造并提交 FaultRecord
  -> 内建 noinit/UART 后端
  -> 启动期冻结的产品 Backend 回调
  -> 关中断停机
```

`fault_init` 位于 `PRE_KERNEL_1`，会处理上次复位保留的记录，然后冻结注册表。注册必须
发生在它之前；`main()` 中注册会失败。描述符由框架复制，调用方描述符本身无需静态生命期，
但 `context` 指向的对象和回调代码必须在整个固件生命期有效。

## 后端契约

后端是无虚表的 POD 回调：

```cpp
struct Backend {
    void *context;
    void (*on_fault)(void *context, const FaultRecord &record);
    void (*on_boot)(void *context);
    void (*clear)(void *context);
};

static Backend backend {
    .context = &storage,
    .on_fault = store_fault,
    .on_boot = publish_previous_fault,
    .clear = clear_storage,
};

const bool registered = hal::fault::registerBackend(backend);
```

最多注册 `MAX_BACKENDS` 个后端。空回调允许；`on_fault` 在中断关闭的异常上下文执行，
不得获取锁、等待中断/DMA、访问 heap、调用调度器或依赖可能尚未初始化的驱动。Flash 后端
只能使用预擦除、独占、已验证为 fault-safe 的介质；不要在 fault 内临时擦除 NVS 分区。

## UART 故障输出

`CONFIG_FAULT_UART_BACKEND` 默认关闭。启用后，产品/SoC 必须提供强符号
`hal::fault::putc(char)`，实现应是有界、轮询、无锁且不依赖中断的最小发送路径；缺少该
实现会链接失败。未启用时提供弱 no-op，仅保证断言调用链可链接，不能声称已有故障 UART。

普通 `LOG_*` 不可用于 fault handler。启动恢复阶段也优先先保留/上报记录，再由应用明确
清除，避免一次不可靠输出导致诊断丢失。

## 数据有效性与调试

`FaultRecord::valid()` 同时检查 `magic`、回溯深度和 CRC。`.noinit` 只能跨不清 RAM 的热
复位保存；断电后不保留。帧指针回溯要求目标以 `-fno-omit-frame-pointer` 构建，回溯失效时
原始寄存器和栈快照仍可离线分析。

发布前至少验证：

1. 人工触发四类 Cortex-M fault 和断言，核对 PC/LR/CFSR/HFSR；
2. 在 fault 写记录的每个阶段复位，半写记录不得被识别为有效；
3. 热复位能读取记录，冷启动不会误报随机 RAM；
4. 启用 UART 后端时，在关闭中断和调度器的条件下测量最坏输出时间；
5. 用 map 文件确认 `.noinit` 未被启动代码清零且没有越过 RAM 边界。

构建通过不能替代上述目标板异常注入。
