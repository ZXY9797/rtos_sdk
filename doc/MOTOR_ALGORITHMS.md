# 电机算法与产品接入

电机算法位于 `component/motor`，入口为 `<algo/motor.h>`，链接 `sdk::motor`。
`sdk::motor` 公开依赖 `sdk::algo` 的通用原语。表中数学、滤波、控制器、轨迹、
信号和 NTC 来自 `sdk::algo`，FOC、标定和电机计算来自 `sdk::motor`。
实例拥有状态，不进行动态分配、硬件访问、线程创建、日志、通信或 Flash I/O。

| 分类 | 头文件目录 | 内容 |
| --- | --- | --- |
| 数学 | `algo/math/` | 定长空间数学、角度、映射和 Lagrange 插值 |
| 滤波 | `algo/filter/` | Biquad、超前滞后、传递函数、Butter/Cheby2、陷波 |
| 控制器 | `algo/controller/` | PID、Hz 截止积分 PID、一阶/二阶 LESO |
| 轨迹 | `algo/trajectory/` | 运动约束、斜率限制、Hermite 和 S 曲线 |
| 信号 | `algo/signal/` | 扫频生成器 |
| 通用传感器换算 | `algo/sensor/ntc_sensor.h` | NTC 查表 |
| 电机位置 | `algo/sensor/hall_*.h` | 连续双 Hall 角度和标定 |
| FOC | `algo/foc/`、`foc/` | Clarke/Park、SVPWM、电流环和电压调制 |
| 标定 | `algo/calibration/` | TMR、电角度、LUT、记录编解码和背隙辨识 |
| 电机 | `algo/motor/` | 反馈、目标、关节控制、诊断、保护和参数 |

头文件和源文件使用相同分类。`PidController` 使用普通 Ki/Kd，`PidSys` 使用积分截止频率与超前滞后配置，
两者的参数合同不同。LESO 统一使用支持初值播种和输出跟踪的实现。

`demo_foc` 的实际调用链、硬件配置和命令见 [产品说明](../app/product/demo_foc/README.md)。
阻抗、导纳、柔顺/柔性控制的实现状态、单位与后续设计见
[电机控制能力与交互控制设计](MOTOR_CONTROL_DESIGN.md)。

## 参数与单位

通过 `motor_parameters(MotorVariant::..., parameters)` 显式选择；未知枚举返回失败。
预设不是硬件探测结果，也不会自动启用 PWM 或标定。

| 预设 | 极对数 | 减速比 |
| --- | --- | --- |
| Default | 14 | 48 |
| ProfileA | 14 | 8 × 6 / 2.8 |
| ProfileB | 14 | 8 |
| ProfileC | 14 | 48 |
| ProfileD | 14 | 45 |
| ProfileE | 14 | 48 |
| ProfileF | 14 | 64 |

预设涵盖电流环、关节环、标定、保护、方向、通道交换与温度表。电流环 20 kHz，
关节环 4 kHz，慢任务 1 ms。ProfileA 速度环限流为 2.2 A。

关节角 rad，关节速度 rad/s，转子速度单独命名为 `motor_velocity_rad_s`。
`torque_a`、MIT 前馈及控制器输出均是 **q 轴电流 A**。当前 `kt_coefficient` 是元数据，
控制调用链没有用它换算 Nm；必须先明确电机侧/关节侧、峰值/RMS 与传动损耗的约定。
电流环接口始终以 A 为单位。

## 反馈与 FOC

- ADC 先按分辨率、过采样比例/右移和参考电压转成端口电压。硬件换算为
  `(ADC_voltage - offset) * gain`；外部线性标定为 `raw * gain + offset`，顺序不可调换。
  外部有效线性数据要求有限值且 `abs(gain) > 1e-6`，允许负增益。
