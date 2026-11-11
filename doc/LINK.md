# Link 通信协议栈

## 概述

Link 是一个静态内存、多链路通信协议栈，支持 UART、CAN、BLE 等物理链路，
提供帧封装、静态路由、分阶段 ACK、异步完成、超时重传、重放防护、优先级
业务调度、远程诊断和分片重组等能力。

业务层只需配置路由表，链路注册和协议处理通过 `SYS_INIT` 自动完成。

---

## 帧格式

```
偏移  字段          大小    说明
----  -----------   ----    ----
0     SOF           1B      固定 0xAA
1     ver_len       2B      len[9:0] | ver[13:10] | resv[15:14]
3     head_crc      1B      前 3 字节 CRC-8/SMBUS
4     cmd_type      1B      is_ack | ack_mode | enc_mode | priority | retransmit
5     sender_id     1B      host_id[7:4] | host_idx[3:0]
6     receiver_id   1B      host_id[7:4] | host_idx[3:0]
7     seq           2B      消息序列号
9     cmd_set       1B      命令集
10    cmd_id        1B      命令 ID
11    data[]        变长    载荷
尾部  CRC16         2B      CRC-16/IBM-3740（CCITT-FALSE），低字节在前
```

帧头 11 字节，总帧长 = 11 + data_len + 2。

### Link v1 校验与互通约束

协议版本号保持 `PROTOCOL_VERSION=1`。`ver_len` 使用 10 位总帧长、4 位协议版本和
2 位保留位，按小端发送；总帧长最大 1023 字节，包含帧头和尾部 CRC16。发送端保留位置零，
接收端拒绝非零保留位、非 v1 版本和超出本机容量的长度。版本字段只标识线协议格式，
不用于表达固件版本；`pack_ver_len()` 对超出位宽的输入返回零，避免截断后生成另一种含义。

| 校验 | 覆盖范围 | poly | init | refin/refout | xorout | `"123456789"` 检查值 |
| --- | --- | --- | --- | --- | --- | --- |
| CRC-8/SMBUS | 偏移 0..2，即 SOF 和 ver_len | `0x07` | `0x00` | false/false | `0x00` | `0xF4` |
| CRC-16/IBM-3740（CCITT-FALSE） | 偏移 0 到尾部 CRC16 前，包含 head_crc、地址、序号、命令和全部载荷 | `0x1021` | `0xFFFF` | false/false | `0x0000` | `0x29B1` |

CRC16 低字节先发送，计算不包含尾部 CRC16 自身。两个 CRC 的查表均在编译期生成，运行时按字节
更新，不需要动态分配或运行期初始化。头部 CRC8 验证通过后才能采信长度，完整帧的 CRC16
通过后才解析和分发业务。外部字节流发生断开或明确的接收间断时，调用方应重置 `FrameCodec`；
纯编解码器不自行读取系统时间或推断传输层超时。

此 v1 线格式使用 CRC8 和标准 CRC16，与早期使用 BCC、历史 CRC16 表
（检查值 `0xA9B1`）的同号 v1 端点不兼容。App、Loader、上位机和其他对端必须配套更新；
版本号 1 本身不能区分两种校验规则。编解码器只接受这里定义的规则，不自动尝试旧校验，
不得在同一链路混用两种 v1 格式。地址、11 字节帧头、`cmd_type` 和 ACK 业务语义保持不变。

主机测试使用固定请求/ACK 字节向量和标准检查值，逐位参考算法核对全部双字节输入，
并覆盖头部及整帧的单/双 bit 误码、旧格式拒绝、版本/保留位、512/1023 字节容量和错误后恢复。

ACK 保持原有 v1 规则：响应帧的 `ack_mode=No`、`priority=Low`，阶段由
本地 pending 记录的原请求模式与响应 `status` 共同解释。对 `Progress` 请求，
`0x01` 表示已受理/处理中，其他合法状态表示最终结果。该约束允许新端与已有 v1
端点互通，不需要能力协商或占用保留位。

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

Router 只接受单播 sender；`0xFF` 不能作为 receiver。一个 Router 可通过
`set_local_addresses()` 终结多个本机逻辑地址，数组首项是本机主动发送时使用的主地址；
ACK 始终使用请求实际命中的本机地址作为 sender。Router 尚未配置有效本机单播地址时
禁止发送，也不会本地分发广播帧，避免启动/关闭窗口产生伪 ACK 或回环。
- `host_idx=0xF`：同类型组播

