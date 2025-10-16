

# SOC的KConfig
## 目录层级
```
soc
|   Kconfig
|
|-- st
|   |   Kconfig
|   |   Kconfig.deconfig
|   |   Kconfig.soc
|   |-- stm32cube
|   |       Kconfig
|   |-- stm32f4x
|   |       Kconfig
|   |-- stm32h7x
|   |       Kconfig
|   |       Kconfig.deconfig
|   |       Kconfig.deconfig.stm32h743xx
|   |       Kconfig.deconfig.stm32h757xx
|   |       Kconfig.soc
├── gd
├── Kconfig.stm32h7x

```
其中Kconfig一般作为配置项的定义；Kconfig.deconfig一般作为配置项的默认值定义；Kconfig.soc一般作为SOC的选择配置相关。

## 配置流
***SOC的Kconfig配置具有唯一来源: DTS中定义的/compatible属性。***
以demo 项目中的board.dts为例:

1. app/demo/config/board.dts中定义/compatible为"st,stm32h743xx"
2. DTS经过预处理、生成后生成 out/edt.pickle文件
3. 在embedded/soc/st/stm32h7x/Kconfig.soc存在如下配置项
    > ```Kconfig
    > DT_SOC_COMPAT_NAME := $(dt_get_soc_compat_name)
    > soc_name := $(get_soc_name,$(DT_SOC_COMPAT_NAME))
    > ...
    > config SOC_STM32H743XX
    >	def_bool $(str_is_equal,$(soc_name),stm32h743xx)
    >	select SOC_SERIES_STM32H7X
    > ```
    其中`dt_get_soc_compat_name`是在`kconfigfunctions.py`中定义的python函数，其会解析edt.pickle文件，拿到DTS中的节点信息
4. 选中SOC_STM32H743XX项后，会自动选中SOC_SERIES_STM32H7X项以及embedded/soc/st/stm32h7x/Kconfig中的配置，同时会相关的ARCH相关的选项也被选中。
5. KConfig进一步转换为.cmake文件和autoconfig.h文件，供构建和编译系统使用。