- `FeedbackProcessor::electrical_sample()` 可在转子尚未标定时单独运行；`rotor_sample()`
  完成输入仿射与电角度校正。`current_sample()` 顺序调用两者，返回两步是否都成功。
  标定任务从原始 ADC 快照取得 TMR 拟合输入，从 `rotor().elec_angle` 取得未校正电角度。
- ProfileA 电流滤波 alpha=0.9，Default 预设为 0.7；Vbus alpha=0.08，首帧直接播种。
  输入 TMR 需有效二维仿射结果，
  atan2 的通道交换来自预设。电角度沿用 112 点表和周期二阶插值，包括周期插值公式的端点处理。
- 转子速度在 4 kHz 差分后 alpha=0.05；输出等效速度含方向与减速比换算。
  输出加速度 alpha=0.01，输出 TMR 角速度 alpha=0.005，输出多圈角 alpha=0.2。
  背隙使用的输入折算角与输出角采用相同多圈角滤波。
- 输出角先做 TMR 校正、LUT，再做 zero、解绕和 offset。零位候选是 LUT 后、旧 zero 前的单圈角；
  offset 候选是旧 offset 前的多圈角，重复标定不会叠加旧参考。上电位于零点负侧、
  单圈角超过 0.95 × 2π 时置 turn_count=-1。LUT view 标记 updating 时暂停关节采样。
- Clarke 保留三相输入和 2/3 系数，Park 符号一致；d/q 双 PI 用`PidSys`。
  正常 CurrentLoop 输入采用滤波后的三相电流与 Vbus；R/L 采样和慢任务母线保护使用未低通的校准值。
  `integrator_cutoff_hz` 是 Hz，积分贡献为 Kp × 2π × cutoff × 积分，不能替换成普通 Ki。
- `CurrentLoopMode` 包含 Current、Voltage、OpenLoop、DirectAlphaBeta、HeldQVoltage。
  OpenLoop 每次快环最多移动 0.02 rad；HeldQVoltage 对应IqOpenLoop，保持已有 q 电压并清 PI，
  不把目标电流解释成电压。零矢量输出 50%；六扇区 SVPWM 在 t1+t2 过限时同比缩放。
  保留占空比反相、`ARR-1` 比较值换算和 ADC 峰前触发偏移换算。
  demo_foc 硬件配置设峰前 1000 ns、dead_time_ns=0；触发比较值可调用 `adc_trigger_count()`
  计算，硬件适配仍须按 PWM 模式、预分频与 ADC 触发通道配置落实这些时序。
- 无效输入、停用或母线不满足条件时 `energize=false`。调用方必须据此关闭全部功率输出，
  不能把“50% 中性占空比”当成硬件断能。

## 标定顺序及事务

`Calibrator` 不等待、不写 Flash；调用 `update(now_ms, coherent_sample, protection)` 推进。
start 要求输出禁用、制动准备完成；电角度标定还要求已有输入仿射数据。
所有预设为 14 极对、112 点；改变极对数时应重新设计并验证表长和扫描关系。

| 阶段 | 实际方案 | ProfileA 参数 |
| --- | --- | --- |
| 输入 TMR | 448 点，电角步进 π/16；累加均值/协方差，求 `(2C)^(-1/2)`，消除中心、幅值差与非正交 | d=2 V，初始对齐 0 ms，步进 10 ms |
| 电角度 | 112 点，步进 π/4；首帧 raw 电角为 zero，以实际施加的开环角计算误差表 | d=3 V，初始对齐 0 ms，步进 100 ms |
| 输出 TMR | 两通道分别记录 min/max，offset=(max+min)/2，amp=(max-min)/2 | d=2 V，初始对齐 0 ms，步进 2 ms |

输入拟合保留协方差行列式 1e-8 门限及有限值检查。输出运动步长 π/6，
运动次数 `round(448 × gear_ratio × 1.2 × (π/16)/(π/6))`；名义采样点数
`round(448 × 1.2)=538`，按运动进度的整数比例选择采样点，每步最多一帧。
输出 TMR 使用极值法，输入 TMR 使用二维仿射拟合。