---

## 架构

```
  ┌─────────────────────────────────────────────────┐
  │  应用层                                          │
  │  配置本机地址和静态路由                            │
  │  通过 linker section 分布式注册 handler           │
  └────────────────────┬────────────────────────────┘
                       │
  ┌────────────────────▼────────────────────────────┐
  │  Router 层                                       │
  │  Parser task: 解码/认证/重放判断/ACK/静态路由      │
  │  High/Low 静态队列 + 公平突发限制                  │
  │  Worker task: 业务 handler/异步完成/诊断           │
  │  ACK pending + 超时重传 + per-link 统计            │
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

每个物理 Link 只允许一个有序分片流，Router 的 TX 锁保证本机不会交错发送两帧；承载层
也必须保证同一 Link 内顺序送达。收到新的 index=0 时，Reassembler 会丢弃未完成旧流并
保留这片作为新帧起点，避免一次丢片导致下一整帧也被连带丢弃；乱序、越界和 7-bit
索引回绕均 fail-closed 重置。

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
3. 创建周期 Parser 任务（默认 1kHz），只处理链路输入和协议快路径
4. 创建静态 Worker 任务，按优先级队列执行 handler，不阻塞其它物理链路

任务频率通过 Kconfig 配置：
```
CONFIG_LINK_PROCESS_HZ=1000    # 任务频率
CONFIG_LINK_PROCESS_STACK=2048 # 栈大小
CONFIG_LINK_PROCESS_PRIO=5     # 优先级
CONFIG_LINK_INIT_PRIORITY=95   # 生命周期顺序：设备之后、watchdog monitor 之前
CONFIG_LINK_PROCESS_RX_BUDGET=1024 # 每链路、每周期最大接收字节数
CONFIG_LINK_WORKER_STACK=4096 # handler worker 静态栈
CONFIG_LINK_WORKER_PRIO=4     # 默认低于 Parser
CONFIG_LINK_WORK_QUEUE_DEPTH=4 # 每个优先级的静态队列深度
CONFIG_LINK_HIGH_PRIORITY_BURST=4 # 连续高优先级处理上限
```

Parser 每周期仍对每条链路单独执行 RX budget。Worker 优先处理高优先级请求，但在两个
队列同时非空时，每处理 `LINK_HIGH_PRIORITY_BURST` 个高优先级请求就处理一个低优先级
请求，避免低优先级永久饥饿。所有队列、任务栈和控制块均为静态内存。

每个物理 `Link` 可在 Router 初始化前调用 `set_process_divider(n)`，把
`LINK_PROCESS_HZ` 按整数 `n` 分频。例如基础频率 1kHz、divider=2 时，该端口以 500Hz
调用 `poll()` 并排空接收 ring；divider=1 是默认的每周期处理。较大分频会降低唤醒和
空轮询开销，但产品必须保证驱动 RX ring 至少能容纳“最大字节率 × 处理间隔 × 2”的数据，
并额外容纳一个最大帧。当前接口不伪装 `divider=0` 事件模式，因为 `Link` 尚未定义
ISR-safe 的 Router 唤醒契约；传 0 会失败。

---

## 路由配置

### 路由模式

| 模式 | 匹配规则 | 用途 |
|------|---------|------|
| ByHost | `(receiver & mask) == (match_addr & mask)` | 按 host_id 路由 |
| Direct | 匹配来源 link_id | 透传/桥接模式 |

路由选择不依赖配置顺序：来源 link 精确匹配优先于地址规则，地址规则按 mask 中置位数
选择最长匹配，`Direct(0)` 仅作为最低优先级默认路由。相同优先级且匹配空间重叠的规则
会使 `set_routes()` 失败；Direct 路由也禁止转发回同一个输入 link。
运行时对所有模式都排除实际输入 link，因此默认 Direct 规则也不会把原始帧回送到来源端口。
协议帧当前不携带跨设备 TTL，产品静态拓扑必须无环，并在多节点 HIL 中验证断链、恢复和
错误路由不会形成设备间转发环。

### 流式 API

```cpp
auto &router = link::Router::instance();
router.set_self_addr(link::make_addr(0x1, 0)); // 编码结果为 0x10

