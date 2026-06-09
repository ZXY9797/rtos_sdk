# 通信协议实现规范

嵌入式系统中常见的通信接口（SPI、I2C、UART）
是与外部设备交互的基础。正确的实现确保可靠性和可维护性。

---

## 1. SPI 驱动规范

### 1.1 SPI 配置标准化

```c
/* SPI 配置结构体 */
typedef struct {
    SPI_Instance  *instance;     /* SPI 外设实例 */
    GPIO_Port     *cs_port;      /* 片选 GPIO 端口 */
    uint16_t       cs_pin;       /* 片选 GPIO 引脚 */
    uint32_t       max_speed_hz; /* 最大时钟频率 */
    SPI_Mode       mode;         /* CPOL/CPHA 配置 */
    uint8_t        bits;         /* 数据位数（8/16） */
} SPI_Config_t;

/* 标准模式定义 */
typedef enum {
    SPI_MODE_0 = 0,  /* CPOL=0, CPHA=0 — 最常用 */
    SPI_MODE_1 = 1,  /* CPOL=0, CPHA=1 */
    SPI_MODE_2 = 2,  /* CPOL=1, CPHA=0 */
    SPI_MODE_3 = 3,  /* CPOL=1, CPHA=1 */
} SPI_Mode;
```

### 1.2 片选管理

```c
/* 片选信号管理 */
void spi_cs_select(const SPI_Config_t *cfg)
{
    HAL_GPIO_WritePin(cfg->cs_port, cfg->cs_pin, GPIO_PIN_RESET);
    /* 某些设备需要延时 */
    delay_us(1);
}

void spi_cs_deselect(const SPI_Config_t *cfg)
{
    HAL_GPIO_WritePin(cfg->cs_port, cfg->cs_pin, GPIO_PIN_SET);
    /* 片选恢复时间 */
    delay_us(1);
}

/* 完整的 SPI 传输 */
int spi_transfer(const SPI_Config_t *cfg,
                 const uint8_t *tx, uint8_t *rx, size_t len)
{
    int ret = 0;

    spi_cs_select(cfg);

    if (HAL_SPI_TransmitReceive(cfg->instance, (uint8_t *)tx,
                                rx, len, 100) != HAL_OK) {
        ret = ERR_IO;
    }

    spi_cs_deselect(cfg);
    return ret;
}
```

### 1.3 SPI 设备驱动示例

```c
/* W25Q Flash 驱动 */
typedef struct {
    SPI_Config_t  spi;
    uint32_t      capacity;
} W25Q_Driver_t;

int w25q_read_jedec_id(W25Q_Driver_t *dev, uint8_t *id)
{
    uint8_t cmd = 0x9F;
    uint8_t resp[3];

    int ret = spi_transfer(&dev->spi, &cmd, resp, 4);
    if (ret == 0) {
        id[0] = resp[1];  /* Manufacturer */
        id[1] = resp[2];  /* Memory Type */
        id[2] = resp[3];  /* Capacity */
    }
    return ret;
}

int w25q_read(W25Q_Driver_t *dev, uint32_t addr,
              uint8_t *buf, size_t len)
{
    uint8_t cmd[4] = {
        0x03,                    /* Read command */
        (addr >> 16) & 0xFF,
        (addr >> 8) & 0xFF,
        addr & 0xFF,
    };

    spi_cs_select(&dev->spi);
    spi_transmit(&dev->spi, cmd, 4);
    int ret = spi_receive(&dev->spi, buf, len);
    spi_cs_deselect(&dev->spi);

    return ret;
}
```

---

## 2. I2C 驱动规范

### 2.1 I2C 配置

```c
typedef struct {
    I2C_Instance  *instance;
    uint32_t       speed_hz;     /* 100kHz/400kHz/1MHz */
    uint16_t       timeout_ms;
} I2C_Config_t;

/* I2C 速度模式 */
#define I2C_SPEED_STANDARD   100000   /* 100kHz */
#define I2C_SPEED_FAST       400000   /* 400kHz */
#define I2C_SPEED_FAST_PLUS  1000000  /* 1MHz */
```

### 2.2 I2C 基础操作