标定超时为 `initial_align + step_interval × (motion_or_sample_count + 5) + 5000 ms`，
采用无符号时间差，支持计时器回绕。电角度误差必须传入 CurrentLoop 返回的
`applied_electrical_angle_rad`，不能直接用尚未追上的目标相位。

完成采样后进入 `PendingPersistence` 并返回断能意图：

1. 适配层关闭 PWM，保存候选数据，读回并校验记录及 payload。
2. 成功后调用 `complete_persistence(true)`，在快环静止时更新运行快照。
3. 写入/读回失败调用 false，恢复先前数据；取消、超时、保护中止也恢复先前快照。

`AutoCalibration` 对应上电策略：输入仿射、电角度或输出 TMR 任一缺失，便依次运行
**输入 → 电角度 → 输出完整三阶段**，各阶段成功才推进；失败锁存，避免反复带电重试。
复位或数据全部恢复有效后解除失败状态。poll 的 idle、control_disabled、ADC available
必须来自项目真实状态。它不因缺少外部电流/Vbus 线性系数而自动启动。
电流/Vbus 线性系数通过外部采样拟合后下发，不设置独立的自动零电流阶段。

## LUT 与持久化

LUT360 v2 使用 360 个 float32 rad 偏移，单块最多 120 点，线缆按小端浮点编码。
`decode_angle_lut_request()` 检查版本、动作、会话、索引、数量、保留位和消息长度。
`LutSession` 保留所有者/会话隔离、完整位图、重复相同块幂等、冲突块整块拒绝、CRC32、
10 s 空闲超时、取消、读回会话及 Finish 重试。失败写入不能发布；成功 Finish 可重复应答。
**输入 LUT 提供存储/读取功能，转子角计算不应用输入 LUT**；仅输出 LUT 提交后允许发布应用。

反馈通过稳定 view 借用活动输出表，LUT 会话拥有事务暂存表。活动表与暂存表独立时，
读取和分块接收不会覆盖活动数据；输出更新期间用 updating 暂停关节采样。
产品在 PWM 和 ADC 停止后完成写入、读回验证与活动表发布，再恢复采样。
若适配层直接借用暂存表，则必须在覆盖前 detach；跨核同步不能只依赖普通 bool。

MCAL 头为 28 字节：magic u32、version u16、header_size u16、data_type u32、
payload_size u32、payload_crc32 u32、sequence u32、reserved u32，头字段小端。
payload 保留ARM float32 数据布局；跨 ABI 使用前须确认 bool/对齐/端序，不把 C++ 对象任意序列化。
CRC 为 IEEE CRC32（反射多项式 0xEDB88320，初始与最终 XOR 都是 0xFFFFFFFF），
校验范围仅 payload；头按预期类型、版本和长度逐项检查。reserved 写 0，读取沿用兼容策略。

| 记录常量 `calibration_records::` | 类型 / 版本 | demo_foc NVS ID | payload |
| --- | --- | --- | --- |
| current_linear | CURL / 2 | 0x4000 | MotorCurrentLinearCalibData |
| bus_voltage | VBUS / 2 | 0x4001 | MotorLinearCalibData |
| input_tmr | ITMR / 4 | 0x4002 | MotorAffine2DCalibData |
| output_tmr | OTMR / 1 | 0x4003 | MotorTmrPairCalibData |
| electrical_angle | EANG / 1 | 0x4004 | MotorElecAngleCalibData |
| input_angle_lut | IANG / 2 | 0x4005 | MotorAngleLutData |
| output_angle_lut | OANG / 2 | 0x4006 | MotorAngleLutData |
| joint_zero | JZER / 1 | 0x4007 | JointAngleCalibData |
| joint_offset | JOFF / 1 | 0x4008 | JointAngleCalibData |

记录解码先完成全部检查再覆盖输出；读回校验还比较 payload 原始字节。
sequence、存储键、掉电恢复和 NVS 事务由存储适配层管理。HOST/MSN1 等设备身份记录不属于算法标定。

