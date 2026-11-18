# RTOS SDK 框架演进与使用

SDK 面向 MCU 实时控制，采用声明式硬件配置、静态资源和类型化组件。

## 需求与实现

| 需求 | 实现 | 验证入口 |
|---|---|---|
| 跨内核事件语义 | `osal/event_bits.h`、公共 `event_group.cc` | 主机并发测试；FreeRTOS/RT-Thread 运行测试固件 |
| 整帧通信接管 | `FrameTxQueue`、CAN/BLE transport | 邮箱拥塞、满队列、失败和 session reset |
| 发布配置闭环 | 显式 profile；三合一最终配置检查 | profile 构建、生产策略负向测试 |
| SDK/应用扩展 | CMake package、APPLICATION、项目 config/ 硬件描述 | external_app fixture 完整链接 |
| 外部模块与依赖 | MODULE_ROOTS、sdk_add_component | 外部 Kconfig 与组件编译 |
| 编译期能力与运行诊断 | DeviceMetadata、DeviceInfo、稳定 ID | 生成器测试与固件链接 |
| 硬件资源检查 | DMA/GPIO 占用、pinctrl 位域、已有 IRQ/依赖检查 | 生成器负向测试 |
| 新鲜度与通知样例 | ipc::experimental::TimedTopic | 主机样例与无效/过期/回绕测试 |
| 异步请求表样例 | runtime::experimental::AsyncPool | 主机容量、超时、旧 token、重复完成和取消测试 |
| 类型化通信样例 | link::experimental::TypedCommand | 主机请求/响应 codec、schema 检查 |
| DMA 内存契约 | DmaRegion、prepare/complete、GD32 SPI 接线 | 对齐/区域测试、缓存 provider 门禁 |
| 独立输出关闭 | system_safety、FOC/heater 注册 | latch 模型、异常/采样 overrun 路径 |
| 执行预算 | ExecutionSpec、TimingMetrics、gimbal | 回绕/预算测试、目标时序诊断 |
| 产品诊断 | Link 系统快照、故障 build ID、Boot 状态 | 编码/解码测试与目标构建 |
| 来源与资源报告 | sdk_artifacts、dependency_inventory | ELF、配置、镜像和依赖哈希 |
| 参考数据通路 | control_pipeline、gimbal | 主机端到端运行、MCU 构建及实板验收 |

## 构建和扩展

原有 `cmake -S . -B out/demo_foc -G Ninja -Dp=demo_foc` 保持可用。
App/Boot 共用 `app/product/<product>/config/board.dts`，外设默认关闭。
硬件和引脚配置随项目维护，不再单独选择 BOARD 或注册 BOARD_ROOT。
App 使用 `app/product/<product>/config/app.overlay`；各 Boot 镜像使用
`app/product/<product>/boot/<image>/config/<image>.overlay`。
全镜像分区布局统一位于项目 `config/layout.json`，链接脚本随各自镜像目录维护。
Boot overlay 仅保存镜像差异，不复制本项目的硬件 DTS。
目录职责、覆盖顺序和迁移示例见 [项目硬件与镜像配置](BOARD_CONFIGURATION.md)。

配置合并顺序固定为：

```text
board_defconfig -> CONF_FILE（默认 config/prj.conf）
                -> 当前镜像 config/profiles/<PROFILE>.conf
                -> 显式 EXTRA_CONF_FILE
```

不再自动合并配置目录中的所有 .conf。PROFILE=default 不附加 profile。
profile 只选择 Kconfig 文件，功能开关仍由 Kconfig 定义。
Boot profile 来自产品对应镜像的 config，不从通用 Boot 实现目录查找。

设备树顺序为 `board.dts -> 当前镜像 overlay -> DTC_OVERLAY_FILE -> 生成分区`。
默认 overlay 可省略，显式追加的文件缺失会报错。最终设备树会与共享 layout.json
核对 Flash 几何、分区集合和地址；App 的 chosen 分区须对应链接的 slot0。

独立产品可指定 APPLICATION=<绝对路径>，或者使用应用自己的 CMake 入口：

```cmake
cmake_minimum_required(VERSION 3.20)
set(p demo_foc)
set(APPLICATION "${CMAKE_CURRENT_LIST_DIR}/firmware")
find_package(RtosSdk REQUIRED CONFIG PATHS "${SDK_ROOT}/share/rtos-sdk-package")
project(my_product LANGUAGES NONE)
```