```c
/* 写操作 */
int i2c_write(const I2C_Config_t *cfg, uint8_t dev_addr,
              const uint8_t *data, size_t len)
{
    if (HAL_I2C_Master_Transmit(cfg->instance,
                                dev_addr << 1,
                                (uint8_t *)data, len,
                                cfg->timeout_ms) != HAL_OK) {
        return ERR_IO;
    }
    return 0;
}

/* 读操作 */
int i2c_read(const I2C_Config_t *cfg, uint8_t dev_addr,
             uint8_t *data, size_t len)
{
    if (HAL_I2C_Master_Receive(cfg->instance,
                               dev_addr << 1,
                               data, len,
                               cfg->timeout_ms) != HAL_OK) {
        return ERR_IO;
    }
    return 0;
}

/* 寄存器写 */
int i2c_write_reg(const I2C_Config_t *cfg, uint8_t dev_addr,
                  uint8_t reg_addr, uint8_t value)
{
    uint8_t buf[2] = { reg_addr, value };
    return i2c_write(cfg, dev_addr, buf, 2);
}

/* 寄存器读 */
int i2c_read_reg(const I2C_Config_t *cfg, uint8_t dev_addr,
                 uint8_t reg_addr, uint8_t *value)
{
    /* 先写寄存器地址 */
    if (i2c_write(cfg, dev_addr, &reg_addr, 1) != 0) {
        return ERR_IO;
    }
    /* 再读数据 */
    return i2c_read(cfg, dev_addr, value, 1);
}
```

### 2.3 I2C 设备驱动示例

```c
/* BME280 传感器驱动 */
typedef struct {
    I2C_Config_t  i2c;
    uint8_t       dev_addr;
    int16_t       temp_calib[3];  /* 校准数据 */
} BME280_Driver_t;

int bme280_init(BME280_Driver_t *dev)
{
    uint8_t chip_id;
    int ret;

    /* 读取芯片 ID */
    ret = i2c_read_reg(&dev->i2c, dev->dev_addr, 0xD0, &chip_id);
    if (ret != 0 || chip_id != 0x60) {
        return ERR_DEVICE;
    }

    /* 软复位 */
    ret = i2c_write_reg(&dev->i2c, dev->dev_addr, 0xE0, 0xB6);
    if (ret != 0) return ret;
    delay_ms(10);

    /* 读取校准数据 */
    uint8_t calib[6];
    ret = i2c_read_regs(&dev->i2c, dev->dev_addr, 0x88, calib, 6);
    if (ret != 0) return ret;

    /* 解析校准数据 */
    dev->temp_calib[0] = (int16_t)(calib[1] << 8 | calib[0]);
    dev->temp_calib[1] = (int16_t)(calib[3] << 8 | calib[2]);
    dev->temp_calib[2] = (int16_t)(calib[5] << 8 | calib[4]);

    return 0;
}

int bme280_read_temperature(BME280_Driver_t *dev, float *temp)
{
    uint8_t data[3];
    int ret = i2c_read_regs(&dev->i2c, dev->dev_addr, 0xFA, data, 3);
    if (ret != 0) return ret;

    int32_t adc_T = (int32_t)data[0] << 12 |
                    (int32_t)data[1] << 4 |
                    (int32_t)data[2] >> 4;

    /* 温度补偿计算 */
    int32_t var1 = (adc_T / 8 - dev->temp_calib[0] * 2) *
                   dev->temp_calib[1] / 2048;
    int32_t var2 = (adc_T / 16384 - dev->temp_calib[0]) *
                   (adc_T / 16384 - dev->temp_calib[0]) / 4096 *
                   dev->temp_calib[2] / 16384;

    *temp = (var1 + var2) / 5120.0f;
    return 0;
}
```

### 2.4 I2C 总线恢复

```c
/* I2C 总线锁死恢复 */
int i2c_bus_recovery(I2C_Config_t *cfg, GPIO_Port *sda_port,
                     uint16_t sda_pin, GPIO_Port *scl_port,
                     uint16_t scl_pin)
{
    /* 配置为 GPIO 模式 */
    GPIO_InitTypeDef gpio = {
        .Pin   = sda_pin | scl_pin,
        .Mode  = GPIO_MODE_OUTPUT_OD,
        .Pull  = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_HIGH,
    };
    HAL_GPIO_Init(sda_port, &gpio);

    /* 发送 9 个时钟脉冲 */
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_SET);
        delay_us(5);
        HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_RESET);
        delay_us(5);
    }

    /* 发送停止条件 */
    HAL_GPIO_WritePin(sda_port, sda_pin, GPIO_PIN_RESET);
    delay_us(5);
    HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_SET);
    delay_us(5);
    HAL_GPIO_WritePin(sda_port, sda_pin, GPIO_PIN_SET);
    delay_us(5);

    /* 恢复 I2C 外设模式 */
    HAL_I2C_DeInit(cfg->instance);
    HAL_I2C_Init(cfg->instance);

    return 0;
}
```

---

## 3. UART 驱动规范

### 3.1 UART 配置

```c
typedef struct {
    UART_Instance  *instance;
    uint32_t        baudrate;
    uint8_t         databits;     /* 8 或 9 */
    uint8_t         stopbits;     /* 1 或 2 */
    uint8_t         parity;       /* 0=无, 1=奇, 2=偶 */
    GPIO_Port      *tx_port;
    uint16_t        tx_pin;
    GPIO_Port      *rx_port;
    uint16_t        rx_pin;
} UART_Config_t;
```

