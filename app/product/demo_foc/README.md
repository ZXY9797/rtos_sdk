# demo_foc

单电机 FOC 应用，使用 `sdk::algo` 中的标定、反馈、关节控制、FOC、辨识及保护算法。
项目层负责采样、PWM 提交、任务调度、命令分发和持久化。控制器和拟合算法只实现一份。

## 调用链

```text
TIMER0 CH3 → ADC0 六通道扫描 → DMA 半满/全满 IRQ（20 kHz）
  → ADC 电压换算 → FeedbackProcessor → CurrentLoop → 六路 PWM

TIMER7 TRGO → ADC1 双通道、16 倍累加 → DMA IRQ（4 kHz）
  → 输出 TMR / 转子速度反馈 → JointController → Iq 目标

1 ms 任务 → 温度 / 母线保护 → Calibrator / AutoCalibration
          → 命令队列 → 已验证的标定提交 → 辨识波形队列发送
```

模式包含力矩、速度、位置和 MIT。位置可切换线性或 S 曲线规划；输入/输出 TMR、电角度、
零位、offset、线性电流/Vbus、LUT360 均有命令入口。R/L、电流扫频和背隙具有独立互斥流程。
参数通过 `MotorVariant::ProfileA` 加载：14 极对、减速比 `8×6/2.8`，速度环限流 2.2 A。
默认关闭自动标定；可显式发起完整流程，或启用 `CONFIG_MOTOR_AUTO_CALIBRATION`。

## 控制能力边界

MIT 是以关节位置/速度误差产生 Iq 的基础阻抗路径，具备弹簧、阻尼和电流前馈。
当前控制单位是 A，`torque_a` 不表示 Nm。Kp/Kd 的映射结果分别为 A/rad、A·s/rad。
位置反馈来自输出 TMR，外环速度来自转子角差分后按减速比/方向折算；不是同一传感器链。

新增物理模式使用独立的 `JointInteraction` / `CartesianController<1>`，共用原电流执行链；详见本文末尾的物理交互控制入口。
背隙辨识用于测量间隙，不代表已经具备弹性力矩估计或背隙补偿。
Linear 是默认位置规划模式，不能代替 S 曲线的加速度/jerk 约束。
MIT 目标超时会清速度/电流前馈，但仍保留位置与增益，不等于断能。
各模式失联后的策略、物理力矩换算及交互控制扩展见
[控制能力与设计](../../../doc/MOTOR_CONTROL_DESIGN.md)。

## 硬件与配置

硬件完整描述位于 `config/board.dts`，应用选择和时序位于 `config/app.overlay`，
引脚操作位于 `config/pinctrl.dtsi`。Boot 镜像配置位于 `boot/<image>/config/`，共用项目 DTS。

| 用途 | 资源 / 顺序 |
| --- | --- |
| PWM | TIMER0，PA8/PA9/PA10，互补 PB13/PB14/PB15，中心对齐 20 kHz |
| 电流与转子 | ADC0：Ch2/Iw、Ch1/Iv、Ch0/Iu、Ch4/Vbus、Ch8/TMR1、Ch9/TMR2 |
| 输出 TMR | ADC1：Ch17/PB11、Ch7/PA7；16 倍累加、右移 0 |
| 温度 | ADC2：Ch15/PB10 板温、Ch13/PC3 与 Ch14/PE15 电机温度 |
| ADC 触发 | TIMER0 CH3 峰前 1000 ns；TIMER7 更新 4 kHz |
| DMA | DMA0 Ch0 电流，Ch1 输出 TMR；DMA1 Ch4 UART TX |
| UART | USART0，PB6/PB7；APP 2 Mbaud，Loader 115200 baud |
| CAN | CAN0，PA11/PA12，经典 CAN 500 kbit/s |
| 状态灯 | PE0 |

本产品没有制动器输出。六路 PWM 的开关统一由功率接口负责，关闭输出时采样定时器仍可运行。
ADC 的两个完整帧交替移交，禁止在扫描中穿插同步单通道读取。DMA 错误或两帧同时积压时，
停止对应扫描并触发停机。快环和关节环无阻塞、日志、Flash 或通信操作。

## 通信

统一使用 [Link v1](../../../component/link/README.md)，设备地址 `0x10`、命令集 `0x30`。
UART 是独占二进制通道，主机地址 `0x20..0x2F`；CAN 主机地址 `0x30..0x3F`，CAN ID `0x100`，
使用 Link 经典 CAN 分片。日志也封装为 Link 帧，发送给 UART 地址 `0x20`，无 ASCII 命令线程。

