# 电机控制 FOC 组件边界

`component/foc` 是电机控制领域组件，负责统一的 SVPWM、dq 电压调制、相电流
闭环 FOC、电机模型、测量和角度估计。电压模式与电流模式共享同一调制内核，
不会维护两套限幅和零序注入算法。

## 组件内职责

- `CONFIG_FOC_VOLTAGE_CONTROL`：无相电流采样时的有界电压 FOC 和三相 PWM 适配。
- `CONFIG_FOC_CURRENT_CONTROL`：依赖 `foc,motor` DTS 的电流环、速度环和观测器。
- 两种模式公共的线性区矢量限幅、零序注入与三相占空比生成。
- 电流环、速度/位置相关的领域对象。
- 电机参数测量和控制计算。
- 与驱动抽象协作的电机运行接口。

## 组件外职责

- 产品启动顺序放在 `app/product`。
- CLI、CAN 命令、产品诊断和日志策略放在 `app/product`。
- 产品级默认参数优先来自 DTS、Kconfig 或产品配置。
- bootloader 状态和镜像确认不进入 FOC 组件。

`CONFIG_FOC=y` 只打开领域组件；具体源文件由两个子模式选择。只启用电压模式时，
静态库仅包含 `svpwm.cc`、`voltage_foc.cc` 和 `voltage_motor.cc`，不会链接电流
采样、HFI、磁链观测器或电机参数测量代码。
