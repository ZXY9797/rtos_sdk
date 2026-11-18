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

采集命令只订阅数据，不使能或移动电机。将外部编码器接到 CAN0，发送标准 ID 0x680..0x68F、
三字节大端 21 bit 原始计数。TMR 的三阶段标定和显式运动控制由设备命令完成。

```powershell
python tools/demo_foc/motor_tool.py --port COM8 capture samples.csv --seconds 30 --divider 20
```

CSV 保留输入/输出角、编码器计数、两条采样链的软件时间戳、配对/编码器序号、有效/更新标志、
累计丢包和编码器处理年龄。不能将这些软件观察时间当成物理同步采样时间；
拟合前检查有效位、年龄、序号连续性，并按时间回绕及复位切分数据。

LUT CSV 使用 `index,offset_rad` 两列，必须完整包含有序的 0..359 索引及有限的 rad 误差值。
输入和输出 LUT 的格式相同，输入 LUT 仅存储，控制不应用输入 LUT。
工具提供采集和表传输；误差表由标定数据分析生成，不自动混入新的拟合模型。

```powershell
python tools/demo_foc/motor_tool.py --port COM8 lut-write correction.csv --table output
python tools/demo_foc/motor_tool.py --port COM8 lut-read readback.csv --table output
```

LUT 传输使用独占事务、120 点分块和整表 CRC32；写入 Finish 后开启新读取事务检查持久化内容。
LUT 读写需要控制禁用。工具不自动执行 Stop，以保留运动控制的显式顺序。

模式事务示例：先读取状态，显式禁用，再进入指定位置；Hold/Restore 都携带完整目标。
工具从状态中取得接收序号后分配下一序号，不能同时使用多个控制主机。

```powershell
python tools/demo_foc/motor_tool.py --port COM8 mode-status
python tools/demo_foc/motor_tool.py --port COM8 mode disable
python tools/demo_foc/motor_tool.py --port COM8 mode target --mode 2 --position 0.2 --velocity 0.1 --iq 1
python tools/demo_foc/motor_tool.py --port COM8 mode hold --mode 2 --position 0.2 --iq 1
python tools/demo_foc/motor_tool.py --port COM8 mode restore --mode 2 --position 0.3 --velocity 0.1 --iq 1
```

事务 ACK 表示接收，查询 ModeStatus 的 busy=false、accepted=applied 且 faults=0 才能确认应用完成。
协议字段及观测数值顺序见 [项目文档](../../app/product/demo_foc/README.md)。

离线测试不需要串口或 pyserial：

```powershell
python -m unittest discover -s tools/demo_foc -p test_*.py
```

物理交互模式、参数管理、力矩/RL 辨识和外力接口见 [交互控制工具](INTERACTION.md)。