static const link::RouteEntry routes[] = {
    // 同一匹配项的多个出口必须写在同一条规则中。
    link::make_route(
        link::route_by_host(0x10, 0xF0).to(1).to(3)),              // PC ↔ UART/BLE
    link::make_route(link::route_by_host(0x40, 0xF0).to(2)),       // 主控 ↔ CAN
    link::make_route(link::route_direct(0).to(1)),                  // 桥接：任意 → UART
};
router.set_routes(routes, 3);
```

多路由（同一 host_id 对应多个链路）自动 round-robin 选择。

---

## 本机地址与 handler 注册

```cpp
const uint8_t local_addresses[] = {
    link::make_addr(0x1U, 0U),
    link::make_addr(0x1U, 1U),
};
if (!router.set_local_addresses(local_addresses,
                                std::size(local_addresses))) {
    // 配置包含广播、保留地址、重复地址或数量超限
}
```

`set_self_addr()` 保留为单地址兼容接口。Handler 同时支持 receiver、cmd_set 和 cmd_id
掩码；Router 选择 mask 置位数最多的匹配项，并在初始化时拒绝同等特异度的重叠项，
从而使分发结果与链接顺序无关。

```cpp
void on_motor_cmd(const link::Frame &frame, void *arg) {
    // 处理电机命令
}

// 注册到 .link_handler section，SYS_INIT 时自动加载
LINK_HANDLER(0x01, 0x01, on_motor_cmd, nullptr);

// 仅处理 0x2x 本机地址、0x30 命令集和 0x4x 命令族。
LINK_HOST_HANDLER(0x20U, 0xF0U,
                  0x30U, 0xFFU,
                  0x40U, 0xF0U,
                  on_motor_cmd, nullptr);
```

同步 handler 在 Link Worker 任务中运行，`Frame::data` 只在回调期间有效。回调不能保存
该指针、阻塞等待 Link Parser，也不能调用 Router 生命周期接口。同步 handler 必须有界，
最坏执行时间应远小于 `LINK_REPLAY_RETENTION_MS`，并纳入 Worker 任务响应时间预算；需要
长时间运行的命令使用异步 handler：

```cpp
link::HandlerResult start_calibration(
    const link::Frame &frame,
    const link::CompletionToken &token,
    void *arg)
{
    // 将 token 按值复制到业务模块自己的静态队列。
    if (!enqueue_calibration(frame, token, arg)) {
        return link::HandlerResult::completed(0x10U);
    }
    return link::HandlerResult::deferred();
}

LINK_ASYNC_HANDLER(0x30U, 0x01U, start_calibration, nullptr, 5000U);

// 业务任务完成后调用一次；0xFA..0xFF 是 Link 保留状态。
(void)link::Router::instance().complete_request(token, 0x00U,
                                                 result, result_size);
```

异步 handler 只接受单播 `Finish` 或 `Progress` 请求。令牌在完成、超时、Router
deinit 后失效；重复完成返回 false。`Progress` 长任务可调用 `report_progress(token)`
刷新接收端等待窗口。进度报文固定为空数据，使响应有界且可重放；安全 Provider
仍必须为每次发送生成新的 nonce。
异步 handler 的超时必须严格小于 `LINK_REPLAY_RETENTION_MS`，初始化时会拒绝不满足
该条件的注册项，避免原任务尚未结束时相同 sequence 被当作新请求再次执行。
`LINK_SECURE_ASYNC_HANDLER`、`LINK_SECURE_HOST_HANDLER` 和
`LINK_SECURE_HOST_ASYNC_HANDLER` 可组合安全策略与掩码分发。

---

## ACK 与重传

| ack_mode | 阶段 | 重传策略 | 说明 |
|----------|------|---------|------|
| No | — | 不重传 | 发后不管 |
| Now | ACK / 非 `0x01` | 每 `LINK_ACK_NOW_TIMEOUT_MS` 重传，最多 `LINK_ACK_NOW_RETRIES` 次 | 业务队列写入成功后立即返回最终受理结果 |
| Finish | ACK / 非 `0x01` | 仅在前段送达窗口重传，之后等待调用方完成超时 | handler 完成后返回最终结果 |
| Progress | ACK / `0x01` → ACK / 最终状态 | 首个 `0x01` 后绝不重传原命令 | ACK 帧保持 v1 `ack_mode=No`，由状态码区分处理中和完成 |

`Finish` 和 `Progress` 发送必须设置 `SendRequest::finish_timeout_ms`，并严格小于
`LINK_REPLAY_RETENTION_MS`。`Progress` 在收到首个 ACK `status=0x01` 前只做有界送达
重传；收到后原业务命令已被对端受理，此后的 `status=0x01` 只刷新完成等待窗口，
完成超时也不会重发，
避免电机动作、Flash 写入等非幂等命令被执行两次。`Finish` 没有单独的受理阶段，
因此只在初始送达窗口内重传；接收端 replay cache 保证副本不重复进入 handler。
对于使用本节校验规则、但不会发送中间 `0x01` 的 v1 端点，Progress 在用完送达重试后仍会等待
`finish_timeout_ms` 到期，期间可直接接受对端返回的最终状态，但不再重发原命令。

ACK 回调优先绑定到单次发送，避免不同业务共享 sequence 状态：

```cpp
void on_command_ack(const link::AckEvent& event, void* context)
{
    if (event.kind == link::AckEventKind::Timeout) {
        // event.request_mode 是原请求的 v1 ack_mode。
        return;
    }
    // Progress 请求的 status=0x01 表示处理中，其他状态表示最终结果。
}

