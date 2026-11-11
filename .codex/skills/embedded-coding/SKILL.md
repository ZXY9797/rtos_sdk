---
name: embedded-coding
description: 编写、修改和审查 MCU/RTOS 的 C/C++ 固件、驱动与 HAL；处理 SDK 上游同步、私有修复保护和构建集成。用于涉及中断、并发、DMA、资源生命周期或嵌入式配置的实际代码任务；仅分析日志或控制调试器时使用相应调试流程。
---

# Embedded Coding

以当前源码、配置和调用链为依据，完成范围明确、可以验证的嵌入式修改。
先保留项目已有的架构与行为契约，再选择必要的抽象和验证；不因普通固件任务
自动套用安全认证流程、OOP 重构或统一线程模型。

功能开发或调试需要验证 KP 协议请求、应答、订阅和 USB/UART/CAN 通信时，
自动结合全局 `kptools-debug` Skill 与 `kptools` MCP 获取本次协议证据。
纯代码修改无需启动 KPtools；已有设备会话应复用，不能另开通信进程抢占端口。

## 开始工作

1. 检查 `git status`、适用的仓库指令和目标文件附近实现，保留 staged、unstaged、
   untracked 工作。找到实际调用者、配置入口、工具链与构建产物，不能只看目录名。
2. 确认执行上下文：初始化、普通线程、ISR、DMA 回调或 run-to-completion 事件处理。
   明确状态/缓冲的所有者、时序预算、错误返回、取消与清理契约。
3. 普通修复沿证据收窄改动；上游导入先读
   [upstream-sync.md](references/upstream-sync.md)，完成路径映射、基线比较和私有修复保护。
4. 修改驱动、配置或 SDK 时，读 [build-validation.md](references/build-validation.md)，
   确认修改进入实际生成配置、编译链接和目标镜像。
5. 优先从已有上下文发现路径和参数；只询问会改变目标、行为或授权边界的缺失信息。

## 实现中的关键约束

- **执行上下文**：ISR 与有非阻塞契约的事件调用链不得引入睡眠、互斥等待或无界轮询。
  普通线程可按接口使用有界等待。沿 HAL 到具体实现核对阻塞性，不凭 API 名称推断。
- **接口与资源**：明确缓冲借用期限、DMA/线程所有权交接、回调上下文、失败回收、
  取消/关闭次序和再次进入条件。`volatile` 不替代同步、原子性或缓存一致性。
- **硬件行为**：核对芯片对应的寄存器语义、读清/W1C、时钟、引脚、IRQ 与缓存/DMA 可达性。
  配置值、单位、符号、采样窗口和保护索引必须沿调用链对应，不能类推扩大其他通道。
- **架构选择**：复用已有设备接口、状态机和队列。只有确有替换/扩展需求时才增加函数表、
  虚函数或 CRTP；按延迟、栈和调度预算选择工作线程、workqueue 或 ISR 安全事件投递。
- **C/C++ 约定**：沿用项目格式、内存分配器、异常/RTTI 与语言标准配置。
  未规定行宽时默认目标为 **100 列**，已有 formatter 或项目约定优先。
  相邻函数定义之间、函数内不同职责的逻辑段之间默认保留 **1 个空行**；
  同一逻辑段内保持连贯，不在每条语句或每个大括号后机械插空行。
  不为缩短函数、控制行数或满足行宽而删除必要留白、合并多条语句或压缩分支。
  排版细节见 [code-style.md](references/code-style.md)，用户本次明确的格式要求优先。
  实时热路径避免不可控分配，资源适合时用 RAII，外部输入检查与内部断言分别处理。
  不以统一行数、禁用某个容器或强制设计模式取代实际预算与项目规范。
- **变更边界**：保留本地扩展与私有 bugfix；新行为要覆盖相应头文件、配置、生成器和消费者。
  不为了通过构建而静默删保护、增缓冲掩盖泄漏，或恢复产品有意移除的上游 API。

## 按需阅读

只加载本次机制所需的参考。下列旧有示例中的具体数值、线程模型、风格和库禁用项，
在项目有对应约束时采用；用户要求和当前项目配置优先，不为满足示例重写无关代码。

| 任务 | 参考 |
|---|---|
| 上游同步、HAL/CMSIS 更新、局部移植 | [upstream-sync.md](references/upstream-sync.md) |
| CMake/Kconfig/DTS、生成代码、ELF/map、验证范围 | [build-validation.md](references/build-validation.md) |
| 模块边界、接口或回调设计 | [architecture.md](references/architecture.md)、[design-patterns.md](references/design-patterns.md) |
| 函数、错误传播、命名和格式 | [clean-code.md](references/clean-code.md)、[code-style.md](references/code-style.md) |
| C++、RAII、编译选项与 C ABI | [cpp-embedded.md](references/cpp-embedded.md) |
| 内存所有权、并发和共享资源 | [memory-safety.md](references/memory-safety.md) |
| HAL、寄存器、DMA、非阻塞调用链 | [hardware-interaction.md](references/hardware-interaction.md) |
| ISR、优先级与任务协作 | [interrupt-handling.md](references/interrupt-handling.md)、[rtos-programming.md](references/rtos-programming.md) |
| 低功耗、时钟与唤醒 | [low-power-design.md](references/low-power-design.md) |
| 日志、断言和崩溃记录 | [debug-logging.md](references/debug-logging.md) |
| 总线、协议帧和错误恢复 | [communication-protocols.md](references/communication-protocols.md) |
| 主机、集成或 HIL 测试设计 | [embedded-testing.md](references/embedded-testing.md) |
| 安全启动、密码学和不可信输入边界 | [embedded-security.md](references/embedded-security.md) |
| 修改后的风险核对 | [safety-checklist.md](references/safety-checklist.md) |

## 验证与交付

对完成的一组相关修改，按 [safety-checklist.md](references/safety-checklist.md) 检查受影响
的逻辑、资源、并发和硬件契约；不相关项可注明不适用。修复后重跑受影响的检查与项目要求。
检查需针对可观察行为和故障机制，不为可逆的小改编写重复实现的形式测试。

报告修改解决的触发条件、关键源码/产物证据、实际命令与结果，以及剩余验证边界。
源码/主机测试、目标构建、烧录回读和板上实测分别说明。
最后检查差异范围，提交/推送按用户请求执行。
需要操作真实目标时使用可用的 `$mcu-ai-debug`；源码任务不隐含烧录、复位或暂停授权。
