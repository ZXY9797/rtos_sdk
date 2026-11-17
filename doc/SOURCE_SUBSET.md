# SDK 源码精简与维护

SDK 的主体是面向实时控制的 C++ 设备、组件和资源契约。厂商 SDK、内核上游包和硬件
描述按本轮清理前的文件范围完整保留，方便后续接入；当前是否编译使用不作为删除依据。
精简集中于 SDK 自身的冗余层、重复依赖和构建脚手架，构建仍按配置选择源文件。

## 本次精简范围

2026-11-17 对 `embedded`、`component`、`tools` 及应用、板卡、Boot、样例、测试、文档
进行盘点。以下保留上一轮精简并恢复厂商包后的记录；后续目录整理以审计报告为准。
文件内容大小排除 `.git`、`out`、`__pycache__`；本机已存在的
旧 ELF 解析器的上游测试样本计入清理前的盘点，因此新克隆的节省量会有所不同。

| 范围 | 精简前 | 精简后 | 处理方式 |
|---|---:|---:|---|
| embedded | 187.20 MiB / 4273 文件 | 187.16 MiB / 4262 文件 | 恢复厂商和内核包，只保留框架冗余清理 |
| component | 35.69 MiB / 444 文件 | 35.69 MiB / 444 文件 | Goodix 库、服务及原有归档全部保留 |
| tools | 24.59 MiB / 396 文件 | 约 1.40 MiB / 177 文件 | 保留生成器与验证工具，统一 Python 依赖 |
| 盘点总计 | 248.19 MiB | 约 224.96 MiB | 净减少约 23.23 MiB |

此前对未接入厂商代码的裁剪已撤回。恢复记录及 SHA-256 位于本机
`out/sdk-vendor-restore/restore-plan.json`；原清理备份仍保留在 `out/sdk-trim/`。
精简前后同口径净减少约 229 个文件，文档、检查工具和测试的增加已计入精简后规模。
这是源码集合的缩减，固件 Flash/RAM 大小应以各配置生成的资源报告为准。

## 删除与保留的依据

- **内核**：FreeRTOS、RT-Thread 原有端口、头文件、构建文件、BSP、DSP、样例及配套
  内容均已恢复；SDK 仍只编译当前配置选中的源文件。Baremetal 和两个 OSAL 适配层保留。
- **CMSIS**：原有 Core、编译器支持、文档、验证工程、模板及 API 均已恢复。
  根目录 `LICENSE.txt` 来自原包中的 `CoreValidation/LICENSE.txt`，内容未改写。
- **厂商和硬件资源**：保留 GD32、Goodix、STM32Cube 原有文件，恢复 STM32 通用 LL
  转接头和 F4 DTS。硬件描述的存在与已接入的构建后端分别管理：当前 STM32 后端为 H7，
  F4 资源保留供后续接入，不在 SoC catalog 中宣称已有可构建的 F4 后端。
- **Goodix**：原有全部库、BLE 服务、DSP 归档及其他配套内容均已恢复，包括本机原有的
  hardfloat 协议栈变体；当前构建继续使用原来受哈希校验的协议栈归档。
- **工具**：原来复制的 pyelftools 0.28 缺少 `construct/lib`，独立导入即失败；固件压缩
  原本已依赖环境中的 pyelftools。删除这份重复且不完整的副本和无效 fallback，统一使用
  `tools/requirements-ci.txt` 锁定的 0.32；该依赖同时服务压缩与 ELF 资源分析。
  同时移除没有脚本或 CMake 调用入口的 `pylib/yaml`，统一使用清单中的 PyYAML。
  Zephyr CMake 辅助库从原始 6160 行收敛到 431 行、21 个辅助命令，
  移除没有调用者的 runner、编译器探测与用户缓存层；SDK 扩展继续使用 `sdk_*` API。
- **SDK 公共层**：移除空架构转接头、无使用者的 Zephyr atomic/list/spinlock API、
  未实现的 display/mbox/port-endpoint DTS 便捷接口和外部 cache-controller 菜单。
  Cortex-M 自身 cache API 保留。STM32 备份域继续使用同一 IRQ 锁原语；Hall 硬件
  facade 不再依赖位置传感器算法头文件，现有样本读写方式保留。

“当前没有使用者”仅是审查线索，不能据此删除厂商功能。SDK 自身的接口须同时检查
构建引用、调用者和替代实现；许可证、来源记录、厂商样例和有效测试保留。

## 持续检查与上游维护

`tools/source-policy.json` 的 `protected_roots` 保护厂商 SDK、内核上游包和硬件资源。
审计拒绝与保护目录重叠的裁剪规则；执行删除前再次校验路径，防止绕过候选生成直接删除。
`tools/dependencies.json` 记录源码内容摘要，缺失的上游 revision 继续明确标为未知。
FreeRTOS 已恢复原有文件并核验内嵌 Git 工作区干净。

```sh
python -m pip install -r tools/requirements-ci.txt
python tools/scripts/source_audit.py --check
python tools/scripts/dependency_inventory.py
python tools/sdk.py test --suite host -B out/sdk-checks
python tools/sdk.py test --suite firmware -B out/sdk-matrix --toolchain-path C:/arm-none-eabi-gcc-9.3.1-1.4
python tools/scripts/check_rtos_ports.py --compiler C:/arm-none-eabi-gcc-9.3.1-1.4/bin/arm-none-eabi-gcc.exe --build out/port-checks
```

审计默认只读。确需再次裁剪时，先审阅默认输出或 `--report` 的候选清单，再使用
`--apply --backup out/<新的备份名>.zip`。工具拒绝删除已有本地修改的候选文件，
先校验备份内容及 SHA-256，再逐文件删除；其他未跟踪文件保留给使用者判断。
CI 执行保留清单与内容摘要检查。

产品公共元数据在 `products/` 下，同样计入后续审计。纯 C++ runtime 统一归属
`component/runtime`，实验性接口保留在对应组件的 `experimental` 目录和命名空间内。

本次移除的原文件分别保存在本机 `out/sdk-trim/removed-sources.zip`、
`removed-ll-stubs.zip`、`removed-cache-menu.zip`、`removed-unused-cache.zip`；
剩余的旧 ELF 解析器和样本输出分别保存在 `removed-duplicate-parser.zip`、
`removed-parser-reference-outputs.zip`，重复 YAML 包保存在 `removed-duplicate-yaml.zip`；
修改前的 LL 转接头保存在
`original-ll-dispatch.zip`。恢复前先核对清单和目标路径，
选择性恢复文件，避免覆盖后续修改。输出目录中的备份不进入 SDK 分发。

新增芯片、内核端口或 BLE 服务时，同步补齐后端、内容摘要和代表性验证；已有厂商文件
可以先保留，再按需要接入。不要为重新生成摘要而无条件接受来源不明的库。

## 验证边界

精简回归覆盖主机契约、现有 App 配置矩阵、三种产品的 Boot 链与开发三合一打包、
独立应用及外部模块；另编译 M4/M7/M33 的 RTOS/CMSIS 端口和 STM32 备份域源文件。
有效 DTS/引脚操作/Kconfig 与精简前产物对比，产物清单校验 SHA-256 和 ELF 内 build ID。
这些检查证明指定配置可生成、编译、链接和打包；板上时序、外设、电气保护及断电回滚
仍按实际板卡验收。
