# demo_foc 主机工具

`motor_tool.py` 使用 Link v1、CRC8/SMBUS 和 CRC16/IBM-3740。默认 UART 2 Mbaud，主机地址 0x20，
设备地址 0x10。需要 Python 3.10+，连接串口时安装 `pyserial`。
命令只发送一次，超时不会自动重复写入或运动请求。

```powershell
python -m pip install pyserial
python tools/demo_foc/motor_tool.py --port COM8 read current
python tools/demo_foc/motor_tool.py --port COM8 status
python tools/demo_foc/motor_tool.py --port COM8 observe raw
python tools/demo_foc/motor_tool.py --port COM8 observe control
python tools/demo_foc/motor_tool.py --port COM8 observe timing
python tools/demo_foc/motor_tool.py --port COM8 telemetry control.csv --kind control --seconds 30 --period 20
```

`read` 返回 valid_mask 和按协议顺序排列的 float 值。原始电流是预设 ADC 换算后的 A，原始 Vbus 是 V；
线性标定是 `calibrated = raw * gain + offset`，不是 ADC 电压转换系数。
current-linear 同时提交三相，顺序是 U offset/gain、V offset/gain、W offset/gain。
工具写入后再次查询校验。总线系数示例不改变正常换算：

```powershell
python tools/demo_foc/motor_tool.py --port COM8 current-linear 0 1 0 1 0 1
python tools/demo_foc/motor_tool.py --port COM8 bus-linear 0 1
python tools/demo_foc/motor_tool.py --port COM8 erase input_tmr
python tools/demo_foc/motor_tool.py --port COM8 erase all
```

清除只能在空闲时执行；成功清除不再自动启动标定。一次 `erase all` 按记录逐项提交。
`calibrate all` 显式启动三阶段运动标定，`calibrate input/electrical/output` 单独启动一阶段；
用 `status` 查看最终状态。`stop` 请求停止，`stop --emergency` 立即退出背隙流程，
`clear-fault` 在空闲且保护恢复后清本地故障。

采集命令订阅转子 TMR 和关节 Hall 的配对快照，电机保持当前使能状态。

```powershell
python tools/demo_foc/motor_tool.py --port COM8 capture samples.csv --seconds 30 --divider 20
```

CSV 保留输入/输出角、两条采样链的软件处理时间、配对与关节采样序号、
输入角有效位和累计丢包数。拟合前检查有效位和序号连续性，按 32 位回绕处理时间差。

LUT CSV 使用 `index,offset_rad` 两列，必须完整包含有序的 0..359 索引及有限的 rad 误差值。
输入和输出 LUT 的格式相同，输入 LUT 仅存储，控制不应用输入 LUT。
工具提供采集和表传输；误差表由标定数据分析生成，不自动混入新的拟合模型。

```powershell
python tools/demo_foc/motor_tool.py --port COM8 lut-write correction.csv --table output
python tools/demo_foc/motor_tool.py --port COM8 lut-read readback.csv --table output
```

LUT 传输使用独占事务、120 点分块和整表 CRC32；写入 Finish 后开启新读取事务检查持久化内容。
LUT 读写需要控制禁用。工具不自动执行 Stop，以保留运动控制的显式顺序。

UART 可以读取模式状态或请求停机。运行目标通过
[CANopen FD](../../app/product/demo_foc/doc/joint_drive.md#canopen-fd-接口) 下发。

```powershell
python tools/demo_foc/motor_tool.py --port COM8 mode-status
python tools/demo_foc/motor_tool.py --port COM8 stop
```

协议字段及观测数值顺序见 [项目文档](../../app/product/demo_foc/README.md)。

离线测试不需要串口或 pyserial：

```powershell
python -m unittest discover -s tools/demo_foc -p test_*.py
```

参数管理、力矩/RL 辨识和外力接口见 [交互控制工具](INTERACTION.md)。
