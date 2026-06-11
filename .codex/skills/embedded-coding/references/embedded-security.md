# 嵌入式安全编码规范

嵌入式系统面临独特的安全挑战：
资源受限、难以更新、物理可访问。
安全必须从设计阶段开始考虑。

---

## 1. 安全设计原则

### 1.1 最小权限原则

```c
/* 每个模块只获取完成任务所需的最小权限 */

/* 错误 — 全局可访问 */
extern volatile SystemConfig_t g_system_config;

/* 正确 — 通过接口访问，有权限控制 */
int system_config_get(ConfigKey key, void *value, size_t size);
int system_config_set(ConfigKey key, const void *value, size_t size);

/* 权限检查 */
int system_config_set(ConfigKey key, const void *value, size_t size)
{
    /* 检查调用者权限 */
    if (!has_permission(PERMISSION_CONFIG_WRITE)) {
        return ERR_PERMISSION_DENIED;
    }

    /* 检查参数有效性 */
    if (!is_valid_config(key, value, size)) {
        return ERR_INVALID_PARAM;
    }

    /* 执行配置 */
    return config_store(key, value, size);
}
```

### 1.2 纵深防御

```c
/* 多层安全检查，不依赖单一防护 */

int process_external_command(const uint8_t *data, size_t len)
{
    /* 第1层：长度检查 */
    if (len < MIN_CMD_LEN || len > MAX_CMD_LEN) {
        return ERR_INVALID_LENGTH;
    }

    /* 第2层：格式验证 */
    if (!validate_command_format(data, len)) {
        return ERR_INVALID_FORMAT;
    }

    /* 第3层：CRC 校验 */
    if (!verify_command_crc(data, len)) {
        return ERR_CRC;
    }

    /* 第4层：权限检查 */
    if (!check_command_permission(data)) {
        return ERR_PERMISSION_DENIED;
    }

    /* 第5层：业务逻辑验证 */
    if (!validate_command_business_logic(data)) {
        return ERR_INVALID_COMMAND;
    }

    return execute_command(data);
}
```

---

## 2. 输入验证

### 2.1 所有外部输入必须验证

```c
/* 用户输入验证 */
int process_user_input(const char *input, size_t len)
{
    /* 长度检查 */
    if (len == 0 || len > MAX_INPUT_LEN) {
        return ERR_INVALID_LENGTH;
    }

    /* 字符集检查 */
    for (size_t i = 0; i < len; i++) {
        if (!is_valid_char(input[i])) {
            return ERR_INVALID_CHAR;
        }
    }

    /* 空终止检查 */
    if (input[len] != '\0') {
        return ERR_NOT_NULL_TERMINATED;
    }

    /* 业务规则检查 */
    if (!meets_business_rules(input)) {
        return ERR_INVALID_FORMAT;
    }

    return process_input(input);
}
```

### 2.2 整数溢出检查

```c
/* 安全的整数运算 */
bool safe_add_uint32(uint32_t a, uint32_t b, uint32_t *result)
{
    if (a > UINT32_MAX - b) {
        return false;  /* 溢出 */
    }
    *result = a + b;
    return true;
}

bool safe_multiply_size(size_t a, size_t b, size_t *result)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return false;  /* 溢出 */
    }
    *result = a * b;
    return true;
}

/* 使用示例 */
int allocate_buffer(size_t element_size, size_t count, void **buf)
{
    size_t total_size;
    if (!safe_multiply_size(element_size, count, &total_size)) {
        return ERR_OVERFLOW;
    }

    *buf = malloc(total_size);
    return *buf ? 0 : ERR_NO_MEMORY;
}
```

### 2.3 指针验证

```c
/* 所有指针参数必须验证 */
int process_data(const uint8_t *data, size_t len, uint8_t *output)
{
    /* 空指针检查 */
    if (data == NULL || output == NULL) {
        return ERR_NULL_POINTER;
    }

    /* 对齐检查（如果需要） */
    if (((uintptr_t)data & 0x03) != 0) {
        return ERR_ALIGNMENT;
    }

    /* 范围检查 */
    if (len > MAX_DATA_LEN) {
        return ERR_INVALID_LENGTH;
    }

    /* 安全处理 */
    memcpy(output, data, len);
    return 0;
}
```