### 3.2 环形缓冲区

```c
typedef struct {
    uint8_t  *buffer;
    size_t    size;
    volatile size_t head;
    volatile size_t tail;
} RingBuffer_t;

void ring_buffer_init(RingBuffer_t *rb, uint8_t *buf, size_t size)
{
    rb->buffer = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

size_t ring_buffer_write(RingBuffer_t *rb, const uint8_t *data, size_t len)
{
    size_t written = 0;
    while (written < len) {
        size_t next = (rb->head + 1) % rb->size;
        if (next == rb->tail) {
            break;  /* 缓冲区满 */
        }
        rb->buffer[rb->head] = data[written++];
        rb->head = next;
    }
    return written;
}

size_t ring_buffer_read(RingBuffer_t *rb, uint8_t *data, size_t len)
{
    size_t read = 0;
    while (read < len && rb->tail != rb->head) {
        data[read++] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
    }
    return read;
}

size_t ring_buffer_count(const RingBuffer_t *rb)
{
    return (rb->head - rb->tail + rb->size) % rb->size;
}
```

### 3.3 UART 中断驱动

```c
/* UART 驱动上下文 */
typedef struct {
    UART_Config_t  config;
    RingBuffer_t   rx_ring;
    RingBuffer_t   tx_ring;
    uint8_t        rx_buf[256];
    uint8_t        tx_buf[256];
    os_sem_t       rx_sem;
} UART_Driver_t;

static UART_Driver_t g_uart1;

int uart_init(const UART_Config_t *cfg)
{
    UART_Driver_t *drv = &g_uart1;
    drv->config = *cfg;

    ring_buffer_init(&drv->rx_ring, drv->rx_buf, sizeof(drv->rx_buf));
    ring_buffer_init(&drv->tx_ring, drv->tx_buf, sizeof(drv->tx_buf));
    os_sem_init(&drv->rx_sem, 0);

    /* 配置 UART 硬件 */
    uart_hw_init(cfg);

    /* 使能接收中断 */
    uart_enable_rx_interrupt(cfg->instance);

    return 0;
}

/* 中断处理 */
void USART1_IRQHandler(void)
{
    UART_Driver_t *drv = &g_uart1;
    uint32_t sr = drv->config.instance->SR;

    /* 接收中断 */
    if (sr & USART_SR_RXNE) {
        uint8_t data = drv->config.instance->DR;
        ring_buffer_write(&drv->rx_ring, &data, 1);
        os_sem_give_from_isr(&drv->rx_sem);
    }

    /* 发送中断 */
    if (sr & USART_SR_TXE && (drv->config.instance->CR1 & USART_CR1_TXEIE)) {
        uint8_t data;
        if (ring_buffer_read(&drv->tx_ring, &data, 1) > 0) {
            drv->config.instance->DR = data;
        } else {
            drv->config.instance->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

/* 读取函数 */
size_t uart_read(uint8_t *data, size_t len, uint32_t timeout_ms)
{
    UART_Driver_t *drv = &g_uart1;
    size_t total_read = 0;

    while (total_read < len) {
        /* 先读取缓冲区中已有数据 */
        size_t n = ring_buffer_read(&drv->rx_ring,
                                    data + total_read,
                                    len - total_read);
        total_read += n;

        if (total_read >= len) break;

        /* 等待新数据 */
        if (os_sem_take(&drv->rx_sem, timeout_ms) != OS_OK) {
            break;  /* 超时 */
        }
    }

    return total_read;
}
```

---

## 4. 协议层设计

### 4.1 帧格式定义

```c
/* 通用帧格式 */
typedef struct __packed {
    uint8_t  sof;         /* 帧起始标志 0xAA */
    uint8_t  cmd;         /* 命令字 */
    uint16_t length;      /* 数据长度 */
    uint8_t  data[];      /* 数据载荷 */
    /* uint16_t crc16;    CRC 校验（跟随数据后） */
} ProtocolFrame_t;

#define FRAME_SOF  0xAA
#define FRAME_HEADER_SIZE  4
#define FRAME_CRC_SIZE     2
#define FRAME_MAX_DATA_LEN 256
```

### 4.2 帧解析状态机

