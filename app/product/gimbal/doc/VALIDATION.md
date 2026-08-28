# Gimbal 验证与发布门禁

## 1. 可自动执行的验证

主机算法测试：

```sh
cmake -S tests -B out/host-tests -G Ninja
cmake --build out/host-tests
ctest --test-dir out/host-tests --output-on-failure
```

固件构建：

```sh
cmake -S . -B out/gimbal/app -G Ninja -Dp=gimbal -Df=app -Dt=armgcc9
cmake --build out/gimbal/app

cmake -S . -B out/gimbal/preloader -G Ninja \
  -Dp=gimbal -Df=preloader -Dt=armgcc9
cmake --build out/gimbal/preloader

cmake -S . -B out/gimbal/loader -G Ninja \
  -Dp=gimbal -Df=loader -Dt=armgcc9
cmake --build out/gimbal/loader

cmake -S . -B out/gimbal/upgrade -G Ninja \
  -Dp=gimbal -Df=upgrade -Dt=armgcc9 \
  -DLOADER_PAYLOAD_FILE="$PWD/out/gimbal/loader/gimbal_loader.bin"
cmake --build out/gimbal/upgrade
```

构建证明只覆盖编译、链接、分区和 ELF 静态门禁，不代表 PWM 引脚、ADC 通道、
电机方向、Hall 极性或整机闭环已经通过。

2026-08-28 的静态基线：11 个主机测试全部通过；`gimbal` 四类 ARM 镜像均使用
各自产品链接脚本构建通过，app 的 ELF 策略检查也通过。链接器报告如下：

| 镜像 | Flash | 分区 | RAM |
|---|---:|---:|---:|
| app | 96,212 B | 286,592 B（33.57%） | 41,776 B（31.87%） |
| preloader | 5,628 B | 16,384 B（34.35%） | 1,832 B（1.40%） |
| loader | 17,600 B | 24,576 B（71.61%） | 6,280 B（4.79%） |
| upgrade | 26,140 B | 155,648 B（16.79%） | 1,840 B（1.40%） |

loader 二进制为 17,664 B，剩余 6,912 B。upgrade 嵌入并校验同轮 loader
payload。同一轮 `demo` ARM 回归构建及 ELF 策略检查通过；复用同一 CMake
输出目录执行 `gimbal -> demo -> gimbal` 时，链接脚本也随产品正确切换。
该结果使用 GNU Arm Embedded 13.2.1；GD32 产品没有 Goodix BLE 固件的
GCC 9.3.1 工具链限制，但正式发布仍需锁定并记录工具链容器摘要。

## 2. 上板前门禁

- 原理图逐项核对三轴 PWM、Hall ADC、IMU CS、加热器和硬件关断链。
- 用 MCO/示波器确认 200 MHz 时钟树、20 kHz 中心对齐电机 PWM 和
  1 kHz 加热 PWM；同时确认 TIMER6 触发周期为 62.5 us。不得以 Kconfig
  或 DTS 数值代替测量。
- 用逻辑分析仪同时捕获 TIMER6 调试脉冲、IMU CS/SCLK 和 DMA 完成调试脉冲，
  确认 10 MHz 下 15 字节事务在下一采样边沿前结束，并记录最坏触发到闭环
  PWM 更新延迟、峰峰抖动和 CPU 占用。
- 验证正常启动时首个 1 kHz 批次先于 supervisor 执行；断开 IMU 或阻塞 DMA
  时，有界等待在 20 ms 内退出，supervisor 随后锁存真实传感器故障且 PWM
  始终关闭。
- 人为延迟 `g_closed`、阻塞一次 SPI DMA、注入 TX/RX DMA error，确认
  `trigger_overrun`、`batch_overrun`、`source_error` 分别增加，DMA 不覆盖
  Reading 缓冲，并在下一次可运行的闭环周期撤销三轴输出。
- 示波器确认复位、Boot、Fault 和 Calibration 状态下门极均关闭。
- 注入缺失/损坏工厂参数，确认始终无法 arm。
- 注入错误工厂硬件版本，确认固件进入 `Calibration`；修改 DTS 中的 PWM 周期
  或重复 U/V/W 通道，确认生成或编译阶段失败。
- 注入 Hall/IMU 超时、非有限值、机械越界和 Jacobian 奇异，确认一周期内撤销授权。
- 在最大通信、规划和传感器负载下记录各任务 `stack_free`、`missed` 与
  `heap_min`，同时记录 sensor trigger/sample/source_error/batch_overrun；
  不允许周期失约，栈和堆裕量须由产品安全评审批准。
- 对所有已置位 capability 注入开路、短路、过量程和过温；未置位能力不得误报。

## 3. 单轴到三轴验证

1. 脱离负载验证 Hall 信号、机械方向、电角方向和低电压 SVPWM。
2. 单轴闭环完成阶跃、扫频、堵转、饱和和启动停止手感。
3. 三轴逐步启用耦合和动力学前馈，覆盖全姿态和最小/最大负载。
4. 验证 Follow、Lock、FPV 和回中时的目标连续性及模式切换无突跳。
5. 在恒温箱验证预热、稳态波动、高环境温度 heater-off 和 yaw 漂移。

## 4. 量产与追溯

每台设备保存硬件版本、参数 schema、代际、标定残差、频响摘要和工装版本。
发布记录分别列出静态分析、主机测试、目标板测试、HIL、整机性能和故障注入
结果；任何一栏不得用“构建成功”替代。
