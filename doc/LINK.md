# Link 通信协议栈

## 概述

Link 是一个轻量级多链路通信协议栈，支持 UART、CAN、BLE 等物理链路，提供帧封装、路由转发、ACK 应答、超时重传、分片重组等能力。

业务层只需配置路由表，链路注册和协议处理通过 `SYS_INIT` 自动完成。

---

## 帧格式

```
偏移  字段          大小    说明
----  -----------   ----    ----
0     SOF           1B      固定 0xAA
1     ver_len       2B      len[9:0] | ver[11:10] | resv[15:12]
3     head_crc      1B      前 3 字节 BCC(XOR)
4     cmd_type      1B      is_ack | ack_mode | enc_mode | priority | retransmit
5     sender_id     1B      host_id[7:4] | host_idx[3:0]
6     receiver_id   1B      host_id[7:4] | host_idx[3:0]
7     seq           2B      消息序列号
9     cmd_set       1B      命令集
10    cmd_id        1B      命令 ID
11    data[]        变长    载荷
尾部  CRC16         2B      CRC-16/CCITT
```

帧头 11 字节，总帧长 = 11 + data_len + 2。

### cmd_type 位域

```
bit:  7       6           5          4..3       2..1        0
      resv    retransmit  priority   enc_mode   ack_mode    is_ack
```

| 字段 | 值 | 含义 |
|------|----|------|
| is_ack | 0/1 | 普通帧 / 应答帧 |
| ack_mode | 0/1/2/3 | 无需应答 / 立即应答 / 执行完成应答 / 分阶段应答 |
| enc_mode | 0/1/2/3 | 无加密 / AES-128 / AES-256 / ChaCha20 |
| priority | 0/1 | 普通 / 高优先级 |
| retransmit | 0/1 | 首次发送 / 重传副本 |

### 地址编码

```
bit:  7..4      3..0
      host_id   host_idx
```

- `0x00`：广播地址
- `0xFF`：保留
- `host_idx=0xF`：同类型组播

---

## 架构

```
  ┌─────────────────────────────────────────────────┐
  │  应用层                                          │
  │  只配置路由表: router().set_routes(...)           │
  │  注册回调: LINK_HANDLER(set, id, cb, arg)         │
  └────────────────────┬────────────────────────────┘
                       │
  ┌────────────────────▼────────────────────────────┐
  │  Router 层                                       │
  │  路由表 + 多路由 round-robin + 回调分发            │
  │  ACK 应答 + 超时重传队列                          │
  │  SYS_INIT 自动创建 1kHz 处理任务                  │
  └────────────────────┬────────────────────────────┘
                       │
  ┌────────────────────▼────────────────────────────┐
  │  Link 层                                         │
  │  FrameCodec: 字节流 ↔ 帧（状态机解包 + 打包）      │
  │  Fragmenter / Reassembler: 分片发送 + 重组        │
  └────────────────────┬────────────────────────────┘
                       │
  ┌────────────────────▼────────────────────────────┐
  │  Link 子类（构造时自动注册到 Router）              │
  │  UartLink · CanLink · BleLink                    │
  └─────────────────────────────────────────────────┘
```

---

## 分片重组

CAN（8B/64B）、BLE（MTU 受限）等链路需要分片传输。分片逻辑在 Link 层完成，Driver 层只负责收发单帧。

### 分片格式

```
[frag_hdr(1B) | payload(最多 N 字节)]
frag_hdr: bit7 = 最后一片标志, bit6..0 = 片索引
```

### 分片大小配置

| 链路 | 默认 frag_payload | 说明 |
|------|-------------------|------|
| CAN classic | 7 | 8B 帧 - 1B header |
| CAN FD | 63 | 64B 帧 - 1B header |
| BLE 4.x | 20 | MTU=23 - ATT 3B |
| BLE 5.0 | 244 | MTU=247 - ATT 3B |

构造时可自定义：`CanLink(port, id, 63)` / `BleLink(244)`。

### 数据流

```
发送: 完整 link 帧 → Fragmenter 按 frag_payload 切片 → 逐片交给 Driver
接收: Driver 收到单帧 → Fragmenter::recv → Reassembler 重组 → 完整 link 帧
```

---

## 分层 Buffer 策略

