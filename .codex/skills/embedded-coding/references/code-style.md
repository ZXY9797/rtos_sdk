
# 代码风格与格式规范

先遵循用户本次明确的格式要求，再遵循当前项目的 formatter、编译规则和邻近代码。
下列行宽、留白和函数长度等为缺省参考，
不要求重排未修改内容；具体符号可见性还需保留启动入口、ISR、弱符号、链接段和生成器契约。

## 行宽

用户没有另行指定时，项目已有 `.clang-format`、formatter 配置或明确约定的，以项目为准。
没有项目规定时，默认目标行宽为 **100 列**；这属于可读性约定，不是安全要求。

- 普通代码优先保持在 100 列以内，在参数组、运算符或逻辑边界处自然换行。
- 一行能清楚表达的短调用可以保留，不因旧的 80 列限制把参数机械拆成多行。
- 项目明确采用 80 或 120 列时沿用该值，不自动覆盖已有格式配置。
- URL、不可拆分标识符或字符串等按项目格式器支持的例外处理；不要改变内容以满足行宽。
- 修改只格式化相关代码，不为调整默认行宽重排整个仓库。

使用 clang-format 且项目未规定行宽时，对应选项为：

```yaml
ColumnLimit: 100
```

该选项含义见 [Clang-Format 文档](https://clang.llvm.org/docs/ClangFormatStyleOptions.html#columnlimit)。
这是格式配置示例，不要求仅因加载本 skill 就创建或修改项目配置。

```c
/* 这个调用在 100 列内，可以保持单行。 */
int result = very_long_function_name(first_parameter, second_parameter, third_parameter);

/* 参数较长时，按语义分组换行。 */
int result = configure_peripheral_dma_transfer(
    peripheral_handle, dma_channel_config,
    receive_buffer, receive_buffer_size, completion_callback);
```

## 空行与逻辑分段

横向行宽和纵向留白分别控制。100 列不意味着把更多语句或函数挤在一起。
没有项目明确约定时，采用以下默认规则：

- **函数之间**：相邻函数定义之间保留 1 个空行，类内成员函数定义也适用。
  函数文档注释紧贴其声明或定义，分隔空行放在文档注释之前。
  同组函数声明可以连续排列，不要求每条原型之间留空行。
- **函数内部**：参数校验、准备数据、主要处理、提交结果/清理等不同逻辑段之间
  保留 1 个空行。按职责分段，不按固定语句数分段；短小且完整的一组操作不强行拆散。
- **控制结构**：保护性提前返回结束后，通常留 1 个空行再开始主体逻辑。
  完成一个循环或条件块后，若接下来进入独立步骤，留 1 个空行。
  `if`/`else if`/`else`、`do`/`while` 属于同一控制结构，其相邻分支之间不额外插空行。
- **块的边缘**：不在左大括号后的第一条语句前、右大括号前机械留空行。
  同一逻辑段内连续赋值、相关变量声明或连续寄存器配置保持相邻。
- **留白数量**：通常 1 个空行即可，不用连续多个空行代替模块划分。
  不为了压缩代码、缩短回复或满足函数行数目标而删除已有的合理分段。
- **单行代码**：短调用可以保持单行，不意味着可以把任意函数体或分支折叠为单行。
  项目允许的简单 getter/setter 可沿用，但仍应保持函数之间的分隔。

例如，校验与发送是不同步骤；同一次发送的返回值检查仍紧贴该调用：

```c
int packet_send(const Packet_t *packet)
{
    if (packet == NULL) {
        return ERR_INVALID_ARG;
    }

    int ret = validate_packet(packet);
    if (ret != ERR_OK) {
        return ret;
    }

    ret = transport_send(packet->data, packet->length);
    if (ret != ERR_OK) {
        return ret;
    }

    record_packet_sent(packet);
    return ERR_OK;
}

void packet_reset(Packet_t *packet)
{
    packet->length = 0;
    packet->valid = false;
}
```

完成编辑或运行 formatter 后，检查实际差异中的函数分隔和逻辑分段。
若格式器删除了用户要求的空行，查明并处理具体配置冲突；不要把这种输出直接视为
符合要求，也不要为了本次局部修改重排整个仓库。空行不能表达清楚的职责边界，再考虑拆函数。

## 函数长度

按职责、控制流和可读性评估函数长度。接近或超过 80 行是复查信号，不是硬上限。
不得通过删除空行、合并多条语句或压缩分支来凑行数。
确有独立职责或重复子流程时再提取函数，同时保留接口、时序和资源生命周期契约：

```c
/* 反面示例 - 单体函数 */
int process_packet(Packet_t *pkt)
{
    /* 120行混合了验证、解析、处理和响应构建 */
}

/* 正面示例 - 分解 */
int process_packet(Packet_t *pkt)
{
    int ret;

    ret = validate_packet_header(pkt);
    if (ret != ERR_OK) {
        return ret;
    }

    ret = parse_packet_payload(pkt);
    if (ret != ERR_OK) {
        return ret;
    }

    return build_response(pkt);
}
```

## 嵌套深度

最多4层嵌套。使用提前返回、保护子句或提取函数来减少嵌套深度：

```c
/* 反面示例 - 5层嵌套 */
void process(Data_t *d)
{
    if (d != NULL) {
        if (d->valid) {
            for (int i = 0; i < d->count; i++) {
                if (d->items[i].active) {
                    if (d->items[i].value > 0) {
                        /* 第5层 - 太深了 */
                    }
                }
            }
        }
    }
}

/* 正面示例 - 提前返回减少嵌套 */
void process(Data_t *d)
{
    if (d == NULL || !d->valid) {
        return;
    }

    for (int i = 0; i < d->count; i++) {
        process_item(&d->items[i]);
    }
}

static void process_item(Item_t *item)
{
    if (!item->active || item->value <= 0) {
        return;
    }
    /* 在合理的嵌套深度中处理 */
}
```

## 魔法数字

所有数字字面量（除0、1和明显的布尔类值外）必须定义为宏：

```c
/* 反面示例 */
if (retry_count > 3) { ... }
uint8_t buffer[256];
timeout = 5000;

/* 正面示例 */
#define MAX_RETRY_COUNT      (3U)
#define RX_BUFFER_SIZE       (256U)
#define DEFAULT_TIMEOUT_MS   (5000U)

if (retry_count > MAX_RETRY_COUNT) { ... }
uint8_t buffer[RX_BUFFER_SIZE];
timeout = DEFAULT_TIMEOUT_MS;
```

### 宏定义风格

- 值用括号包裹：`#define FOO (42U)`
- 无符号常量使用 `U` 后缀
- 大型无符号常量使用 `UL` 或 `ULL` 后缀
- 相关宏使用统一前缀分组

## 注释

### 语言

推荐使用英文注释，或与项目约定一致。

### 作者署名

创建新文件或主要新章节时：

```c
/**
 * @file    uart_driver.c
 * @brief   UART driver with non-blocking async API.
 * @author
 */
```

### 注释质量规则

1. 注释解释"为什么"，而非"是什么"
2. 注释必须与代码一致——修改代码时同步更新注释
3. 删除被注释掉的代码；使用版本控制来保存历史
4. 不写重复代码内容的冗余注释

```c
/* 反面示例 - 重复代码内容 */
i++;  /* 递增 i */

/* 反面示例 - 代码修改后注释过时 */
/* 发送3次重试 */
for (int i = 0; i < MAX_RETRY_COUNT; i++) { /* 原来是3，现在是5 */

/* 正面示例 - 解释意图 */
/* 使用指数退避重试，避免在错误恢复期间
 * 造成总线拥塞 */
for (int i = 0; i < MAX_RETRY_COUNT; i++) {
```

### 函数文档

公共函数必须有文档注释：

```c
/**
 * @brief  通过驱动异步发送数据。
 * @param  self   驱动实例（不能为NULL）
 * @param  data   数据缓冲区指针
 * @param  len    要发送的字节数（最大 TX_BUF_SIZE）
 * @return 成功返回 ERR_OK，忙碌时返回 ERR_QUEUE_FULL
 * @note   非阻塞。结果通过回调传递。
 *         线程安全：可从任何上下文调用。
 */
int Driver_SendAsync(Driver_t *self,
                     const uint8_t *data,
                     uint16_t len);
```

## 死代码

所有未使用的、废弃的或被注释掉的代码必须删除。
版本控制会保存历史。死代码的危害：

- 造成对活跃代码的混淆
- 可能被意外启用
- 增加维护负担
- 在代码审查中掩盖真正的问题

## const正确性

尽可能使用 `const` 来防止意外修改并文档化意图。
这是硬性规则，不是建议。

1. 不被修改的指针参数：`const uint8_t *data`
2. 查找表和配置：`static const`
3. 初始化后不可变的结构体成员

**注意：** 不要对值参数加 `const`（如 `const uint16_t len`），
因为值参数是调用者的副本，加 `const` 没有实际意义。

```c
/* 指针参数加 const 表示不修改指向的数据 */
int Protocol_Send(Protocol_t *self,
                  const uint8_t *data,
                  uint16_t len);

/* const 查找表 */
static const uint16_t CRC_TABLE[CRC_TABLE_SIZE] = {
    /* ... */
};
```

## static限制作用域

所有不属于模块公共API的函数和变量必须声明为 `static`。无例外。

- `static` 函数：内部辅助函数、硬件访问封装
- `static` 变量：模块私有状态、缓冲区
- 文件级静态变量加 `s_` 前缀：`static int s_count;`
- 真正的全局变量加 `g_` 前缀：`extern int g_system_mode;`

## 头文件包含

### 头文件保护

每个头文件必须有头文件保护宏：

```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H

/* ... 内容 ... */

#endif /* MODULE_NAME_H */
```

C++ 文件也可使用 `#pragma once`（编译器普遍支持），
但需与项目约定保持一致。

### 自包含头文件

每个头文件必须能独立编译。包含它所引用的所有类型
——不要依赖引用者来提供。

### 路径约定

在添加 `#include` 之前，搜索项目中其他文件是如何包含
同一头文件的。使用完全相同的路径格式：

```c
/* 如果项目中其他文件这样写： */
#include "drivers/uart_driver.h"

/* 那么你也必须这样写： */
#include "drivers/uart_driver.h"

/* 不要写成： */
#include "uart_driver.h"
#include "../drivers/uart_driver.h"
```

### 包含顺序

遵循项目现有的包含顺序。常见约定：

```c
/* 1. 对应头文件（用于 .c 文件） */
#include "my_module.h"

/* 2. 平台/RTOS头文件 */
#include "FreeRTOS.h"
#include "task.h"

/* 3. HAL/BSP头文件 */
#include "stm32f4xx_hal.h"

/* 4. 项目模块头文件 */
#include "drivers/uart_driver.h"
#include "app/protocol.h"

/* 5. 标准库（如使用） */
#include <string.h>
#include <stdint.h>
```

## 命名约定

遵循项目现有的命名约定。

### C 语言命名

| 元素 | 约定 | 示例 |
|-----|------|------|
| 宏 | 全大写蛇形 | `MAX_RETRY_COUNT` |
| 类型 | PascalCase + `_t` | `UartDriver_t` |
| 函数 | 模块_动作 | `UartDriver_Send` |
| 局部变量 | 小写蛇形 | `retry_count` |
| 结构体成员 | 小写蛇形 | `buf_size` |
| 枚举值 | 前缀_名称 | `UART_ERR_TIMEOUT` |

### C++ 命名

在 C 命名约定基础上，补充以下 C++ 特有约定：

| 元素 | 约定 | 示例 |
|-----|------|------|
| 类名 | CamelCase | `UartDriver`、`SensorBase` |
| 成员函数 | camelCase | `readData()`、`init()` |
| 成员变量 | camelCase + 尾部下划线 | `tx_buf_`、`is_open_` |
| 常量 | `k` 前缀 + CamelCase | `kMaxBaudRate`、`kBufSize` |
| 模板参数 | CamelCase | `typename Derived` |
| 命名空间 | 小写蛇形 | `drivers::uart` |
| 枚举类成员 | CamelCase | `State::Idle`、`State::Running` |
| 布尔成员 | `is_`/`has_` 前缀 | `is_initialized_` |

始终先检查项目的实际约定并保持一致。

## 函数参数数量

每个函数最多5个参数。需要更多参数时，将相关参数组合成配置
结构体。详细的模式和示例参见 `03_Clean_Code.md`。

## 安全编码基本规则

以下规则参考 BARR-C 嵌入式编码标准。

### 大括号规则

所有 `if`/`for`/`while`/`do-while` 语句必须使用大括号，
即使只有一行：

```c
/* 禁止 */
if (error) return;

/* 必须 */
if (error) {
    return;
}
```

### 固定宽度整数类型

禁止使用 `int`/`short`/`long`/`unsigned` 等原生类型，
必须使用 `stdint.h` 的固定宽度类型：

```c
/* 禁止 */
int count;
unsigned char data;

/* 必须 */
int32_t count;
uint8_t data;
```

### 位操作安全

- 位操作仅用于无符号整数类型
- 移位量不得大于或等于操作数的位宽
- 有符号整数禁止左移（C标准中是未定义行为）

```c
/* 禁止 — 有符号左移是未定义行为 */
int32_t val = 1;
int32_t result = val << 16;

/* 必须 — 使用无符号类型 */
uint32_t val = 1U;
uint32_t result = val << 16U;
```

### 返回值检查

非 `void` 函数的返回值不得被丢弃，必须检查或显式标注忽略：

```c
/* 禁止 — 返回值被丢弃 */
platform_device_open(dev, DEVICE_FLAG_RDWR);

/* 必须 */
err_t ret = platform_device_open(dev, DEVICE_FLAG_RDWR);
if (ret != ERR_OK) {
    /* 处理错误 */
}
```
