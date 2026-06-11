#pragma once

#include <cstdint>
#include <cstddef>

namespace boot {

// SHA-256 上下文 (支持硬件 HAU 或软件实现)
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
};

void sha256_init(Sha256Ctx &ctx);
void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256Ctx &ctx, uint8_t hash[32]);

// 一次性计算
void sha256(const uint8_t *data, size_t len, uint8_t hash[32]);

} // namespace boot
