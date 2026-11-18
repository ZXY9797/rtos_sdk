# 算法 Algo 组件边界

`component/algo` 是纯算法组件。

头文件与源文件按同一分类组织：`math`、`filter`、`controller`、`trajectory`、`signal`、
`sensor`、`foc`、`calibration`、`motor`、`interaction` 和 `robotics`。统一电机入口为 `<algo/motor.h>`，链接 `sdk::algo`。

## 允许内容

- 定长向量、矩阵、四元数和空间旋转运算。
- Biquad、Lead-Lag、插值、PID、LESO、NTC 查表和扫频信号。
- 与机构无关的单轴限速、限加速度和限加加速度轨迹原语。
- `algo::motor` 的单轴电机数学模型、FOC、标定与控制状态计算，见
  [电机算法与接入说明](../../doc/MOTOR_ALGORITHMS.md)。
- 阻抗/导纳等能力的现状与设计边界见
  [电机交互控制](../../doc/MOTOR_CONTROL_DESIGN.md)，统一入口 `<algo/interaction.h>` / `<algo/robotics.h>`。
- 只依赖标准 C/C++ 头文件。

## 禁止内容

- 不依赖 OSAL。
- 不包含驱动、DTS、bootloader 或产品应用头文件。
- 不进行日志输出、线程创建或硬件访问。
- 不定义产品主题、设备通道或云台状态机，不决定任务/中断的执行调度。
  算法采样周期可以作为数值参数，调用方负责使它与实际执行频率一致。

完整姿态 EKF、非正交运动学、三轴姿态规划和绑定设备的闭环执行器属于功能组件，
只复用这里的计算原语。`algo::motor` 仅返回计算结果和驱动意图；PWM、制动、采样调度、
协议、存储与故障停机由功能组件或项目负责。电机参数预设显式选择，不自动配置产品。