---

## 3. 内存安全

### 3.1 缓冲区溢出防护

```c
/* 安全的字符串操作 */
int safe_string_copy(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || src == NULL || dest_size == 0) {
        return ERR_INVALID_PARAM;
    }

    size_t src_len = strnlen(src, dest_size);
    if (src_len >= dest_size) {
        dest[0] = '\0';
        return ERR_BUFFER_OVERFLOW;
    }

    memcpy(dest, src, src_len);
    dest[src_len] = '\0';
    return 0;
}

/* 安全的内存拷贝 */
int safe_memory_copy(void *dest, size_t dest_size,
                     const void *src, size_t src_size)
{
    if (dest == NULL || src == NULL) {
        return ERR_NULL_POINTER;
    }

    if (src_size > dest_size) {
        return ERR_BUFFER_OVERFLOW;
    }

    memcpy(dest, src, src_size);
    return 0;
}
```

### 3.2 栈溢出防护

```c
/* 栈保护标记 */
#define STACK_CANARY_VALUE  0xDEADBEEF

typedef struct {
    uint32_t canary;
    /* 实际数据 */
    uint8_t data[256];
} ProtectedBuffer_t;

void init_protected_buffer(ProtectedBuffer_t *buf)
{
    buf->canary = STACK_CANARY_VALUE;
    memset(buf->data, 0, sizeof(buf->data));
}

bool check_stack_canary(const ProtectedBuffer_t *buf)
{
    return buf->canary == STACK_CANARY_VALUE;
}

/* 使用示例 */
void process_with_protection(void)
{
    ProtectedBuffer_t buffer;
    init_protected_buffer(&buffer);

    /* 使用 buffer.data */

    if (!check_stack_canary(&buffer)) {
        /* 栈溢出检测 */
        handle_stack_overflow();
    }
}
```

### 3.3 敏感数据清理

```c
/* 安全清理敏感数据 */
void secure_zero(void *ptr, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (size--) {
        *p++ = 0;
    }
    /* 编译器屏障，防止优化掉 */
    __asm__ volatile("" ::: "memory");
}

/* 使用示例 */
int process_secret(const uint8_t *key, size_t key_len)
{
    uint8_t local_key[32];
    if (key_len > sizeof(local_key)) {
        return ERR_INVALID_LENGTH;
    }

    memcpy(local_key, key, key_len);

    /* 使用密钥 */
    int result = encrypt_with_key(local_key, key_len);

    /* 安全清理 */
    secure_zero(local_key, sizeof(local_key));

    return result;
}
```

---

## 4. 密码学实践

### 4.1 密钥管理

```c
/* 密钥存储在安全区域 */
#define KEY_SLOT_ENCRYPTION  0
#define KEY_SLOT_AUTH        1

/* 从安全存储加载密钥 */
int load_key_from_secure_storage(uint8_t slot, uint8_t *key, size_t key_size)
{
    /* 检查安全状态 */
    if (!is_secure_boot()) {
        return ERR_SECURITY_VIOLATION;
    }

    /* 从 OTP/安全区域读取 */
    return otp_read_key(slot, key, key_size);
}

/* 禁止硬编码密钥 */
/* 错误 */
const uint8_t hardcoded_key[] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
};

/* 正确 — 运行时从安全存储加载 */
uint8_t runtime_key[16];
int ret = load_key_from_secure_storage(KEY_SLOT_ENCRYPTION,
                                       runtime_key,
                                       sizeof(runtime_key));
```

### 4.2 随机数生成

```c
/* 使用硬件随机数生成器 */
int generate_random(uint8_t *buf, size_t len)
{
    /* 检查 RNG 状态 */
    if (!rng_is_ready()) {
        return ERR_NOT_READY;
    }

    /* 从硬件 RNG 读取 */
    for (size_t i = 0; i < len; i += 4) {
        uint32_t random = rng_get_random();
        size_t copy_len = (len - i) < 4 ? (len - i) : 4;
        memcpy(buf + i, &random, copy_len);
    }

    return 0;
}

/* 禁止使用伪随机数生成器处理安全相关数据 */
/* 错误 — 使用 rand() */
int generate_token_bad(void) {
    return rand();  /* 可预测，不安全 */
}
```

