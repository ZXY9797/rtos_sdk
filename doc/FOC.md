# FOC 电机控制组件

本文描述 `component/motor/drive` 的设备适配与配置契约。`demo_foc` 的电流控制使用
`sdk::motor` 中的 `CurrentLoop`，由产品直接接入采样和 PWM。
当前产品调用链见 [电机算法](MOTOR_ALGORITHMS.md)，能力范围及阻抗/导纳设计见
[电机控制能力](MOTOR_CONTROL_DESIGN.md)。下述 Kconfig/DTS 契约不能直接套用于该产品。

## 定位

`component/motor` 提供纯计算，`component/motor/drive` 提供 FOC 设备接入。
电压模式使用 PWM，电流模式还需要同步 ADC 和 `foc,motor` 设备描述。
产品启动、控制策略、CLI 和 NVS 编排由 `app/product/<product>` 管理。

```text
产品控制与保护策略
        ↓
foc::VoltageMotorDevice 或 foc::MotorDevice
        ↓
foc::VoltageFoc 或 foc::Motor / FocController
        ↓
foc::Svpwm（两种设备模式共用）
        ↓
hal::Pwm [+ hal::Adc]
        ↓
SoC 后端与定时器/ADC 硬件
```

## 配置单一事实源

| 配置 | 内容 |
|:---|:---|
| DTS `foc,motor` | PWM/ADC phandle、三相通道、电机 R/L/磁链/极对数、限流限压、PWM/ADC 时序与采样换算、控制模式 |
| `component/motor/Kconfig` | `FOC_VOLTAGE_CONTROL`/`FOC_CURRENT_CONTROL` 源文件选择及速度环频率 |
| 产品 Kconfig | 转矩常数、减速比、LESO/位置环、保护阈值、扫频等产品行为 |
| NVS | 通过范围与有限值校验后的运行时标定覆盖；无效数据不得替换 DTS 安全默认值 |

电机物理参数和 PWM/ADC 硬件参数不再在 Kconfig 中维护副本。修改电机或功率板时
更新 DTS；修改产品行为时更新产品 Kconfig；现场标定只通过受校验的 NVS 数据。

`FOC_VOLTAGE_CONTROL` 用于没有相电流采样的硬件，只提供 dq 电压到三相占空比的
有界映射，不宣称电流、转矩或过流软件闭环。`FOC_CURRENT_CONTROL` 依赖
`DT_HAS_FOC_MOTOR_ENABLED`，并编译电流采样、观测器、HFI 和电机运行状态机。
两种设备模式复用 `foc::Svpwm` 的线性区限幅、零序注入和占空比生成代码。
`demo_foc` 使用同一电机组件中的扇区式 SVPWM 接口，直接取得 PWM 周期内的比较时刻。

`foc,motor` 的全部电气、安全限制、PWM/ADC 换算和控制模式属性均为 required，binding
不提供可静默落地的默认值。adapter 在编译期检查极对数、三相通道唯一性、限流/限压、
电流环带宽、定时器频率、死区、ADC 分辨率/通道/零点和缩放范围；不满足约束时停止构建。
这些静态检查用于阻止明显错误配置，不能替代功率板参数评审和目标板测量。

## 设备树示例

```dts
motor0: motor {
    compatible = "foc,motor";
    status = "okay";

    pwm-dev = <&timer0>;
    adc-dev = <&adc0>;
    pwm-ch-u = <0>;
    pwm-ch-v = <1>;
    pwm-ch-w = <2>;

    rs-milliohm = <50>;
    ld-microhenry = <100>;
    lq-microhenry = <100>;
    flux-milliweber = <0>;
    pole-pairs = <14>;
    max-current-ma = <20000>;
    max-voltage-mv = <48000>;

    pwm-frequency = <20000>;
    current-bandwidth = <1000>;
    timer-clock-hz = <120000000>;
    pwm-prescaler = <0>;
    dead-time-ns = <500>;

    adc-resolution-bits = <12>;
    current-u-channel = <0>;
    current-w-channel = <1>;
    vbus-channel = <2>;
    adc-trigger-source = <0>;
    adc-reference-mv = <3300>;
    current-zero-code = <2048>;
    current-sense-uv-per-amp = <100000>;
    vbus-divider-milli = <20000>;

    sensor-mode = <0>;  /* 0 无感，1 Hall，2 开环，3 编码器 */
    control-mode = <0>; /* 0 转矩，1 速度，2 占空比，3 位置 */
};
```

binding 中的 `requires` 声明 PWM 和 ADC 依赖，生成器在配置阶段检查初始化顺序。
adapter 负责工程单位到 `foc::MotorConfig` 的类型化转换，并对极对数、PWM 通道和
模式做编译期检查。

## 使用

产品层通过板级门面取得设备：

```cpp
auto &motor_device = app::board::main_motor();
if (!motor_device.is_initialized()) {
    return kMotorNotReady;
}
motor_device.motor().enable();
return 0;
```

`MotorDevice` 在 initcall 的 `APPLICATION` 阶段使用 DTS 配置初始化。应用在启动
电机前仍应检查就绪状态，并在保护触发、采样失效或校准失败时保持 PWM 关闭。

## 板级验收

编译和链接通过不证明功率级安全。首次上板至少验证：

1. 三相 PWM 通道/互补输出/死区和紧急关断极性；
2. ADC 触发点、采样通道、零点和电流/母线换算；
3. PWM ISR 与 ADC 数据的新鲜度、最坏执行时间和丢样行为；
4. 过流、过压、欠压和过温保护能在硬件实测阈值下关闭输出；
5. 转子锁定、传感器异常、通信中断和复位期间均不会产生非预期转矩。

硬件确认前使用限流电源、断开负载或低压母线，并保留可独立切断 PWM 的硬件手段。
