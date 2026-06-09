# 嵌入式测试规范

测试是保证嵌入式软件质量的关键。
嵌入式测试比桌面软件更复杂，需要考虑硬件依赖和实时性。

---

## 1. 测试层次

### 1.1 测试金字塔

```
        /\
       /  \       系统测试（HIL）
      /    \      - 真实硬件环境
     /──────\     - 端到端验证
    /        \    
   /  集成测试\   集成测试
  /            \  - 模块间交互
 /──────────────\ - Mock 硬件
/                \
/    单元测试     \ 单元测试
/──────────────────\- 纯函数逻辑
                    - 无硬件依赖
```

### 1.2 测试策略

| 测试类型 | 目标 | 环境 | 覆盖率目标 |
|---------|------|------|-----------|
| 单元测试 | 函数逻辑正确性 | PC/CI | 80%+ |
| 集成测试 | 模块间协作 | PC + Mock | 60%+ |
| HIL 测试 | 硬件交互 | 真实硬件 | 关键路径 |

---

## 2. 单元测试

### 2.1 可测试性设计

```c
/* 原则：依赖注入，避免直接操作硬件 */

/* 错误 — 直接依赖硬件 */
int read_sensor_value(void)
{
    return ADC1->DR;  /* 无法在 PC 上测试 */
}

/* 正确 — 通过接口抽象 */
typedef struct {
    int (*read_adc)(uint8_t channel);
} SensorHal_t;

int read_sensor_value(const SensorHal_t *hal)
{
    return hal->read_adc(SENSOR_CHANNEL);
}

/* 测试时注入 Mock */
int mock_adc_read(uint8_t channel)
{
    return 1234;  /* 返回测试数据 */
}

void test_sensor_conversion(void)
{
    SensorHal_t hal = { .read_adc = mock_adc_read };
    int temp = read_sensor_value(&hal);
    assert(temp == expected_value);
}
```

### 2.2 测试框架选择

```c
/* Unity — 轻量级嵌入式测试框架 */
#include "unity.h"

void setUp(void) { /* 每个测试前调用 */ }
void tearDown(void) { /* 每个测试后调用 */ }

void test_crc_calculation(void)
{
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t crc = calculate_crc16(data, sizeof(data));
    TEST_ASSERT_EQUAL_HEX16(0x1234, crc);
}

void test_buffer_overflow_protection(void)
{
    uint8_t buf[10];
    size_t written = safe_write(buf, sizeof(buf), large_data, 20);
    TEST_ASSERT_EQUAL(10, written);  /* 应该截断 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_calculation);
    RUN_TEST(test_buffer_overflow_protection);
    return UNITY_END();
}
```

### 2.3 测试命名规范

```c
/* 命名格式：test_<模块>_<场景>_<期望结果> */

void test_crc16_valid_data_returns_correct_checksum(void);
void test_crc16_empty_data_returns_zero(void);
void test_parser_valid_frame_returns_success(void);
void test_parser_invalid_sof_returns_error(void);
void test_parser_crc_mismatch_returns_error(void);
void test_queue_full_returns_error(void);
void test_queue_empty_blocks_until_data(void);
```

### 2.4 测试用例模板

```c
/* 每个测试遵循 AAA 模式：Arrange-Act-Assert */

void test_ring_buffer_write_and_read(void)
{
    /* Arrange — 准备测试环境 */
    uint8_t buf[16];
    RingBuffer_t rb;
    ring_buffer_init(&rb, buf, sizeof(buf));
    uint8_t data[] = {1, 2, 3, 4, 5};
    uint8_t read_buf[5] = {0};

    /* Act — 执行被测函数 */
    size_t written = ring_buffer_write(&rb, data, sizeof(data));
    size_t read = ring_buffer_read(&rb, read_buf, sizeof(read_buf));

    /* Assert — 验证结果 */
    TEST_ASSERT_EQUAL(5, written);
    TEST_ASSERT_EQUAL(5, read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, read_buf, 5);
}
```

---

## 3. Mock 与 Stub

### 3.1 硬件 Mock

```c
/* Mock 寄存器访问 */
static uint32_t mock_adc_dr;

uint32_t mock_adc_read_register(uint32_t reg_addr)
{
    if (reg_addr == (uint32_t)&ADC1->DR) {
        return mock_adc_dr;
    }
    return 0;
}

void mock_adc_set_value(uint32_t value)
{
    mock_adc_dr = value;
}

/* Mock HAL 函数 */
static HAL_StatusTypeDef mock_i2c_result = HAL_OK;

HAL_StatusTypeDef HAL_I2C_Master_Transmit(
    I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
    uint8_t *pData, uint16_t Size, uint32_t Timeout)
{
    return mock_i2c_result;
}

void mock_i2c_set_result(HAL_StatusTypeDef result)
{
    mock_i2c_result = result;
}
```