find_package 必须先于第一个 project，以便在主机平台初始化前选择交叉工具链。
应用创建 app target，SDK 为其接入平台依赖；平台也提供 `sdk::platform` 别名。
独立应用可在 config/board.dts 中描述自己的硬件，未提供时使用所选产品的硬件。
DTC_OVERLAY_FILE 在默认镜像 overlay 后显式追加 overlay 列表；
BOOT_LAYOUT 可选择已有布局名或外部 layout.json。
MODULE_ROOTS 是外部模块目录列表，每个目录提供 sdk-module.cmake，可设置
`SDK_MODULE_KCONFIG Kconfig`。应用按需链接 `sdk::algo` 或 `sdk::motor`，分别获取通用算法和电机计算。

模块的 `CMakeLists.txt` 可以使用如下写法。

```cmake
sdk_add_component(my_component
    SOURCES source.cc
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
    REQUIRES algo
    PRIV_REQUIRES osal)
```

对应别名为 sdk::my_component。编译策略只附加到通过 sdk_* 创建或接入的 target。
host 工程没有 sdk_build_config，可直接复用 `component/algo`、`component/motor`、`component/ipc`
与 `component/runtime`。接入例子见 [control_pipeline](../samples/control_pipeline/CMakeLists.txt) 和
[interaction_control](../samples/interaction_control/CMakeLists.txt)。
Hall 硬件门面位于 device_adapters，`sdk::motor` 接收采样值并计算角度和标定结果。
云台三轴领域契约位于产品目录。裁剪的 Zephyr 内部 CMake 辅助函数不作为稳定扩展 API。

runtime 的源码与公开头文件统一位于 `component/runtime`，由 `sdk::runtime` 提供。
平台实现显式依赖该纯 C++ 组件，runtime 不反向依赖平台头文件。稳定部分包括
ExecutionSpec/TimingMetrics、OutputLatch 和 WireWriter。

目前只有样例和测试使用的 AsyncPool、TimedTopic、TypedCommand 放在各组件
`include/<组件>/experimental/` 下，类型也位于对应的 `experimental` 命名空间。
这些接口可以用于验证设计，尚未承诺产品级 API 稳定性；后续根据真实产品调用决定是否稳定。
没有使用者的 BufferView 已撤出公共 API。

## 执行、数据与所有权

ExecutionSpec 描述触发、周期、deadline、预算和溢出策略。TimingMetrics 由一个 owner
更新，其他线程读取原子快照。微秒时间按 uint32 回绕，比较间隔不得超过 INT32_MAX。
固定区间直方图不构成 WCET 证明。
当前溢出策略只支持 RejectNewest、Fault，未实现的 CoalesceLatest 已撤出枚举；
原策略值 1 和其他无效枚举值均被配置检查拒绝，关闭时序统计也不会绕过检查。
ExecutionSpec 提供描述与测量边界，实际采样/线程频率仍由对应执行器配置。

SensorBatchCore 可注入时钟、执行描述和 metrics。未注入时钟时，原有合成采样时间
仅表达计划时间，不产生实测延迟。gimbal 接入 SysTick 插值时钟与 closed-loop metrics。
该时钟依赖已配置的固定 CPU 频率及正常 tick 服务；低功耗/动态时钟产品应注入适合其
硬件的时钟，板上验收需校准实际分辨率和误差。

硬件同步内环保留 ISR/外设触发路径。SnapshotTopic 提供最新状态；SpscRing 满时
拒绝新事件并计数，由产品决定重试或触发故障。TimedTopic 增加 schema、有效性、
时间戳和可选唤醒。唤醒回调运行在发布者上下文，ISR 发布要求回调有界且 ISR-safe。

AsyncPool 是串行 owner 的固定容量请求表，不操作外设。完成/取消只通知一次；
槽位重用后旧 generation token 无法完成新请求。超时/取消前 owner 必须先让外设停止
访问缓冲。cancel_all 期间拒绝新请求。TypedCommand 由 Schema 明确编码各字段，
不能拷贝 native struct；payload schema version 不改变 Link v1 的线格式。

GD32 SPI 接入 DMA prepare/complete。无缓存实现只接受 linker 声明的 RAM，不猜测
Flash/TCM/其他 SRAM 的 DMA 可达性。缓存目标必须提供 common/dma_memory.cc，
实现可达性、clean/invalidate 和整 cache line 所有权；缺失即配置失败。
产品可采用 DMA-safe staging/bounce buffer。STM32 异步 SPI 仍返回 NotSupported，
新增契约不扩大硬件支持声明。

## 故障与诊断

system_safety 在异常/致命错误记录和日志之前关闭已注册输出。FOC/heater 注册有界
停机回调，锁存后禁止重新 arm，复位后重新初始化。gimbal 采样/缓冲 overrun 可从
timer ISR 直接关闭输出，不依赖消费线程继续运行。CPU/时钟完全失效仍依赖独立
watchdog、硬件 Break 或驱动使能设计。