link::SendRequest request {};
request.receiver = 0x21U;
request.cmd_set = 0x30U;
request.cmd_id = 0x01U;
request.ack_mode = link::AckMode::Progress;
request.finish_timeout_ms = 5000U;
request.ack_callback = on_command_ack;
request.ack_arg = context;
(void)link::Router::instance().send(request);
```

`ack_callback` 和 `ack_arg` 会保存到 pending 表，必须至少存活到最终 ACK、超时或 Router
deinit，禁止把栈对象地址作为异步回调上下文。旧的 `set_on_timeout()` 签名继续保留；
需要区分 Now、Finish 和 Progress 原请求模式时使用 `set_on_timeout_ex()`。
`set_on_ack()`、`set_on_event()`、超时回调以及 handler 的上下文应在产品初始化阶段配置并保持静态生命周期；
运行中替换回调不会等待已经复制出去的旧回调执行完毕。

超时重发时 `cmd_type.retransmit` 位置 1。Router 使用固定容量 replay cache，以发送方、
接收方、sequence、命令、除 retransmit 外的命令属性和完整受保护 payload 精确识别
请求：完全相同的重传不会再次执行 handler，而是重放 ACK 状态及响应数据；相同 key 但任何
属性或 payload 冲突时，明文链路返回 `0xFD`，加密链路静默丢弃并计入安全统计，避免
在原响应上下文下生成不同密文而重复使用 AEAD nonce。缓存不使用短 CRC 代替完整比较，
避免碰撞后误判幂等。
ACK 必须携带至少一个状态字节，并与待确认请求的物理链路、本机地址、对端地址、
序列号、命令号和加密模式全部匹配；非 v1 规范的 ACK 优先级或 retransmit 位也会被拒绝。
不匹配的 ACK 不会清除 pending 项。16-bit 序列号回绕时也不会复用仍在等待
确认的序列号。

ACK 帧自身的 `ack_mode` 必须为 Link v1 约定的 `No`。Now 和 Finish 请求只接受
非 `0x01` 的终态；Progress 请求的 `0x01` 表示已受理或仍在处理，其他状态表示
最终结果。ACK 模式错误、来源、地址、
物理链路、sequence 或命令不匹配的 ACK 均不能清除 pending。应用可通过
`SendRequest::ack_callback` 接收本次发送的响应或超时，通过 `set_on_ack()` 注册全局观察
回调；其中数据指针仅在回调期间有效。ACK、event、
timeout 回调运行在 Parser 任务上下文，必须短小、非阻塞，且不能调用 Router
`init/deinit/process()`。

系统状态码如下；产品 handler 的最终状态只能使用 `0x00` 或 `0x02..0xF9`：

| 状态 | 含义 |
|------|------|
| `0x00` | 成功/已受理 |
| `0x01` | 处理中，仅用于 Progress ACK |
| `0xFA` | handler 返回值或请求模式非法 |
| `0xFB` | 异步 handler 超时 |
| `0xFC` | 工作队列、异步槽位或 replay 容量已满 |
| `0xFD` | sequence 重放冲突 |
| `0xFE` | 需要认证加密 |
| `0xFF` | handler 不存在 |

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
Provider，因此既不会重复执行业务，也不会因 AEAD nonce replay 把合法请求重传路径
误判为新命令。为保持 Link v1，所有 ACK 的 `ack_mode` 都是 `No`，Progress 的中间状态
和最终状态只能通过 status 区分；因此
Provider 每次 `protect()` 必须生成并携带新的唯一 nonce，不能只从 `SecurityContext`
确定性派生 nonce。nonce 应包含受保护的启动代次和单调计数器；接收端可先解析 nonce，
但只有验 tag 成功后才能提交 replay-window 状态并释放明文。重放 ACK 时 Router 会再次调用 `protect()`，同样必须分配
新的 nonce。重放缓存的静态 RAM 约为
`LINK_REPLAY_CACHE_SIZE * (LINK_MAX_FRAME_SIZE + LINK_REPLAY_RESPONSE_SIZE)`，产品必须
结合 map 文件配置条目数、帧长和最大返回数据长度。
缓存条目数还必须覆盖 retention 窗口内可能完成的唯一需应答请求数；安全 provider 自身
仍必须拒绝 retention 窗口以外的旧 nonce 重放。Router 不会驱逐窗口内的完成项或在途项；
缓存满时以 `0xFC` 拒绝新的需应答请求并产生 QueueOverflow 事件，以明确背压换取
非幂等命令不被重复执行。产品应按“窗口内峰值需应答请求数”配置条目并用诊断计数验证。

`Router::stats()` 原子快照还包含队列溢出、异步完成/超时、未处理请求和发送失败。
`Router::diagnostics()` 增加队列深度、异步占用和每条物理 link 的收发/错误计数。
Link 处理线程使用调用方持有对象、静态栈和静态内核控制块，不占 RTOS
heap；Router 使用的 mutex/semaphore 控制块同样内联在 OSAL 对象中。

### 远程诊断命令

启用 `LINK_DIAGNOSTICS` 后，`LINK_DIAGNOSTICS_COMMAND_SET`（默认 `0xFE`）的命令
`0x01` 返回 version 1 诊断快照。请求必须无 payload、单播，并使用 Finish 或 Progress；
启用 Link Security 时默认要求认证加密。返回 payload 使用小端格式：

```
u8 version, u8 flags, u8 returned_link_count, u8 total_link_count
u16 high_queue_depth, u16 low_queue_depth, u16 async_active, u16 reserved
15 * u32 aggregate_counters
returned_link_count * {
    u16 link_id, u8 ready, u8 process_divider,
    u32 rx_bytes, u32 rx_frames, u32 codec_errors,
    u32 tx_frames, u32 tx_failures
}
```

编码器只输出完整 link 记录，绝不截断记录；`returned_link_count < total_link_count` 表示
调用方配置的响应容量不足，剩余端口记录被省略。`set_on_event()` 可订阅 LinkUp、LinkDown、
QueueOverflow、UnhandledRequest 和 AsyncTimeout；事件回调遵守与 ACK 回调相同的上下文约束。

---

## Kconfig

```
menuconfig LINK
    bool "Link 通信协议栈"
    default n