### 3.2 函数指针 Mock

```c
/* 可替换的依赖函数 */
typedef struct {
    int (*read_sensor)(void);
    int (*write_flash)(uint32_t addr, const uint8_t *data, size_t len);
    uint32_t (*get_tick)(void);
} SystemApi_t;

/* 生产环境实现 */
static int real_read_sensor(void) { /* 硬件读取 */ }
static int real_write_flash(uint32_t addr, const uint8_t *data, size_t len) {
    /* Flash 写入 */
}
static uint32_t real_get_tick(void) { return HAL_GetTick(); }

const SystemApi_t g_real_api = {
    .read_sensor = real_read_sensor,
    .write_flash = real_write_flash,
    .get_tick = real_get_tick,
};

/* 测试环境 Mock */
static int mock_sensor_value = 25;
static int mock_read_sensor(void) { return mock_sensor_value; }

const SystemApi_t g_mock_api = {
    .read_sensor = mock_read_sensor,
    .write_flash = mock_write_flash,
    .get_tick = mock_get_tick,
};
```

---

## 4. 集成测试

### 4.1 模块间交互测试

```c
/* 测试传感器模块与数据处理模块的协作 */
void test_sensor_to_processor_integration(void)
{
    /* 配置 Mock 传感器返回特定值 */
    mock_adc_set_value(2048);  /* 对应 2.5V */

    /* 执行传感器读取 */
    SensorData_t data;
    int ret = sensor_read_all(&data);

    /* 验证数据处理正确性 */
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, data.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.3f, data.voltage);
}
```

### 4.2 状态机测试

```c
/* 测试状态转换 */
void test_state_machine_transitions(void)
{
    StateMachine_t sm;
    state_machine_init(&sm);

    /* 初始状态 */
    TEST_ASSERT_EQUAL(STATE_IDLE, sm.current_state);

    /* 发送启动命令 */
    state_machine_process(&sm, EVENT_START);
    TEST_ASSERT_EQUAL(STATE_RUNNING, sm.current_state);

    /* 发送停止命令 */
    state_machine_process(&sm, EVENT_STOP);
    TEST_ASSERT_EQUAL(STATE_IDLE, sm.current_state);

    /* 测试无效转换 */
    state_machine_process(&sm, EVENT_STOP);  /* 已经是 IDLE */
    TEST_ASSERT_EQUAL(STATE_IDLE, sm.current_state);
}

/* 测试状态机超时 */
void test_state_machine_timeout(void)
{
    StateMachine_t sm;
    state_machine_init(&sm);

    state_machine_process(&sm, EVENT_START);
    TEST_ASSERT_EQUAL(STATE_RUNNING, sm.current_state);

    /* 模拟超时 */
    mock_advance_tick(TIMEOUT_MS + 1);
    state_machine_process(&sm, EVENT_TICK);
    TEST_ASSERT_EQUAL(STATE_ERROR, sm.current_state);
}
```

---

## 5. HIL 测试

### 5.1 硬件测试框架

```c
/* HIL 测试需要与真实硬件交互 */

/* 测试夹具定义 */
typedef struct {
    UART_HandleTypeDef *debug_uart;
    GPIO_TypeDef *led_port;
    uint16_t led_pin;
    ADC_HandleTypeDef *test_adc;
} HilTestFixture_t;

/* 测试夹具初始化 */
void hil_setup(HilTestFixture_t *fixture)
{
    /* 初始化调试接口 */
    debug_uart_init(fixture->debug_uart);

    /* 初始化测试引脚 */
    gpio_init_output(fixture->led_port, fixture->led_pin);
}

/* 测试 LED 闪烁 */
void test_led_blink_hil(HilTestFixture_t *fixture)
{
    /* 控制 LED */
    HAL_GPIO_WritePin(fixture->led_port, fixture->led_pin, GPIO_PIN_SET);
    delay_ms(100);

    /* 测量电压验证 LED 状态 */
    uint16_t adc_value = adc_read_channel(fixture->test_adc, 0);
    float voltage = adc_value * 3.3f / 4096;

    /* LED 亮时电压应该较高 */
    TEST_ASSERT_GREATER_THAN(2.0f, voltage);

    /* 关闭 LED */
    HAL_GPIO_WritePin(fixture->led_port, fixture->led_pin, GPIO_PIN_RESET);
    delay_ms(100);

    adc_value = adc_read_channel(fixture->test_adc, 0);
    voltage = adc_value * 3.3f / 4096;
    TEST_ASSERT_LESS_THAN(1.0f, voltage);
}
```

