# 组件分层说明

`component/` 放置可复用组件。组件不能依赖具体产品应用，也不能依赖
bootloader 私有实现。

## 依赖等级

| 组件 | 职责 | 允许依赖 | 禁止依赖 |
|:---|:---|:---|:---|
| `algo` | 数学、滤波、PID/LESO、轨迹、姿态 EKF、NTC 与温控 | 标准 C/C++ 头文件 | 电机领域、OSAL、driver、DTS、产品代码 |
| `motor` | 电机与关节控制、Hall 角度和标定、FOC 与 SVPWM | `sdk::algo`、标准 C/C++ 头文件 | OSAL、driver、DTS、产品代码 |
| `motor/drive` | 电压/电流 FOC 与 PWM、ADC 及 OSAL 的设备适配 | `sdk::motor`、驱动抽象、OSAL 与平台支持 | 产品 CLI、boot 状态、产品启动流程 |
| `ipc` | 固定容量快照与 SPSC 原语 | 标准原子类型 | OSAL、产品主题集合 |
| `link` | 多链路通信服务 | OSAL、驱动抽象 | 具体产品应用 |
| `nvs` | 非易失键值存储 | OSAL、Flash 驱动抽象 | 具体产品应用 |
| `ble/goodix` | Goodix BLE 适配 | Goodix SDK、公共 `ble::` API | 向产品层暴露厂商细节 |

## 复用传感器与温控算法

固件通过下列 Kconfig 开关选择实现，主机工程直接链接对应目标即可。

| Kconfig | 链接目标 | 公开头文件 | 输入与输出 |
|:---|:---|:---|:---|
| `ATTITUDE_EKF` | `sdk::algo` | `<algo/attitude/attitude_ekf.h>` | 传感器坐标系下的角速度、加速度、温度与时标，输出机体系到世界系的姿态和估计质量 |
| `POSITION_SENSOR` | `sdk::motor` | `<algo/sensor/hall_sensor.h>`、`<algo/sensor/hall_calibration.h>` | 双 Hall 采样和角度参考，输出角度、速度与标定结果 |
| `THERMAL_CONTROL` | `sdk::algo` | `<algo/thermal/thermal_controller.h>` | 温度与采样周期，输出占空比请求、就绪状态和故障 |

产品负责传感器采集、参数持久化、策略编排和实际输出授权。云台的三轴模型、主题类型和
整机安全监督位于 `app/product/gimbal`。

## 设计约束

- 组件公开头文件应保持产品无关。
- 组件中需要硬件能力时，应依赖驱动抽象或由应用注入设备对象。
- 产品策略、启动顺序、命令行交互、产品参数默认值应放在 `app/product`。
- 第三方 SDK 只在适配组件内部扩散，不能穿透到应用层。
- 组件按独立 CMake target 和 Kconfig symbol 选择；禁止重新引入按产品命名的
  聚合静态库。产品参数 schema 和主题拓扑必须留在 `app/product`。