### 4.3 加密算法选择

```c
/* 推荐算法 */
#define CIPHER_ALGO    AES_256_GCM    /* 认证加密 */
#define HASH_ALGO      SHA256         /* 哈希 */
#define SIGN_ALGO      ECDSA_P256     /* 数字签名 */

/* 禁止使用的算法 */
/* - DES, 3DES（密钥太短）
 * - MD5, SHA1（碰撞攻击）
 * - RC4（流密码弱点）
 * - ECB 模式（模式泄露） */

/* AES-GCM 加密示例 */
int aes_gcm_encrypt(const uint8_t *key, size_t key_len,
                    const uint8_t *iv, size_t iv_len,
                    const uint8_t *plaintext, size_t pt_len,
                    const uint8_t *aad, size_t aad_len,
                    uint8_t *ciphertext, uint8_t *tag)
{
    /* 验证参数 */
    if (key_len != 32 || iv_len != 12) {
        return ERR_INVALID_PARAM;
    }

    /* 执行 AES-256-GCM 加密 */
    return crypto_aes_gcm_encrypt(key, iv, plaintext, pt_len,
                                  aad, aad_len, ciphertext, tag);
}
```

---

## 5. 通信安全

### 5.1 消息认证

```c
/* 使用 HMAC 验证消息完整性 */
typedef struct __packed {
    uint8_t  header[4];
    uint16_t length;
    uint8_t  payload[MAX_PAYLOAD];
    uint8_t  hmac[32];  /* HMAC-SHA256 */
} AuthenticatedMessage_t;

int verify_message(const AuthenticatedMessage_t *msg,
                   const uint8_t *key, size_t key_len)
{
    /* 计算 HMAC */
    uint8_t computed_hmac[32];
    hmac_sha256(key, key_len,
                msg->header, 4 + 2 + msg->length,
                computed_hmac);

    /* 常量时间比较（防止时序攻击） */
    if (secure_memcmp(msg->hmac, computed_hmac, 32) != 0) {
        return ERR_AUTH_FAILED;
    }

    return 0;
}

/* 常量时间比较 */
int secure_memcmp(const void *a, const void *b, size_t len)
{
    const uint8_t *p1 = a;
    const uint8_t *p2 = b;
    uint8_t result = 0;

    for (size_t i = 0; i < len; i++) {
        result |= p1[i] ^ p2[i];
    }

    return result;
}
```

### 5.2 重放攻击防护

```c
/* 使用序列号和时间戳防止重放 */
typedef struct __packed {
    uint32_t sequence;    /* 单调递增序列号 */
    uint32_t timestamp;   /* 时间戳 */
    uint8_t  payload[];
} ReplayProtected_t;

static uint32_t g_last_sequence = 0;
static uint32_t g_last_timestamp = 0;

int check_replay_protection(const ReplayProtected_t *msg)
{
    /* 检查序列号单调递增 */
    if (msg->sequence <= g_last_sequence) {
        return ERR_REPLAY;
    }

    /* 检查时间戳在合理范围内 */
    uint32_t now = get_current_timestamp();
    if (msg->timestamp < now - MAX_TIME_DRIFT ||
        msg->timestamp > now + MAX_TIME_DRIFT) {
        return ERR_TIMESTAMP;
    }

    /* 更新状态 */
    g_last_sequence = msg->sequence;
    g_last_timestamp = msg->timestamp;

    return 0;
}
```

---

## 6. 安全启动

### 6.1 固件签名验证

```c
/* 安全启动流程 */
int secure_boot_verify(const FirmwareHeader_t *header)
{
    /* 1. 验证固件头格式 */
    if (header->magic != FIRMWARE_MAGIC) {
        return ERR_INVALID_MAGIC;
    }

    /* 2. 验证版本号（防降级） */
    if (header->version < get_minimum_version()) {
        return ERR_VERSION_TOO_OLD;
    }

    /* 3. 计算固件哈希 */
    uint8_t hash[32];
    sha256(header->data, header->size, hash);

    /* 4. 验证签名 */
    uint8_t public_key[64];
    load_root_public_key(public_key);

    if (!ecdsa_verify(public_key, hash, 32,
                      header->signature, header->sig_len)) {
        return ERR_SIGNATURE_INVALID;
    }

    return 0;  /* 验证通过 */
}
```

