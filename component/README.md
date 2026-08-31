# 组件分层说明

`component/` 放置可复用组件。组件不能依赖具体产品应用，也不能依赖
bootloader 私有实现。

## 依赖等级

| 组件 | 职责 | 允许依赖 | 禁止依赖 |
|:---|:---|:---|:---|
| `algo` | 数学、DSP、估计和单轴轨迹原语 | 标准 C/C++ 头文件 | OSAL、driver、DTS、产品代码 |
| `control_contracts` | 定长领域数据契约 | `algo` 定长数值类型 | 任务、驱动、产品策略 |
| `motion` | 非正交运动学、动力学和三轴规划 | `algo`、`control_contracts` | driver、OSAL、产品代码 |
| `attitude` | IMU 姿态 EKF | `algo`、`control_contracts` | driver、产品代码 |
| `control` | 频率整形和 2DOF 控制 | `algo`、`motion`、`control_contracts` | driver、产品代码 |
| `position_sensor` | 连续 Hall 估计和标定 | `algo`、`control_contracts`、ADC 抽象 | 产品参数存储、任务编排 |
| `thermal` | 恒温状态机和调节器 | `algo`、`control_contracts` | PWM 设备、产品代码 |
| `safety` | 故障检测、锁存和授权状态机 | `control_contracts` | 硬件写入、Flash、产品任务 |
| `foc` | 电压/电流 FOC 与统一 SVPWM | `algo`、驱动抽象、OSAL | 产品 CLI、boot 状态、产品启动流程 |
| `ipc` | 固定容量快照与 SPSC 原语 | 标准原子类型 | OSAL、产品主题集合 |
| `link` | 多链路通信服务 | OSAL、驱动抽象 | 具体产品应用 |
| `nvs` | 非易失键值存储 | OSAL、Flash 驱动抽象 | 具体产品应用 |
| `ble/goodix` | Goodix BLE 适配 | Goodix SDK、公共 `ble::` API | 向产品层暴露厂商细节 |

## 设计约束

- 组件公开头文件应保持产品无关。
- 组件中需要硬件能力时，应依赖驱动抽象或由应用注入设备对象。
- 产品策略、启动顺序、命令行交互、产品参数默认值应放在 `app/product`。
- 第三方 SDK 只在适配组件内部扩散，不能穿透到应用层。
- 组件按独立 CMake target 和 Kconfig symbol 选择；禁止重新引入按产品命名的
  聚合静态库。产品参数 schema 和主题拓扑必须留在 `app/product`。
