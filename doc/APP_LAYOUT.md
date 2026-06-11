# App 产品目录约定

`app/product/<product>` 只放产品级编排代码：启动流程、设备门面、通信桥接、任务编排、控制策略和产品配置。可复用算法、协议栈、驱动和厂商 SDK 适配应继续放在 `component/` 或 `embedded/`。

## 通用规则

- `main.cc` 固定放在产品目录根部，保持入口可见。
- 产品代码统一使用 `app` 命名空间；板级设备门面使用 `app::board`。
- 不使用 `demo_app`、`demo::board`、`foc_app`、`ble_app_*` 这类产品名或模块名前缀作为命名空间。
- 业务代码通过 `board/board_devices.*` 暴露的产品语义设备名访问硬件，不在控制、服务或任务代码里直接散落 `device_get(...)`。
- CMake 只登记当前产品实际源文件目录，不保留空的历史目录。

## demo

`demo` 是 GD32 FOC 电机控制产品，当前结构如下：

```text
app/product/demo/
  main.cc                       启动入口
  board/
    board_devices.h             产品级设备门面声明
    board_devices.cc            DTS 设备别名到产品语义名称的绑定
  services/
    app_services.h              boot 确认、日志、设备检查、CLI 轮询等应用服务
    app_services.cc
  control/
    control_app.h               控制系统入口：start_control/feed_control_char
    control_app.cc
    motor_context.h             电机运行上下文
    controller/                 控制器、保护、位置传感器、校准等实现
  comm/
    can_handler.h               产品 CAN 命令和遥测
    can_handler.cc
  config/
    board.dts
    prj.conf
  CMakeLists.txt
  Kconfig
  linker.ld
```

## demo_ble

`demo_ble` 是 GR5525 BLE HID + UART 透传产品，当前结构如下：

```text
app/product/demo_ble/
  main.cc                       启动入口
  board/
    board_devices.h             BLE、UART、按键、LED 的产品级设备门面
    board_devices.cc
  services/
    ble_app.h                   BLE 初始化、发送、调度和 RX 回调注册
    ble_app.cc
  comm/
    link_bridge.h               Link 协议到 BLE UART 透传的桥接
    link_bridge.cc
  tasks/
    app_tasks.h                 HID、UART 透传、BLE scheduler、heartbeat 任务
    app_tasks.cc
  config/
    board.dts
    prj.conf
    user_config.h
  CMakeLists.txt
  Kconfig
  linker.ld
```

## 构建

应用固件默认使用 `FIRMWARE_TYPE=app`，也可以用短参数 `-Df=app`：

```powershell
cmake -S . -B out\demo_app -GNinja -Dp=demo -Df=app
cmake --build out\demo_app --target app

cmake -S . -B out\demo_ble_app -GNinja -Dp=demo_ble -Df=app
cmake --build out\demo_ble_app --target app
```

完整 ELF 链接还依赖对应 SoC/SDK 的 ROM 符号、预编译库和工具链兼容性；只验证产品目录重构时可以先构建 `app` 静态库目标。