FaultRecord v4 在 CRC 覆盖范围内增加 build ID 的 128-bit 前缀；旧 v3 记录不再有效。
.noinit 用于保留 RAM 的复位，不保证断电后保留。

CAN/BLE 成功表示整帧已复制进有界队列，Busy 保留发送进度，成功不等于远端接收。
UART 保持有界同步发送，失败返回负值。线路故障仍可能留下不完整帧；框架不自动
重放有副作用的命令。断连和 Router 清理会重置 transport session。ACK timeout
须覆盖排队、发送和往返预算。

Link 诊断 command 1 保留协议统计，command 2 返回系统快照。command 2 无请求
payload；ACK status 之后是 v1 payload，包含 build ID、heap、设备、健康 client、
时序、故障来源和 Boot 控制状态。变长记录为 type/u16-length/payload，只输出完整
记录。设备/client 总数用于识别截断，多项原子计数不是同一时刻的事务快照。
安全策略沿用 Link 诊断配置。
`CONFIG_LINK_SYSTEM_DIAGNOSTICS` 可单独裁剪；小于 114 字节的 Link 帧配置不启用该服务。

```powershell
python tools/scripts/decode_system_diagnostics.py diagnostics-payload.bin
```

## 工具和验收

每次固件链接生成 image.manifest.json、.resources.json、.devices.dot 和
.architecture.md。build-info.json 关联 SDK/应用/模块 revision 或内容标识、dirty
状态、编译器、配置、DTS、layout、编译命令和依赖摘要。资源来自最终 ELF，不能替代
栈水位和目标时序。dependencies.json 记录已有导入的内容摘要，缺失的 upstream
revision 明确保留 unknown，不根据版本号伪造 commit。

```powershell
python tools/sdk.py configure -p demo_foc -B out/demo_foc --profile release
python tools/sdk.py build -B out/demo_foc
python tools/sdk.py inspect -B out/demo_foc --kind resources
python tools/sdk.py test --suite host
python tools/sdk.py test --suite firmware --toolchain-path C:/arm-none-eabi-gcc-9.3.1-1.4
```

test_matrix.json 区分 host、firmware build 和 target acceptance。
samples/osal_contract 在两个内核执行事件组契约后从 UART 输出 OSAL_CONTRACT_PASS。
CLI/CI 只构建，不自动烧录或将构建记为运行通过。samples/control_pipeline 运行
快照/PID/Link 编解码/异步完成/输出 latch，并产生明确标注 host simulation 的
benchmark JSON；gimbal 提供 MCU 参考组合和运行诊断。

目标板验收包括 IRQ 抢占、DMA 缓存/取消、输出关闭延迟、watchdog、满载 deadline
和栈水位、重连，以及 Boot swap/rollback 各阶段断电。生产三合一构建同时要求生产
Boot provider 与 App release profile，不生成密钥或替代产品认证 transport。
gimbal 保留自己的 Boot 布局、产品标识和 loader 配置；三个产品分别构建并打包，
不能跨产品混用 Loader。构建通过后仍须按实际板卡完成 Boot 断电与回滚验收。

## 本次落地验证（2026-11-17）

- Windows + GCC 主机契约：23 项 CTest、59 项工具 Python 测试、29 项 Boot Python 测试。
- 固件矩阵：demo_foc、demo_ble、gimbal，FreeRTOS/RT-Thread、release 和 OSAL 测试应用，
  共 10 项应用配置（含 debug 和各产品 release）；另验证三个产品各自的 preloader、loader、upgrade 和开发三合一镜像。
- 独立应用及外部模块：完整链接、重复配置；错误的 project/find_package 顺序被拒绝。
- 负向配置：缺失缓存 DMA provider、Link 安全 provider、生产 Boot provider 和 profile
  均按预期拒绝。三个产品的原有布局 JSON、产品标识和分区保持一致。
- 本地验证没有烧录或控制目标板；新增 GitHub Actions 矩阵和 MSVC 作业尚未远端执行。
  量产认证、缓存后端、板上性能和电气保护须使用真实产品 provider 与实板验收。

兼容性迁移需关注显式 profile 选择、FaultRecord v4、CAN/BLE 整帧入队语义，
以及独立应用的 CMake 初始化顺序。升级时须同时复核配置、主机解码工具和 ACK 超时预算。

源码维护保留完整厂商 SDK、内核上游包与硬件资源，按配置选择性编译；只清理有明确依据的
重复副本和框架冗余。精简范围与检查命令见 [SDK 源码精简与维护](SOURCE_SUBSET.md)。