| 链路 | Driver 层 buffer | Link 层 buffer | 说明 |
|------|-------------------|----------------|------|
| UART | StreamBuffer（ISR 写） | 无 | 字节流协议，ISR 必须缓冲 |
| CAN | 无（硬件 FIFO） | 无 | Link poll 直接读硬件 FIFO |
| BLE | 无（协议栈回调） | StreamBuffer | on_receive 写入，poll 读出 |

**原则**：在数据进入的第一个点做缓冲，避免丢失。

---

## 自动初始化

### Link 子类注册

```cpp
// 构造函数自动调用 register_link(this)
static link::UartLink g_uart_link(device_get(uart0));
static link::CanLink  g_can_link(0, 0x100);
```

### Router 自动初始化

`SYS_INIT` 在 `POST_KERNEL` 阶段：
1. 从 `LinkRegistry` 拷贝已注册的链路
2. 加载 `.link_handler` section 中的回调
3. 创建周期任务（默认 1kHz），自动调用所有链路的 `poll()` + `Router::process()`

任务频率通过 Kconfig 配置：
```
CONFIG_LINK_PROCESS_HZ=1000    # 任务频率
CONFIG_LINK_PROCESS_STACK=1024 # 栈大小
CONFIG_LINK_PROCESS_PRIO=5     # 优先级
```

---

## 路由配置

### 路由模式

| 模式 | 匹配规则 | 用途 |
|------|---------|------|
| ByHost | `(receiver & mask) == (match_addr & mask)` | 按 host_id 路由 |
| Direct | 匹配来源 link_id | 透传/桥接模式 |

### 流式 API

```cpp
auto &router = link::Router::instance();
router.set_self_addr(link::make_addr(0x10, 0));

static const link::RouteEntry routes[] = {
    link::make_route(link::route_by_host(0x10, 0xF0).to(1)),       // PC ↔ UART
    link::make_route(link::route_by_host(0x40, 0xF0).to(2)),       // 主控 ↔ CAN
    link::make_route(link::route_by_host(0x10, 0xF0).to(3)),       // PC ↔ BLE（多路由）
    link::make_route(link::route_direct(0).to(1)),                  // 桥接：任意 → UART
};
router.set_routes(routes, 4);
```

多路由（同一 host_id 对应多个链路）自动 round-robin 选择。

---

## 回调注册

```cpp
void on_motor_cmd(const link::Frame &frame, void *arg) {
    // 处理电机命令
}

// 注册到 .link_handler section，SYS_INIT 时自动加载
LINK_HANDLER(0x01, 0x01, on_motor_cmd, nullptr);
```

---

## ACK 与重传

| ack_mode | 超时 | 最大重试 | 说明 |
|----------|------|---------|------|
| No | — | 0 | 发后不管 |
| Now | 10ms | 3 | 快速应答 |
| Finish | 500ms | 2 | 等待执行完成 |
| Progress | 2s | 1 | 长任务 |

超时重发时 `cmd_type.retransmit` 位置 1，接收方可做丢包检测/去重。

---

## Kconfig

```
menuconfig LINK
    bool "Link 通信协议栈"
    default y

if LINK
config LINK_MAX_FRAME_SIZE    # 最大帧长度，默认 512
config LINK_MAX_LINKS         # 最大链路数，默认 8
config LINK_MAX_ROUTES        # 最大路由规则数，默认 16
config LINK_MAX_PENDING       # 最大待确认帧数，默认 8
config LINK_PROCESS_HZ        # 处理任务频率，默认 1000
config LINK_PROCESS_STACK     # 处理任务栈大小，默认 1024
config LINK_PROCESS_PRIO      # 处理任务优先级，默认 5
endif
```

关闭 `CONFIG_LINK=n` 时 link 库不参与编译，零开销。

---

## 文件结构

```
component/link/
├── Kconfig
├── CMakeLists.txt
├── include/link/
│   ├── frame.h        # 帧结构体、地址编码、BCC/CRC16
│   ├── codec.h        # FrameCodec 状态机 + 打包
│   ├── link.h         # Link 基类 + LinkRegistry
│   ├── fragment.h     # Reassembler + Fragmenter（分片重组）
│   ├── router.h       # Router 单例 + 路由表 + ACK + 重传
│   ├── uart_link.h    # UartLink 子类
│   ├── can_link.h     # CanLink 子类（含分片）
│   └── ble_link.h     # BleLink 子类（含分片 + StreamBuffer）
└── src/
    ├── codec.cc       # FrameCodec 实现
    ├── crc16.cc       # CRC-16/CCITT 查表法
    └── router.cc      # Router 实现 + SYS_INIT
```