### 6.2 安全更新

```c
/* 安全固件更新流程 */
int secure_firmware_update(const uint8_t *new_fw, size_t fw_size)
{
    /* 1. 验证新固件签名 */
    FirmwareHeader_t *header = (FirmwareHeader_t *)new_fw;
    int ret = secure_boot_verify(header);
    if (ret != 0) {
        return ret;
    }

    /* 2. 写入备份区域 */
    ret = flash_write(BACKUP_START_ADDR, new_fw, fw_size);
    if (ret != 0) {
        return ret;
    }

    /* 3. 验证写入正确性 */
    if (memcmp((void *)BACKUP_START_ADDR, new_fw, fw_size) != 0) {
        return ERR_FLASH_VERIFY;
    }

    /* 4. 设置更新标志 */
    set_update_pending_flag();

    /* 5. 重启应用新固件 */
    NVIC_SystemReset();

    return 0;  /* 不会执行到这里 */
}
```

---

## 7. 安全审计

### 7.1 安全日志

```c
/* 记录安全相关事件 */
typedef enum {
    SECURITY_EVENT_BOOT,
    SECURITY_EVENT_AUTH_SUCCESS,
    SECURITY_EVENT_AUTH_FAILURE,
    SECURITY_EVENT_KEY_ACCESS,
    SECURITY_EVENT_CONFIG_CHANGE,
    SECURITY_EVENT_FIRMWARE_UPDATE,
    SECURITY_EVENT_TAMPER_DETECTED,
} SecurityEventType_t;

void log_security_event(SecurityEventType_t type, const void *data,
                        size_t data_len)
{
    SecurityLogEntry_t entry = {
        .timestamp = get_timestamp(),
        .type = type,
        .sequence = g_security_log_sequence++,
    };

    if (data != NULL && data_len <= sizeof(entry.data)) {
        memcpy(entry.data, data, data_len);
    }

    /* 计算完整性校验 */
    entry.crc = calculate_crc(&entry, sizeof(entry) - sizeof(entry.crc));

    /* 写入安全日志存储 */
    security_log_write(&entry);

    /* 如果是高风险事件，触发警报 */
    if (is_high_risk_event(type)) {
        trigger_security_alert(type);
    }
}
```

### 7.2 安全计数器

```c
/* 防止暴力破解的计数器 */
typedef struct {
    uint32_t failed_attempts;
    uint32_t lockout_until;
    uint32_t total_failures;
} BruteForceProtection_t;

static BruteForceProtection_t g_auth_protection;

bool check_brute_force_protection(void)
{
    uint32_t now = get_tick();

    /* 检查是否在锁定期间 */
    if (g_auth_protection.lockout_until > now) {
        return false;  /* 仍在锁定 */
    }

    /* 检查失败次数 */
    if (g_auth_protection.failed_attempts >= MAX_FAILED_ATTEMPTS) {
        /* 触发锁定 */
        g_auth_protection.lockout_until = now + LOCKOUT_DURATION;
        log_security_event(SECURITY_EVENT_AUTH_FAILURE, NULL, 0);
        return false;
    }

    return true;
}

void record_auth_failure(void)
{
    g_auth_protection.failed_attempts++;
    g_auth_protection.total_failures++;
}

void reset_auth_protection(void)
{
    g_auth_protection.failed_attempts = 0;
    g_auth_protection.lockout_until = 0;
}
```

---

## 嵌入式安全检查清单

- [ ] 所有外部输入经过验证
- [ ] 整数运算检查溢出
- [ ] 指针使用前验证非空
- [ ] 缓冲区操作检查边界
- [ ] 敏感数据使用后安全清理
- [ ] 禁止硬编码密钥和密码
- [ ] 使用硬件随机数生成器
- [ ] 通信使用认证加密
- [ ] 实现重放攻击防护
- [ ] 固件更新验证签名
- [ ] 安全事件有审计日志
- [ ] 暴力破解有防护机制
