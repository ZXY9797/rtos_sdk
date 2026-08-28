# 三轴非正交手持云台

本目录是 GD32F503 参考产品，产品无关算法位于
`component/gimbal`。当前实现包含双 Hall 连续角度、电角度/机械角度标定、
相机 IMU EKF、无手柄 IMU 的非正交运动学反解、动力学前馈、带频率整形的
反馈控制、SO(3) 平滑梯形规划、IMU 恒温、共享内存主题、A/B 参数存储、
安全状态机和独立硬件看门狗。
IMU 链路由 `SensorBatchCore` 管理：TIMER6 以 16 kHz 启动 SPI DMA，RX-DMA
中断封存 ping-pong frame，每 16 点用 const 指针唤醒 1 kHz 闭环任务。
控制输出还包含待机积分清零、三相 PWM 一致提交、重新 arm 前中性占空比装载和
Active 周期失约锁存，避免停止后重启冲击及单周期混相命令。

同一产品目录还配套 `bootloader/product/gimbal` 的 preloader、loader、upgrade
配置和分区布局，可独立生成四类镜像；upgrade 构建必须嵌入同轮生成并校验过的
loader payload。

文档入口：

- [总体架构](doc/ARCHITECTURE.md)
- [代码实现与任务模型](doc/IMPLEMENTATION.md)
- [工厂标定流程](doc/CALIBRATION.md)
- [验证与发布门禁](doc/VALIDATION.md)

`config/board.dts` 只是参考资源映射。实际 PCB 的 PWM pinctrl、功率级
enable/brake/fault、Hall 模拟引脚和可选遥测通道尚不能从现有资料确定；在
完成原理图映射及板级故障注入前，不得用该参考配置给电机功率级上电。没有
有效工厂参数时，固件固定停留在 `Calibration`，电机授权保持关闭。