```c
typedef enum {
    PARSE_STATE_SOF,
    PARSE_STATE_CMD,
    PARSE_STATE_LEN_H,
    PARSE_STATE_LEN_L,
    PARSE_STATE_DATA,
    PARSE_STATE_CRC_H,
    PARSE_STATE_CRC_L,
} ParseState_t;

typedef struct {
    ParseState_t  state;
    uint8_t       cmd;
    uint16_t      length;
    uint16_t      index;
    uint8_t       data[FRAME_MAX_DATA_LEN];
    uint16_t      crc;
    uint16_t      recv_crc;
} FrameParser_t;

void frame_parser_init(FrameParser_t *parser)
{
    parser->state = PARSE_STATE_SOF;
}

int frame_parser_feed(FrameParser_t *parser, uint8_t byte)
{
    switch (parser->state) {
    case PARSE_STATE_SOF:
        if (byte == FRAME_SOF) {
            parser->state = PARSE_STATE_CMD;
        }
        break;

    case PARSE_STATE_CMD:
        parser->cmd = byte;
        parser->state = PARSE_STATE_LEN_H;
        break;

    case PARSE_STATE_LEN_H:
        parser->length = byte << 8;
        parser->state = PARSE_STATE_LEN_L;
        break;

    case PARSE_STATE_LEN_L:
        parser->length |= byte;
        if (parser->length > FRAME_MAX_DATA_LEN) {
            parser->state = PARSE_STATE_SOF;
            return ERR_PROTOCOL;
        }
        parser->index = 0;
        parser->state = PARSE_STATE_DATA;
        break;

    case PARSE_STATE_DATA:
        parser->data[parser->index++] = byte;
        if (parser->index >= parser->length) {
            parser->state = PARSE_STATE_CRC_H;
        }
        break;

    case PARSE_STATE_CRC_H:
        parser->recv_crc = byte << 8;
        parser->state = PARSE_STATE_CRC_L;
        break;

    case PARSE_STATE_CRC_L:
        parser->recv_crc |= byte;
        parser->state = PARSE_STATE_SOF;

        /* 验证 CRC */
        parser->crc = crc16(parser->data, parser->length);
        if (parser->crc == parser->recv_crc) {
            return 0;  /* 帧完整且校验通过 */
        }
        return ERR_CRC;
    }

    return ERR_INCOMPLETE;
}
```

---

## 5. 通信错误处理

### 5.1 重试机制

```c
/* 带重试的通信函数 */
int comm_with_retry(CommFunc func, void *ctx, int max_retries)
{
    int ret;
    int retry_count = 0;

    do {
        ret = func(ctx);
        if (ret == 0) {
            return 0;  /* 成功 */
        }

        retry_count++;
        if (retry_count < max_retries) {
            LOG_WARN("Comm failed (ret=%d), retry %d/%d",
                     ret, retry_count, max_retries);
            delay_ms(10 * retry_count);  /* 指数退避 */
        }
    } while (retry_count < max_retries);

    LOG_ERROR("Comm failed after %d retries", max_retries);
    return ret;
}

/* 使用示例 */
int read_sensor_data(SensorCtx_t *ctx)
{
    return comm_with_retry(sensor_read_impl, ctx, 3);
}
```

### 5.2 超时处理

```c
/* 带超时的等待 */
int wait_for_response(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (!response_received()) {
        if (HAL_GetTick() - start > timeout_ms) {
            LOG_ERROR("Response timeout");
            return ERR_TIMEOUT;
        }
        os_delay_ms(1);
    }

    return 0;
}
```

### 5.3 通信状态监控

```c
typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t error_count;
    uint32_t timeout_count;
    uint32_t crc_error_count;
    uint32_t last_error_tick;
} CommStats_t;

static CommStats_t g_comm_stats;

void comm_record_error(int error_type)
{
    g_comm_stats.error_count++;
    g_comm_stats.last_error_tick = HAL_GetTick();

    switch (error_type) {
    case ERR_TIMEOUT:
        g_comm_stats.timeout_count++;
        break;
    case ERR_CRC:
        g_comm_stats.crc_error_count++;
        break;
    }
}

void print_comm_stats(void)
{
    printf("Communication Statistics:\n");
    printf("  TX: %lu, RX: %lu\n",
           g_comm_stats.tx_count, g_comm_stats.rx_count);
    printf("  Errors: %lu (Timeout: %lu, CRC: %lu)\n",
           g_comm_stats.error_count,
           g_comm_stats.timeout_count,
           g_comm_stats.crc_error_count);
}
```

---

## 通信协议检查清单

- [ ] SPI 片选信号正确管理（选择/释放）
- [ ] I2C 总线有恢复机制
- [ ] UART 使用环形缓冲区和中断驱动
- [ ] 协议帧有起始标志和校验
- [ ] 帧解析使用状态机实现
- [ ] 通信失败有重试机制
- [ ] 所有阻塞操作有超时保护
- [ ] 通信统计数据可用于诊断
- [ ] 错误码统一定义和处理
- [ ] 多设备访问有互斥保护