## 控制与目标处理

速度/位置模式沿用位置 PID → 速度 LESO → Iq 链路：位置误差来自输出端滤波多圈角，
速度反馈来自输入端转子机械速度。目标速度经过方向、减速比换算再送 LESO。
一阶/二阶 LESO 的初值、输出跟踪与饱和修正完整保留。速度输出可先单独低通；
最终 Iq 依次经过陷波、低通、限幅；对称限幅值为 0 时表示不启用该限幅。

位置支持线性与 S 曲线和在线目标重规划。S 曲线配置速度/加速度/jerk 约束；
线性模式按速度步进，不能据此宣称具备全程加速度/jerk 约束。速度前馈加在最终速度限幅前。
死区保留进入/退出迟滞和低速稳定计数；进入位置模式先从当前反馈速度减速。
首次使能规划器以实际反馈位置起步，不从零位向目标跳变。可按预设使用外部 torque 指令的绝对值限制位置环 Iq。

MIT 的 Kp/Kd 用完整 uint8 范围 0～255 映射；位置项、速度项分别限幅，再叠加前馈并总限幅。
它提供电流域的基础关节阻抗。ProfileA 映射为 Kp 0～20 A/rad、Kd 0～10 A·s/rad，
不是 Nm/rad 与 Nm·s/rad；位置来自输出 TMR，阻尼反馈速度来自转子折算链。
物理单位阻抗、虚拟质量导纳和任务空间算法由新增 `interaction` / `robotics` 分类提供；原 MIT 的命令单位保持不变。
MIT 位置 Hermite 插值保留段衔接、主机时间戳回绕、重复帧和超时逻辑；
外部名义周期默认 10 ms、超时默认 500 ms，超时会清速度/电流前馈。
其它模式沿用各自目标保持语义，项目仍需保留通信丢失/硬件故障的上层停机策略。

模式切换保留 20 ms smoothstep 电流过渡，力矩/MIT 等待首条新目标时维持既有支撑电流。
`select_mode()` 是数值模式切换，同模式调用幂等。demo_foc 的 ModeTransaction 在项目层分发
SelectMode/Hold/Restore，并区分接收序号和实际过渡完成序号；数值控制器不承担协议重试状态。
`trace()` 提供目标、规划、MIT 分项和等待状态快照，项目在临界区复制，离开临界区后编码发送。
`update(..., false)` 清旧目标，重新使能时不恢复旧 torque 指令。

## 辨识、背隙与保护

R/L：准备 100 ms；电阻四档各 250 ms：+0.25、-0.25、+0.5、-0.5 A；
PI Kp=0.3、Ki=50、周期 50 us，电压 ±1 V。电感先稳定 50 ms，再以 ±0.5 V 激励 400 ms，
每半周期 20 点。绝对电流保护 2 A；每包 20 点，8 包队列，时间戳按采样序号 ×50 us。
队列满终止并返回 Busy，排空最多 2 s，排空超时返回 Timeout；peek/consume 在传输确认后释放包。

电流扫频固定 20 kHz：默认偏置 1 A、幅值 0.3 A、2→2000 Hz、步长 2 Hz、每点 4 周期、
预载 500 ms。幅值上限 0.5 A、总电流 ≤2 A、重复次数 1～20、预载 1～5000 ms、总超时 90 s。
每包 40 点、8 包队列；频点改变先刷出部分包，频点内采样索引重置。队列满计数丢包并继续，
与 R/L 的停止策略不同；停止后排空最多 2 s，保持原结束返回码。

R/L 进入 Drain 后只推进排空与超时；取消、溢出和故障不会重复启动结束流程。

背隙：默认 0.2 rad/s，每速度 1 个正反循环；smoothstep 加速 2 s、匀速稳定 2 s，
每支路采满 2π，Kahan 求输入折算角与输出角差均值，取两方向均值差的绝对值，最后平均有效循环。
减速后固定等待 2 s，总超时 1000 s。软取消保留减速与等待，紧急取消立即结束并断能。