命令负载中的整数和 IEEE float32 均为小端。变更命令建议使用 `AckMode::Finish`；
ACK 表示命令已处理，标定和辨识的最终状态通过 Status 查询。0 表示成功，2 表示参数或当前状态不允许，
1 保留为 Link 的进行中状态，不能作为最终失败码。
队列满返回 Link `0xFC`。命令共用四项固定队列，由 1 ms 任务串行执行。

| ID | 命令 | 负载 |
| --- | --- | --- |
| 0x00 | Status | 空 |
| 0x01 | Enable | u8：0 禁用，1 使能 |
| 0x02 | Mode | u8：0 力矩，1 速度，2 位置，3 MIT |
| 0x03 | Target | position f32、velocity f32、Iq f32、Kp u8、Kd u8、主机时间戳 us/u32 |
| 0x04 | Calibrate | u8：0 完整流程，3 输入 TMR，4 电角度，5 输出 TMR |
| 0x05 | Stop | u8：0 普通停止，1 紧急停止；背隙普通停止会完成减速 |
| 0x06 | ClearFault | 空；仅在空闲且保护恢复后接受 |
| 0x07 | Backlash | 空 |
| 0x08 | Stream | u8：1 订阅，0 取消；接收者使用请求发送方地址 |
| 0x09 | RL | 空 |
| 0x0A | Sweep | 空使用默认值；或 bias/amplitude/start/end/step 五个 f32、repeat u8、preload_ms u32 |
| 0x0B | LUT | LUT360 v2 消息，见算法文档 |
| 0x0C | JointZero | 空 |
| 0x0D | JointOffset | 空 |
| 0x0E | CurrentLinear | U/V/W 各 offset、gain 两个 f32，共 24 字节 |
| 0x0F | BusLinear | offset、gain 两个 f32 |
| 0x10 | Plan | u8：0 S 曲线，1 线性；空闲时配置 |
| 0x11 | CalibrationData | operation u8、record u8、offset u16、count u16；0 读取，1 清除 |
| 0x12 | Observe | kind u8：0 原始/校正传感器，1 控制链，2 耗时 |
| 0x13 | ModeTransaction | 固定 32 字节，带应用序号的 Target/Hold/Restore/Disable/SelectMode |
| 0x14 | ModeStatus | 空；读取已接收/已应用序号、模式、锁存和故障 |
| 0x15 | Telemetry | enable u8、kind u8、period_ms u16；UART，周期至少 5 ms |
| 0x16 | Capture | enable u8、divider u16；UART，关节采样分频至少 4 |

Target 的角度为关节侧 rad，速度 rad/s，Iq 单位 A。Kp/Kd 为 0～255 映射值。
操作顺序为选择 Mode、Enable、发送 Target；使能会清除停机前的旧目标。
模式切换后等待新目标，保留 20 ms 电流过渡。位置和速度反馈使用不同传感器链，不能互换。

标定、LUT、辨识、背隙与普通闭环互斥。完整标定依次执行输入 TMR → 电角度 → 输出 TMR，
每阶段写入并校验后才进入下一阶段。失败不发布候选；原有有效数据继续保留。
输入 TMR 标定期间不要求已有转子角校正；电角度阶段要求有效转子反馈。

### 标定参数管理

CalibrationData 的 record 为 0～8：电流线性、Vbus、输入 TMR、输出 TMR、电角度、输入 LUT、
输出 LUT、零位、offset。清除全部使用 `record=0xFF`，offset/count 必须为 0。
清除只能在空闲时执行，停 PWM/ADC 后移除 NVS 记录并确认不存在，再清除运行参数；失败不发布该项。
多项清除逐条提交，遇错即停，已清除项保持清除。清除后关闭自动标定，需要显式重新发起。

读取普通参数返回当前已发布数据；每次写入的 ACK 均在 Flash 读回验证及运行参数发布后返回。
LUT 的 CalibrationData 查询返回存储内容，多页一致性读取使用 LUT v2 的 ReadBegin/Read/Abort。
响应头为 `version=1 u8、record u8、valid_mask u8、reserved=0 u8、offset u16、count u16、total u16`，
后接 count 个 f32。offset/count 按 float 元素计数，单次最多 120，count=0 自动选择剩余的最多 120 项。

| record | float 顺序 | valid_mask |
| --- | --- | --- |
| 0 | U/V/W 的 offset、gain，共 6 项 | bit0/1/2 对应 U/V/W |
| 1 | offset、gain | bit0 |
| 2 | center0/1、transform00/01/10/11 | bit0 |
| 3 | TMR1 offset/amp、TMR2 offset/amp | bit0/1 |
| 4 | zero_offset、112 个电角度误差 | bit0 |
| 5/6 | 360 个角度误差，rad | bit0 |
| 7/8 | offset | bit0 |

