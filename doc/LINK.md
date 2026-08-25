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

Router 只接受单播 sender；`0xFF` 不能作为 receiver。Router 尚未配置有效本机单播
地址时禁止发送，也不会本地分发广播帧，避免启动/关闭窗口产生伪 ACK 或回环。
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
| BLE 4.x | 19 | 20B ATT payload - 1B fragment header |
| BLE 5.0 | 243 | 244B ATT payload - 1B fragment header |

当前 CAN backend 仅支持 classic CAN，构造时可在 `1..7` 范围调整；BLE 可通过
`BleLink(243)` 适配 247-byte MTU。

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
| BLE | 无（协议栈回调） | 静态完整帧队列 | 回调重组完整帧后入队，Router 逐字节消费 |

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

`SYS_INIT` 在 `APPLICATION` 阶段、底层 Link 设备初始化之后：
1. 从 `LinkRegistry` 拷贝已注册的链路
2. 加载 `.link_handler` section 中的回调
3. 创建周期任务（默认 1kHz），由 `Router::process()` 轮询和处理所有链路

任务频率通过 Kconfig 配置：
```
CONFIG_LINK_PROCESS_HZ=1000    # 任务频率
CONFIG_LINK_PROCESS_STACK=2048 # 栈大小
CONFIG_LINK_PROCESS_PRIO=5     # 优先级
CONFIG_LINK_INIT_PRIORITY=95   # 生命周期顺序：设备之后、watchdog monitor 之前
CONFIG_LINK_PROCESS_RX_BUDGET=1024 # 每链路、每周期最大接收字节数
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
router.set_self_addr(link::make_addr(0x1, 0)); // 编码结果为 0x10

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

超时重发时 `cmd_type.retransmit` 位置 1。Router 使用固定容量 replay cache，以发送方、
接收方、sequence、命令、除 retransmit 外的命令属性和完整受保护 payload 精确识别
请求：完全相同的重传不会再次执行 handler，而是重放上次 ACK 状态；相同 key 但任何
属性或 payload 冲突时，明文链路返回 `0xFD`，加密链路静默丢弃并计入安全统计，避免
在原响应上下文下生成不同密文而重复使用 AEAD nonce。缓存不使用短 CRC 代替完整比较，
避免碰撞后误判幂等。
ACK 必须携带至少一个状态字节，并与待确认请求的物理链路、发送方、序列号及命令号
全部匹配；不匹配的 ACK 不会清除 pending 项。16-bit 序列号回绕时也不会复用仍在等待
确认的序列号。

当前 section handler 是同步回调：回调返回即表示该命令处理完成。非广播请求只要
`ack_mode != No`，Router 就在回调返回后发送最终状态；找不到 handler 时返回失败状态。
异步长任务不得把回调返回误当作完成，应由后续异步 handler API 扩展后再使用
`Finish/Progress` 语义。回调中的 `Frame::data` 只在本次回调期间有效，不得保存指针。

### 安全命令

启用 `CONFIG_LINK_SECURITY=y` 后，`LINK_SECURE_HANDLER()` 可逐命令要求认证加密；
`CONFIG_LINK_REQUIRE_SECURE_COMMANDS=y` 可强制所有本机收发命令使用安全模式。开启全局
策略会自动选择 `LINK_SECURITY`。任一安全模式下，产品必须提供：

```text
app/product/<product>/common/link_security.cc
```

并实现 `link::product_security_provider()`。Provider 必须使用真实 AEAD，把
`SecurityContext` 全部字段绑定为 AAD，保证不同消息的 nonce 唯一、验 tag 后才释放明文，并从
受保护存储获取密钥。仓库不提供默认密钥或伪加密；缺文件时 CMake 失败，返回空 provider
时 Router 初始化失败。明文访问安全 handler 返回 `0xFE` 且不执行回调。

`retransmit` 位不进入 AAD，因为发送端会在原始受保护帧上原地设置该位并重算帧 CRC；
其余 ack mode、priority、地址、sequence 和命令字段都进入 AAD，不能被中间链路修改。

重传复用原始受保护帧；接收端先用 replay cache 识别完全相同的副本，再决定是否调用
Provider，因此既不会重复执行业务，也不会因 AEAD nonce replay 把合法 ACK 重传路径
误判为新命令。同一请求的 ACK 重放会以相同 `SecurityContext` 和相同状态重新调用
`protect()`；使用派生 nonce 的 provider 必须确定性地产生与首个 ACK 相同的受保护载荷，
不得把它当作不同明文的 nonce 复用。重放缓存的静态 RAM 约为
`LINK_REPLAY_CACHE_SIZE * LINK_MAX_FRAME_SIZE`，产品必须结合 map 文件配置条目数和帧长。
缓存条目数还必须覆盖 retention 窗口内可能完成的唯一需应答请求数；安全 provider 自身
仍必须拒绝缓存淘汰后的旧 nonce 重放。

`Router::stats()` 原子快照接收帧、解码错误、转发/发送、安全丢弃、重复请求、冲突和
ACK timeout。Link 处理线程使用调用方持有对象、静态栈和静态内核控制块，不占 RTOS
heap；Router 使用的 mutex/semaphore 控制块同样内联在 OSAL 对象中。

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
config LINK_PROCESS_STACK     # 处理任务栈大小，默认 2048
config LINK_PROCESS_PRIO      # 处理任务优先级，默认 5
config LINK_INIT_PRIORITY     # 生命周期 init 优先级，默认 95
config LINK_PROCESS_RX_BUDGET # 每链路/周期接收预算，默认 1024 字节
config LINK_UART_TX_TIMEOUT_MS # UART 单帧有界发送窗口，默认 100ms
config LINK_REPLAY_CACHE_SIZE  # 幂等请求缓存条目，默认 16
config LINK_REPLAY_RETENTION_MS # replay 保留窗口，默认 10s
config LINK_SECURITY             # 启用产品 AEAD provider
config LINK_REQUIRE_SECURE_COMMANDS # 全局 fail-closed 安全策略
endif
```

`LINK_UART_TX_TIMEOUT_MS` 必须按产品波特率和最大帧长校核。UART 后端是有界同步发送，
因此不会继续持有 Router 的 TX 缓冲；若产品需要在多链路大流量下消除队头阻塞，应在
具体 Link 后端增加独立静态 TX 队列与 worker，而不是让驱动保存传入指针。
`LINK_PROCESS_RX_BUDGET` 和 CAN 单次 poll 帧预算共同限制异常流量占用，未处理数据留到
下一周期，避免饿死其他链路、ACK 超时检查和健康监控。

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
