# MSDK

msdk 是基于zephyr精简出的一个RTOS开发框架。移植了zephyr的DTS和Kconfig系统，借鉴了zephyr的目录结构并做出了一些更改。
具备以下特点：
 - 目录结构分为app和embedded，app存放项目，embedded存放msdk的源代码
 - 构建编译采用原生CMAKE，放弃zephyr复杂的构建层级
 - 使用简单的两级DTS，具体芯片的xxx.dtsi + project的board.dts
 - 使用单一链接脚本，不使用链接脚本片段
 - 全部基于静态内存实现
 - 支持使用不同的RTOS，例如UCOSII、FreeRTOS、UCOSIII、ThreadX、RT-Threads、Zephyr，通过OSAL对接

## 快速开始

### 安装工具链
与zephyr复杂的编译环境不同，MSDK只需要安装python3、cmake、gcc-arm-none-eabi、ninja即可。

### 创建项目
在app目录下创建一个新文件夹，并将其命名为你的项目名。

### 构建与编译
cmake -B out -GNinja -Dp=<your_project_name> 即可完成项目构建
ninja -C out 即可完成编译
编译产物会生成在out目录下

## TODO
- 添加osal层
- 实现基础服务函数