### 模式事务

ModeTransaction 的小端布局为：version=1/u8、action/u8、mode/u8、enabled/u8、sequence/u32，
position/velocity/Iq 各 f32，Kp/Kd 各 u8，reserved/u16=0，host_timestamp_us/u32，reserved/u32=0。
action 依次为 0 Target、1 Hold、2 Restore、3 Disable、4 SelectMode。
Hold 必须为使能的位置模式；Restore 携带恢复后的完整模式与目标，不隐式恢复历史目标。
SelectMode 只选择模式，其 enabled 必须与当前状态一致。
Disable 只停机，不切换数值模式，也不要求传感器反馈有效。

应用序号采用 uint32 回绕比较，重复或旧序号不重启过渡。接收成功仅更新 accepted_sequence；
过渡完成且电流环已经输出，才更新 applied_sequence。Disable 在确认不输出后完成，即使故障仍存在，
也保留故障位。Hold 锁存期间拒绝普通 Target/SelectMode，Restore 完成后才解除锁存。
本地 Stop/故障打断事务后不会确认先前的使能请求，必须先显式 Disable，再清故障并重新下发目标。
使用过事务后，旧 Enable/Mode/Target 的使能写入被拒绝，Stop 和禁用始终可用。

ModeStatus 响应固定 16 字节：version=1/u8、mode/u8、flags/u8、reserved/u8=0，
accepted_sequence、applied_sequence、faults 各 u32。
flags bit0～3 为 enabled、holding、busy、locally_disabled。faults bit0 为硬件故障，保护沿用 bit2～5。

### 观测与配对采集

Observe/Telemetry 共用编码，Telemetry 命令 ID 为 `0x84`，配对采集为 `0x85`。
观测公共头 20 字节：version=1/u8、kind/u8、flags/u16、fast_sequence/u32、joint_sequence/u32、
input_us/u32、joint_us/u32。flags bit0 为温度有效，bit1 目标有效，bit2 等待新目标，bit8/9 为当前模式。

- kind0 共 104 字节：头后为 U/V/W 原始电流、校正电流、滤波电流，原始/校正/滤波 Vbus，
  输入 TMR1/2、输出 TMR1/2 电压，输入机械角、电角度、输出原始角、板温、电机温度，均为 f32。
- kind1 共 108 字节：头后先放 host_timestamp_us/u32，再放 21 个 f32：最终目标 position/velocity/Iq/Kp/Kd，
  规划位置/速度、位置误差、速度观察器输出、MIT 位置/速度分项、FOC 目标 Iq、关节方向反馈 Iq，
  FOC 实测 Id/Iq、Ud/Uq、实际位置/速度、转子速度、目标年龄秒。FOC 电流保留原坐标，关节反馈应用方向系数。
  其中目标 Kp/Kd 为 0～255 原始码，未换算为物理刚度/阻尼。
- kind2 共 84 字节：头后放 timestamp_hz/u32=1000000，再依次放电流 IRQ、关节 IRQ、慢任务的
  count、last_us、maximum_us、overruns、maximum_interval_us，各 u32；预算依次为 50/250/1000 us。

kind0 当前只包含汇总板温和电机温度，没有完整三通道 NTC 电压及有效掩码。
kind2 软件耗时不包含完整 IRQ 进出栈和采样到回调的延迟，仍需独立测量最坏执行时序。

CAN 接收任务识别标准 ID `0x680..0x68F` 的三字节大端编码器值，掩码为 21 bit。
同一总线只接一个外部编码器；ID 低四位用于兼容设备编号，不隔离多个采样源。
复用 Link 的唯一 CAN FIFO 接收者，外部编码器帧不会进入 Link 分片重组。
关节 IRQ 按 divider 生成配对快照：version=1/u8、flags/u8、reserved/u16=0、timestamp_hz/u32=1000000，
pair_sequence、joint_sequence、joint_us、input_us、encoder_sequence、encoder_processed_us、
encoder_raw_count、dropped 各 u32，再接 input_angle、output_raw_angle、output_calibrated_angle 三个 f32，
共 52 字节。flags bit0/1/2 为编码器存在、新编码器序号、输入角有效。
output_raw_angle 已做 TMR offset/幅值归一化，尚未应用 LUT；output_calibrated_angle 已应用 LUT。
两者均未扣除关节零位/offset，适用于比较传感器本身的角度误差。

