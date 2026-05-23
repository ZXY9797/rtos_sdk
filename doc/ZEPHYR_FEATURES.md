# Zephyr 特性总结

本文档总结了Zephyr RTOS的核心特性，这些特性已经被移植到MSDK项目中。

## 1. 内核架构

### 1.1 微内核设计
Zephyr采用微内核架构，核心组件包括：
- **线程管理**：支持协作式和抢占式调度
- **同步原语**：信号量、互斥锁、条件变量
- **内存管理**：堆内存管理、内存池
- **定时器**：高精度定时器管理

### 1.2 初始化框架
Zephyr使用分级初始化框架：
```
EARLY → PRE_KERNEL_1 → PRE_KERNEL_2 → POST_KERNEL → APPLICATION → SMP
```
每个级别可以设置优先级（0-999），确保初始化顺序正确。

### 1.3 设备驱动模型
- 基于设备树的设备描述
- 统一的设备驱动接口
- 自动设备初始化

## 2. 多次链接特性

### 2.1 三阶段链接
Zephyr支持最多三个链接阶段：
1. **zephyr_pre0**：初始链接，收集设备依赖
2. **zephyr_pre1**：生成内核对象哈希表
3. **zephyr_final**：最终镜像

### 2.2 链接脚本片段化
链接脚本被分解为多个片段：
- `common-rom.ld`：ROM段定义
- `common-ram.ld`：RAM段定义
- `common-noinit.ld`：NOINIT段定义
- `kobject-*.ld`：内核对象段

### 2.3 可迭代段机制
使用`ITERABLE_SECTION_ROM`和`ITERABLE_SECTION_RAM`宏：
- 编译时自动收集变量
- 链接时排序并定义符号
- 运行时可通过宏遍历

## 3. 零延迟中断

### 3.1 原理
零延迟中断运行在比`irq_lock()`更高的优先级：
- 不会被内核锁屏蔽
- 适合时间关键的中断处理
- 可以在临界区内触发

### 3.2 使用方法
```c
// 声明零延迟中断
IRQ_DIRECT_CONNECT(irq, priority, isr, IRQ_ZERO_LATENCY);

// 直接中断处理函数
ISR_DIRECT_DECLARE(my_isr)
{
    // 处理中断
    return 0; // 不需要重调度
}
```

### 3.3 优先级管理
- 普通中断：优先级 = 配置值 + 偏移
- 零延迟中断：使用最高优先级（0）

## 4. 用户空间

### 4.1 内核对象管理
- 使用gperf生成完美哈希表
- 支持静态和动态对象
- 权限位图控制访问

### 4.2 系统调用机制
```
用户空间 → SVC指令 → 内核空间
    ↓
系统调用分发表
    ↓
验证函数 → 实现函数
```

### 4.3 内存域管理
- 每个线程属于一个内存域
- 分区控制内存访问权限
- 支持多分区组合

### 4.4 特权栈管理
- 每个用户线程有独立特权栈
- 系统调用时切换到特权栈
- 支持静态和动态分配

## 5. 多架构支持

### 5.1 架构抽象层
```
┌─────────────────────────────────┐
│         应用层 (Application)      │
├─────────────────────────────────┤
│         内核层 (Kernel)           │
├─────────────────────────────────┤
│    架构抽象层 (arch_interface.h)   │
├──────┬──────┬──────┬───────────┤
│ ARM  │RISC-V│ x86  │   ...     │
└──────┴──────┴──────┴───────────┘
```

### 5.2 支持的架构
- ARM Cortex-M
- ARM Cortex-A/R
- RISC-V
- x86
- Xtensa
- ARC

### 5.3 添加新架构
1. 创建`arch/<arch>/`目录
2. 实现`arch_interface.h`中的接口
3. 在`arch/Kconfig`中声明能力
4. 在`arch/archs.yml`中注册

## 6. 构建系统

### 6.1 Kconfig配置
- 支持层次化配置
- 条件编译支持
- 默认值和依赖关系

### 6.2 设备树支持
- 描述硬件拓扑
- 生成设备配置
- 驱动绑定

### 6.3 CMake构建
- 模块化构建系统
- 条件编译支持
- 自动依赖跟踪

## 7. 内存管理

### 7.1 堆内存
- `k_malloc()` / `k_free()`
- `k_calloc()` / `k_realloc()`
- 对齐分配支持

### 7.2 内存池
- 固定大小块分配
- 无碎片
- 快速分配/释放

### 7.3 内存域
- 用户空间内存隔离
- 分区权限控制
- 动态分区管理

## 8. 同步原语

### 8.1 信号量 (k_sem)
- 计数信号量
- 超时等待
- 优先级继承

### 8.2 互斥锁 (k_mutex)
- 递归锁支持
- 优先级继承
- 死锁检测

### 8.3 条件变量 (k_condvar)
- 等待/通知机制
- 与互斥锁配合使用

### 8.4 事件 (k_event)
- 位掩码事件
- 多事件等待

## 9. 定时器

### 9.1 内核定时器
- 一次性/周期性定时器
- 回调函数机制
- 高精度时间管理

### 9.2 系统时钟
- 可配置Tick频率
- 64位时间计数器
- 睡眠和延时函数

## 10. 调试支持

### 10.1 栈保护
- 栈溢出检测
- 栈金丝雀
- MPU栈保护

### 10.2 断言
- `__ASSERT()`宏
- 可配置断言处理
- 文件名和行号信息

### 10.3 日志系统
- 分级日志
- 模块化日志
- 后端可配置

## 11. 移植到MSDK的关键变化

### 11.1 移除osal层
- 直接使用zephyr内核API
- `osal_thread_create()` → `k_thread_create()`
- `osal_sleep()` → `k_msleep()`
- `osal_mutex_*` → `k_mutex_*`

### 11.2 保留的配置核心
- 项目配置：`-Dp=demo`
- 设备树：`app/demo/config/board.dts`
- Kconfig：`app/demo/config/prj.conf`

### 11.3 构建命令不变
```bash
# 构建
cmake -B out -GNinja -Dp=demo

# 编译
ninja -C out
```

## 12. 参考资料

- Zephyr官方文档：https://docs.zephyrproject.org
- Zephyr GitHub：https://github.com/zephyrproject-rtos/zephyr
- Zephyr API参考：https://docs.zephyrproject.org/latest/reference/index.html