保护保留 bit2 板温、bit3 电机温度、bit4 过压、bit5 欠压及各预设连续触发/恢复次数。
无效采样不能清故障；Vbus 保护使用校准后、显示低通前的值。NTC 用 3.3 V/10 kΩ 分压与
35 点表插值（-20～150 °C）：slot0 板温，slot1/2 取有效电机最高温；电机通道 ≥3.15 V 视为开路跳过。
整体有效需板温有效且至少一个电机有效，连接中的各通道转换均须成功。

## 项目接入与执行约束

每个实例单一所有者。快环、慢任务、协议线程若共用实例，项目提供短临界区或单向快照，
不能直接并发调用。配置、复位、替换标定/参考和 LUT 发布都在控制静止时完成。
快环不做存储、发送、日志或阻塞；诊断采样队列也需要调用方同步，库不自带锁。

执行节拍：20 kHz 采样/FOC/诊断 sample；4 kHz 输出 TMR、速度估计、目标及关节控制；
1 ms 诊断 tick、标定推进、保护去抖和低优先级状态管理。保护的真实调用周期决定去抖时间，
应按产品任务周期配置。`CalibrationSample::valid` 必须表示同步且新鲜的采样，不能把上一帧重复当新帧。

制动适配顺序：正常闭环先建立 PI/PWM 支撑，再请求松闸，Busy 期间维持支撑；
标定、R/L、扫频先完成松闸再施加激励；停止/故障先关闭全部 PWM 再抱闸。
`power_stage_ready`、`brake_ready` 的定义须与所处流程匹配，不用同一个“闸已松开”条件阻断正常闭环支撑。
诊断四个 gate 必须同时成立，并与标定、LUT、背隙、普通控制互斥。故障优先级高于待排空遥测。

最小配置示例（在实例静止时执行；反馈和功率调度由项目持有）：

```cpp
#include <algo/motor.h>

using namespace algo::motor;

MotorParameters parameters;
CurrentLoop current;
JointController joint;
Calibrator calibration;

bool configure_motor_math()
{
    return motor_parameters(MotorVariant::ProfileA, parameters) &&
           current.configure(parameters.current) &&
           joint.configure(parameters.joint) &&
           calibration.configure(parameters.calibration);
}

// 4 kHz: joint.submit(order); joint.update(coherent_feedback, enabled);
// 将返回的 q_current_a 送入 20 kHz CurrentLoopInput::target_current_a.q。
// 仅当各级 valid/energize、采样新鲜且硬件 gate 全部满足时提交 PWM。
```

实例内部有固定大小滤波状态与诊断/LUT 缓存，建议静态或成员持有，避免放在小容量任务栈上。
算法返回成功不证明 ADC 相序、PWM 极性、死区、采样点或制动接线正确。

## 验证

主机测试覆盖数学与 FOC、标定事务、控制环、辨识、保护、协议编码和产品调用链。
目标构建验证生成的设备配置、算法符号、链接与镜像布局。硬件运行还需验证 ADC/PWM 时序、
相序和极性、标定精度、闭环稳定性及中断最坏执行时间。

## 物理交互与机器人计算

已增加 `interaction` 与 `robotics` 分类，物理力矩、阻抗/惯量整形、导纳/拖动、力控/夹持、弹性观测和任务空间控制接入 `demo_foc` 的同一执行链。
单位、信号有效性、限幅/模型、模式 4～14、配置存储与测试边界见 [交互控制设计](MOTOR_CONTROL_DESIGN.md)。
原有七套预设、标定流程和模式 0～3 保留；默认未配置物理 Nm/Iq-A 比例，不能直接进入物理模式。
电流环新增母线电压圆限幅与 PI 实际电压回算，执行监督统一管理电流、热、行程、速度、失联及功率约束。