这些时间戳是软件观察时刻，编码器时间是 CAN 任务处理时刻，不代表编码器物理采样时刻。
主机应结合序号、flags、模 2^32 的年龄判断数据是否适合拟合。新序号标志不等于时延合格。
16 项固定队列满时丢弃新快照并累计 dropped，发送成功后才消费；序号缺口和 dropped 都可用于识别丢包。
UART 订阅有单主机所有权，分频最小 4，默认建议 20（200 Hz）。

R/L 和电流扫频固定采样 20 kHz，必须先由 UART 主机订阅 Stream。
经典 CAN 只承担控制与查询，不接受高速波形订阅。波形命令为 `0x81`/R/L、`0x82`/Sweep，
日志为 `0x83`。R/L 为每包 20 点，扫频每包 40 点，分包字段使用固定小端编码。
发送成功后才消费算法队列；R/L 队列满终止，扫频队列满计数丢包。

Status v1 固定 49 字节：version u8、flags u8、calibration_state u8、backlash_ret u8、
protection_mask u32；position、velocity、Iq、Vbus、backlash 五个 f32；
rl_finished、rl_ret、sweep_finished、sweep_ret 四个 u8；dropped_rl/dropped_sweep 两个 u32；
calibration_kind u8、sample_count u32、motion_count u32。
flags bit0～7 依次为 enabled、ready、hardware_fault、diagnostic_active、backlash_active、
backlash_finished、backlash_result_valid、auto_calibration_failed。

## 存储与执行边界

`CalibrationStore` 使用四个 NVS 扇区，记录 ID `0x4000..0x4008`，每条带 MCAL 类型、版本、长度和 CRC32。
写入前停止 PWM 和 ADC DMA；写入、读回比较、发布结果后恢复采样。Flash 操作和 LUT 大块处理不屏蔽中断。
LUT 使用一个活动输出表和事务暂存表，成功写回后才发布；输入 LUT 可存取，转子角不应用输入 LUT。

配置、命令状态变更和共享快照使用短临界区。关节状态更新整体串行执行，Iq 在计算完成后发布；
电流 IRQ 在临界区外优先处理。临界区执行时间也必须纳入快环的最坏延迟测量。
1 ms 任务运行失期或采样超过 2 ms 未更新时，运动流程进入故障停机。全局故障钩子直接关闭所有 PWM。
本地 ClearFault 不清除全局安全锁存。

## 构建与验证

```powershell
cmake -S . -B out/demo_foc -G Ninja -Dp=demo_foc -Dt=armgcc9 `
  -DARMGCC9_TOOLCHAIN_PATH=C:/arm-none-eabi-gcc-9.3.1-1.4
cmake --build out/demo_foc
python bootloader/scripts/build_3in1.py demo_foc --out out/demo_foc_3in1 `
  --toolchain-path C:/arm-none-eabi-gcc-9.3.1-1.4
```

主机测试包含产品模式互斥、标定提交/失败回滚、传感器故障、协议编码和 ADC DMA 双缓冲。
构建与主机测试不替代板上 ADC/PWM 时序、标定精度、闭环稳定性、最坏执行时间和 UART 持续吞吐验证。
算法与记录格式详见 [电机算法](../../../doc/MOTOR_ALGORITHMS.md)。
串口参数管理、配对 CSV 导出及 LUT CSV 读写见 [主机工具](../../../tools/demo_foc/README.md)。

## 物理交互控制

模式 4～14 提供物理力矩、阻抗/惯量整形、导纳、拖动、力控、夹持、弹性关节与单关节任务空间控制。
算法/坐标/有效性契约见 [交互控制设计](../../../doc/MOTOR_CONTROL_DESIGN.md)，新增命令与标定操作见 [交互工具](../../../tools/demo_foc/INTERACTION.md)。
原 Link v1 帧和模式 0～3 保留，扩展模式事务使用 body v2；独立配置记录为 NVS `0x4100`。
默认物理力矩比例未配置，物理模式拒绝启动；外力输入为带序号和本地有效期的 Link 测量接口。
普通控制现在默认有 500 ms 命令期限，主机必须持续发送新应用序号/时间戳。重复帧不会续期。
新增过流/I²t、速度、温度失效与降额默认值；行程、功率和跟随误差需按机构显式配置。Observe kind=3 提供各 NTC 电压、温度和 masks。
GNU 产品启用局部 LTO/体积优化，初始化和慢服务各用 8 KiB 栈；最终包仍受原升级槽限制。
