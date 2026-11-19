# 物理交互控制工具与协议

`interaction_tool.py` 使用现有 `motor_tool.Client` 的 Link v1 编解码、ACK 匹配和单次写入策略。离线测试只需 Python 标准库；串口操作另需 pyserial。
所有示例为人工执行步骤，本工具不自动连接或激励电机。算法/坐标/保护条件见 [设计说明](../../doc/MOTOR_CONTROL_DESIGN.md)。

## 配置步骤

1. 完成电流、Vbus、TMR、电角度与输出零位标定，台架确认 ADC/PWM、方向与关断路径。
2. 保持电机停止，读取 `limits` 并设置机构行程、速度、温度、热容量、功率和跟随误差。
3. 对照读回值与独立传感器检查配置。运行目标与命令时效由 [CANopen FD 接口](../../app/product/demo_foc/doc/md007_v1.md#canopen-fd-接口) 管理。

```powershell
python tools/demo_foc/interaction_tool.py --port COM7 config-read limits
python tools/demo_foc/interaction_tool.py --port COM7 config-read joint
python tools/demo_foc/interaction_tool.py --port COM7 config-write limits limits_patch.json
python tools/demo_foc/interaction_tool.py --port COM7 status
python tools/demo_foc/motor_tool.py --port COM7 observe temperature
```

`config-write` 的 JSON 只填写要修改的字段；工具先读现值、合并、发送一次写入，再读回逐字节比较。校验不符报错，不重试写入。
每次分区修改会保存完整配置，要求空闲状态；多次分区修改之间不是原子事务。默认力矩比例为 0，尚未调试完时保持该值可以阻止物理模式启动。

### 字段分区

字段顺序同时用于固件协议和主机 `SECTIONS`。全为 LE float32；bool 严格编码为 0/1。

| ID/名称 | 字段数 | 顺序 |
| --- | --- | --- |
| 0 limits | 18 | phase_current_a, continuous_current_a, excess_i2t_a2_s, maximum_velocity_rad_s, current_slew_a_s, command_timeout_s, temperature_timeout_s, minimum_position_rad, maximum_position_rad, hard_limit_margin_rad, following_error_rad, following_time_s, motoring_power_w, regenerative_power_w, derate_start_c, derate_stop_c, position_limits, mechanical_power |
| 1 joint | 14 | joint_nm_per_iq_a, bias_nm, servo_stiffness, servo_damping, stiffness_rate, damping_rate, torque_limit_nm, torque_rate_nm_s, initial_energy_j, maximum_energy_j, external_timeout_s, use_energy_tank, use_model_external, desired_inertia |
| 2 admittance | 8 | inertia, damping, stiffness, position_limit, velocity_limit, acceleration_limit, jerk_limit, force_deadband |
| 3 compensation | 11 | gravity_bias_nm, gravity_amplitude_nm, gravity_phase_rad, viscous_nm_s, coulomb_nm, breakaway_nm, stribeck_velocity, sign_velocity, backlash_rad, backlash_rate_rad_s, load_inertia |
| 4 elastic | 6 | stiffness_nm_rad, damping_nm_s, zero_rad, backlash_rad, observer_alpha, resonance_damping |
| 5 force | 13 | kp, ki, maximum_nm, rate_nm_s, contact_n, release_n, contact_time_s, lever_arm_m, approach_velocity, release_distance, maximum_travel_rad, approach_timeout_s, closing_direction |
| 6 electrical | 5 | resistance, ld, lq, flux, decoupling |
| 7 cartesian | 41 | length_m, mass_kg, com_m, inertia_kg_m2；随后 stiffness/damping/force_kp/force_ki/wrench_limit 各按 xyzXYZ 六项；force_axis_xyzXYZ 六项；dynamics_feedforward |
| 8 cartesian_admittance | 48 | 依 xyzXYZ 六轴，每轴依分区 2 的八字段顺序 |
| 9 cogging | 64 | nm_0 ... nm_63，输出关节每 2π 周期的力矩补偿点 |

小写 xyz 为平移，大写 XYZ 为旋转。关节长度 rad、力矩 Nm、电流 A、功率 W、时间 s；直线长度 m、力 N；惯量 kg·m²，电气 R/Ω、L/H、磁链/Wb。
弹性 `observer_alpha` 在 4 kHz 每次更新，改频率时须重新选滤波系数。力臂只支持固定接触力臂，机构的实际力臂若随位置变化，应由上层提供正确转换后的传递力矩。

### 力矩标定

先停止并执行 `torque-cal begin`，随后通过 CANopen FD CST 输入多个正反向稳态目标。
每个稳定工作点用独立输出力矩测量采点。

```powershell
python tools/demo_foc/interaction_tool.py --port COM7 torque-cal begin
# 由 CANopen FD 主站设置 CST 目标并保持命令更新。
python tools/demo_foc/interaction_tool.py --port COM7 torque-cal sample --measured-nm <实测力矩>
# 采集至少三个具有足够电流跨度的点后停止电机。
python tools/demo_foc/motor_tool.py --port COM7 stop --emergency
python tools/demo_foc/interaction_tool.py --port COM7 torque-cal finish
```

`sample` 取执行时最新原始 FOC Iq，不是同步力传感器采样。只用于稳定工作点；不能对运动/异步测量直接拟合。
`finish` 在电机停止时拟合并持久化，通过 RMS 门限仍需检查斜率、方向、负载与可重复性。应用前需明确执行器力矩和传感器去皮偏差的区别。

### R/L 辨识

```powershell
python tools/demo_foc/interaction_tool.py --port COM7 identify
python tools/demo_foc/interaction_tool.py --port COM7 rl-fit
# 核验结果和有效采流/Vbus 标定后，显式提交：
python tools/demo_foc/interaction_tool.py --port COM7 rl-fit --apply
```

`identify` 订阅并排空 R/L 数据，轮询结束；超时会发送一次停止作为清理。发送失败不重试激励命令。
默认只返回拟合值。`--apply` 将 R 和该锁定方向的有效 L 写入模型，Ld=Lq，解耦仍关闭；不会假定已测得凸极参数或磁链。

### 外力测量

外力输入 API 为 `external_packet(sequence, external, effort, force, wrench, timestamp)`；None 表示该信号无效，0 表示有效的零测量。
CLI `external` 省略序号时先读取当前序号加一；持续测量生产者应显式维护 uint32 序号和采样时序。传感器停止时发送全无效包；旧/重复序号不会刷新有效期。
单一测量源管理序号；多个主机不能分别以互不协调的计数器覆盖它。时间戳只作主机元信息，期限使用设备本地到达时间，不能证明采样同步。

## 协议表

Link 帧版本仍为 1，命令集 0x30。以下长度均不含 ACK 首字节；除另述外保留位为 0。

| 命令 | 请求 | 应答 |
| --- | --- | --- |
| 0x14 ModeStatus | 空 | 16 B；version=1 对应模式 0～3，version=2 对应 4～14；其余 flags/accepted/applied/faults 不变 |
| 0x17 InteractionConfig | 读 `[op=0, section]`；写 `[op=1, section_packet...]` | 读返回 section_packet；写在校验/持久化/生效后完成 |
| 0x18 ExternalFeedback | 48 B，格式如下 | 空 |
| 0x19 InteractionStatus | 空 | 132 B，格式如下 |
| 0x1A ElectricalModel | `op u8`，0 读/1 提交 | version u8, valid u8, reserved u16, samples u32, R/L/biasV/rmsV 四 f32，24 B |
| 0x1B TorqueCalibration | 0 begin；1 + measuredNm f32；2 finish | k, biasNm, rmsNm 三 f32，12 B；finish 同时持久化 |
| 0x12 Observe / 0x15 Telemetry | kind 新增 3 | 通用 20 B 头 + present_mask u8, valid_mask u8, reserved u16, voltage[3] f32, temperature[3] f32，共 48 B |

`section_packet = version u8(1), section u8, field_count u16, fields[field_count] f32`。
完整配置使用 `version=1, sections=10, reserved u16` 头，按 ID 顺序拼接十个 section_packet；当前 payload 956 B，外包 MCAL CRC32 记录。

ExternalFeedback：version u8=1，flags u8，reserved u16，sequence u32，host_timestamp_us u32，externalNm/effortNm/forceN 三 f32，worldWrench[6] f32。
flags bit0/1/2/3 分别表示上述三个标量和 wrench 有效。设备本地到达后超过 `external_timeout_s` 自动失效。

InteractionStatus：

- 0～7：version u8=1、grip u8、flags u8（valid/energize/limited）、reserved u8、execution_faults u32。
- 8～67：torqueNm, requestedIq, referenceQ, referenceV, externalEstimateNm, elasticEffortNm, energyJ, K, D, appliedIq, currentLimit, commandAge, invalidTemperatureAge, excessI2t, electricalPower，共 15 f32。
- 68～71：cartesianValid u8、reserved[3]。
- 72～119：taskError[6], taskCommandWrench[6]，共 12 f32。
- 120～131：externalSequence u32、externalAge f32、externalFlags u8、reserved[3]。

execution_faults bit0～9 分别是相过流、超速、硬行程、跟随误差、命令超时、温度失效、I²t、外部信号、不可实现的功率/能量预算、夹持控制器故障。
旧 Status/ModeStatus 把这些位左移 6 位与原故障合并。既有温度/母线保护位不变。
旧式 Observe kind=1 的控制目标字段用于模式 0～3；物理控制请读取 InteractionStatus。扩展模式的 Observe 头 version=2，模式号使用 flags[11:8]。

## 检查

```powershell
python -m unittest discover -s tools/demo_foc
cmake -S tests -B out/host -G Ninja
cmake --build out/host
ctest --test-dir out/host --output-on-failure
```

串口读回、主机仿真与固件编译不能证明真实力矩、接触安全或 ADC/PWM 最坏时延。上板核验项目见设计说明。