### 5.2 通信接口测试

```c
/* SPI 环回测试 */
void test_spi_loopback(void)
{
    uint8_t tx_data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t rx_data[4] = {0};

    /* 配置 SPI 为环回模式（MOSI 连接到 MISO） */
    spi_enable_loopback(&hspi1);

    /* 执行传输 */
    int ret = spi_transfer(&spi_config, tx_data, rx_data, sizeof(tx_data));

    /* 验证数据一致 */
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx_data, rx_data, sizeof(tx_data));
}

/* I2C 设备扫描 */
void test_i2c_device_scan(void)
{
    uint8_t found_devices[128];
    int count = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        if (i2c_probe(&i2c_config, addr) == 0) {
            found_devices[count++] = addr;
            printf("Found I2C device at 0x%02X\n", addr);
        }
    }

    /* 验证至少找到一个设备 */
    TEST_ASSERT_GREATER_THAN(0, count);
}
```

---

## 6. 测试自动化

### 6.1 CI/CD 集成

```yaml
# .github/workflows/test.yml
name: Embedded Tests

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install Dependencies
        run: |
          sudo apt-get install gcc-arm-none-eabi
          pip install unity-test
      
      - name: Build Tests
        run: |
          cd tests
          mkdir build && cd build
          cmake ..
          make
      
      - name: Run Unit Tests
        run: |
          cd tests/build
          ./run_tests
      
      - name: Generate Coverage
        run: |
          lcov --capture --directory . --output-file coverage.info
          genhtml coverage.info --output-directory coverage
```

### 6.2 测试报告

```c
/* 测试结果记录 */
typedef struct {
    const char *test_name;
    bool passed;
    uint32_t duration_ms;
    const char *error_msg;
} TestResult_t;

static TestResult_t g_test_results[100];
static int g_test_count = 0;

void record_test_result(const char *name, bool passed,
                        uint32_t duration, const char *error)
{
    if (g_test_count < ARRAY_SIZE(g_test_results)) {
        g_test_results[g_test_count++] = (TestResult_t){
            .test_name = name,
            .passed = passed,
            .duration_ms = duration,
            .error_msg = error,
        };
    }
}

void print_test_summary(void)
{
    int passed = 0, failed = 0;
    for (int i = 0; i < g_test_count; i++) {
        if (g_test_results[i].passed) passed++;
        else failed++;
    }

    printf("\n=== Test Summary ===\n");
    printf("Total: %d, Passed: %d, Failed: %d\n",
           g_test_count, passed, failed);
    printf("Pass rate: %.1f%%\n",
           100.0f * passed / g_test_count);
}
```

---

## 7. 测试覆盖率

### 7.1 代码覆盖率测量

```bash
# 使用 gcov 测量覆盖率
gcc -fprofile-arcs -ftest-coverage -o test_app test.c
./test_app
gcov test.c
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

### 7.2 分支覆盖率目标

```c
/* 确保所有分支都被测试 */

/* 有 4 个分支的函数 */
int classify_value(int value)
{
    if (value < 0) return -1;        /* 分支 1 */
    if (value == 0) return 0;        /* 分支 2 */
    if (value < 100) return 1;       /* 分支 3 */
    return 2;                        /* 分支 4 */
}

/* 测试所有分支 */
void test_classify_value_all_branches(void)
{
    TEST_ASSERT_EQUAL(-1, classify_value(-10));
    TEST_ASSERT_EQUAL(0, classify_value(0));
    TEST_ASSERT_EQUAL(1, classify_value(50));
    TEST_ASSERT_EQUAL(2, classify_value(100));
}
```

---

## 嵌入式测试检查清单

- [ ] 每个模块有对应的单元测试
- [ ] 测试遵循 AAA 模式（Arrange-Act-Assert）
- [ ] 测试命名清晰描述场景和期望
- [ ] 硬件依赖通过 Mock/Stub 隔离
- [ ] 关键路径有集成测试
- [ ] 状态机测试覆盖所有转换
- [ ] 边界条件和错误路径有测试
- [ ] CI/CD 自动运行测试
- [ ] 代码覆盖率 > 80%
- [ ] 测试失败有清晰的错误信息
