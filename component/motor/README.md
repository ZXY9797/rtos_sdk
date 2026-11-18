# 电机组件

`sdk::motor` 提供电机和关节控制、反馈、标定、Hall 角度估计、参数辨识与
FOC 计算，依赖 `sdk::algo` 的通用数学、滤波和控制原语。主机工程可直接链接
`sdk::motor`。已有 `<algo/motor.h>`、`<algo/foc/...>` 和纯计算 `<foc/...>`
头文件继续使用原来的包含路径，由 `sdk::motor` 提供。

`drive/` 通过 `foc` 目标提供设备适配，负责调用 PWM、ADC 和 OSAL。`CONFIG_FOC`
启用设备适配，`FOC_VOLTAGE_CONTROL` 和 `FOC_CURRENT_CONTROL` 分别选择电压和
相电流设备实现。固件用 `POSITION_SENSOR` 选择连续双 Hall 算法，电流设备专用的
估计器、HFI 和参数测量随 `FOC_CURRENT_CONTROL` 编译。

## SVPWM 接口

- 扇区式 SVPWM 返回扇区和相对 PWM 周期的比较时刻，由 `demo_foc` 的 `CurrentLoop` 使用。
- `foc::Svpwm` 提供矢量限幅、零序注入和三相占空比，由 `foc::VoltageFoc` 和 `foc::Motor` 使用。

应用负责设备映射、任务编排、参数持久化和产品级保护策略。调用关系见
[电机算法](../../doc/MOTOR_ALGORITHMS.md)和 [FOC 设备接入](../../doc/FOC.md)。