if LINK
config LINK_MAX_FRAME_SIZE    # 最大帧长度，默认 512
config LINK_MAX_LINKS         # 最大链路数，默认 8
config LINK_MAX_ROUTES        # 最大路由规则数，默认 16
config LINK_MAX_PENDING       # 最大待确认帧数，默认 8
config LINK_ACK_NOW_TIMEOUT_MS # 送达阶段重传间隔，默认 50ms
config LINK_ACK_NOW_RETRIES    # 送达阶段最大重传次数，默认 2
config LINK_MAX_LOCAL_ADDRESSES # 本机逻辑地址上限，默认 4
config LINK_WORK_QUEUE_DEPTH  # 每优先级业务队列深度，默认 4
config LINK_HIGH_PRIORITY_BURST # 高优先级连续处理上限，默认 4
config LINK_MAX_ASYNC_REQUESTS # 异步完成槽位数，默认 4
config LINK_PROCESS_HZ        # 处理任务频率，默认 1000
config LINK_PROCESS_STACK     # 处理任务栈大小，默认 2048
config LINK_PROCESS_PRIO      # 处理任务优先级，默认 5
config LINK_WORKER_STACK      # handler worker 静态栈，默认 4096
config LINK_WORKER_PRIO       # handler worker 优先级，默认 4
config LINK_INIT_PRIORITY     # 生命周期 init 优先级，默认 95
config LINK_PROCESS_RX_BUDGET # 每链路/周期接收预算，默认 1024 字节
config LINK_UART_TX_TIMEOUT_MS # UART 单帧有界发送窗口，默认 100ms
config LINK_REPLAY_CACHE_SIZE  # 幂等请求缓存条目，默认 16
config LINK_REPLAY_RETENTION_MS # replay 保留窗口，默认 10s；必须大于异步处理超时
config LINK_REPLAY_RESPONSE_SIZE # replay 返回数据上限，默认 320
config LINK_SECURITY             # 启用产品 AEAD provider
config LINK_REQUIRE_SECURE_COMMANDS # 全局 fail-closed 安全策略
config LINK_DIAGNOSTICS          # 远程诊断，默认开启
config LINK_DIAGNOSTICS_COMMAND_SET # 远程诊断命令集，默认 0xFE
config LINK_DIAGNOSTICS_REQUIRE_SECURE # 安全启用时默认要求认证
endif
```

`LINK_UART_TX_TIMEOUT_MS` 必须按产品波特率和最大帧长校核。UART 后端是有界同步发送，
因此不会继续持有 Router 的 TX 缓冲；若产品需要在多链路大流量下消除队头阻塞，应在
具体 Link 后端增加独立静态 TX 队列与 worker，而不是让驱动保存传入指针。
`LINK_PROCESS_RX_BUDGET` 和 CAN 单次 poll 帧预算共同限制异常流量占用，未处理数据留到
下一周期，避免饿死其他链路、ACK 超时检查和健康监控。

以上设计吸收了 Lark LNP 中值得保留的分层报文处理、分阶段 ACK、按端口调度、掩码命令
分发、链路诊断和多地址思路，但按本项目的静态内存、确定性执行、固定产品拓扑和 Link v1
兼容约束重新实现，没有建立 Link v2。当前 Router 故意不实现动态 MAC/地址学习交换表。动态学习会
引入地址漂移、老化定时器、广播风暴和伪造源地址攻击面；只有未来出现运行时变化的多跳
网络且具备端口认证、环路抑制和老化测试后，才应作为独立可选组件加入。

同样不直接照搬以下机制：业务执行保持单 Worker，以避免多个 handler 并发带来的可重入
契约和每 Worker 独立栈开销；耗时任务通过 `CompletionToken` 转交产品已有任务异步完成。
当前静态路由每帧只选择一个可用出口，因此无需引入引用计数报文池、泛洪 TTL 和交换表。
UART/CAN/BLE 后端都向 Router 暴露同一种完整字节帧，CAN 分片属于后端职责，不在协议头
增加地址省略模式。Console/Shell 也不与业务协议复用同一 TX 通道，避免调试输出改变通信
实时性。这些边界只有在产品拓扑和硬件承载确实需要时再做独立、可关闭的组件。

`CONFIG_LINK` 默认关闭；使用 Link 的产品必须显式开启并根据物理端口数、并发窗口和
map 文件配置容量。关闭时 link 库不参与编译，零开销。`demo` 和 `demo_ble` 只使用
一个端口，因此把 links、routes、pending、queue 和 worker stack 收紧为产品值；replay
保留 16 项以覆盖 10 秒去重窗口内的管理命令峰值，避免框架上限无条件变成固件 RAM 成本。

---

## 文件结构

```
component/link/
├── Kconfig
├── CMakeLists.txt
├── include/link/
│   ├── frame.h        # 帧结构体、地址编码、CRC8/CRC16 契约
│   ├── codec.h        # FrameCodec 状态机 + 打包
│   ├── link.h         # Link 基类 + LinkRegistry
│   ├── fragment.h     # Reassembler + Fragmenter（分片重组）
│   ├── diagnostics.h  # versioned 远程诊断 wire encoder
│   ├── router.h       # Router + 静态路由/队列/异步完成
│   ├── uart_link.h    # UartLink 子类
│   ├── can_link.h     # CanLink 子类（含分片）
│   └── ble_link.h     # BleLink 子类（分片 + 静态完整帧队列）
└── src/
    ├── codec.cc       # FrameCodec 实现
    ├── crc8.cc        # 标准 CRC-8/SMBUS
    ├── crc16.cc       # 标准 CRC-16/IBM-3740（CCITT-FALSE）
    ├── diagnostics.cc # 诊断快照编码
    └── router.cc      # Router 实现 + SYS_INIT
```
