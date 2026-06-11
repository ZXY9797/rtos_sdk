#include <boot/sha256.h>

namespace boot {

static constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static constexpr uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static constexpr uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static constexpr uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static constexpr uint32_t ep0(uint32_t x) {
    return rotr(x, 2U) ^ rotr(x, 13U) ^ rotr(x, 22U);
}

static constexpr uint32_t ep1(uint32_t x) {
    return rotr(x, 6U) ^ rotr(x, 11U) ^ rotr(x, 25U);
}

static constexpr uint32_t sig0(uint32_t x) {
    return rotr(x, 7U) ^ rotr(x, 18U) ^ (x >> 3U);
}

static constexpr uint32_t sig1(uint32_t x) {
    return rotr(x, 17U) ^ rotr(x, 19U) ^ (x >> 10U);
}

static void sha256_transform(Sha256Ctx &ctx) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t(ctx.buffer[i * 4]) << 24) |
               (uint32_t(ctx.buffer[i * 4 + 1]) << 16) |
               (uint32_t(ctx.buffer[i * 4 + 2]) << 8) |
               uint32_t(ctx.buffer[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = sig1(w[i - 2]) + w[i - 7] + sig0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = ctx.state[0], b = ctx.state[1];
    uint32_t c = ctx.state[2], d = ctx.state[3];
    uint32_t e = ctx.state[4], f = ctx.state[5];
    uint32_t g = ctx.state[6], h = ctx.state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + ep1(e) + ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = ep0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx.state[0] += a; ctx.state[1] += b;
    ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f;
    ctx.state[6] += g; ctx.state[7] += h;
}

void sha256_init(Sha256Ctx &ctx) {
    ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
    ctx.count = 0;
}

void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len) {
    size_t idx = ctx.count % 64;
    ctx.count += len;

    for (size_t i = 0; i < len; i++) {
        ctx.buffer[idx++] = data[i];
        if (idx == 64) {
            sha256_transform(ctx);
            idx = 0;
        }
    }
}

void sha256_final(Sha256Ctx &ctx, uint8_t hash[32]) {
    size_t idx = ctx.count % 64;
    uint64_t bits = ctx.count * 8;

    ctx.buffer[idx++] = 0x80;
    if (idx > 56) {
        while (idx < 64) ctx.buffer[idx++] = 0;
        sha256_transform(ctx);
        idx = 0;
    }
    while (idx < 56) ctx.buffer[idx++] = 0;

    for (int i = 7; i >= 0; i--) {
        ctx.buffer[idx++] = uint8_t(bits >> (i * 8));
    }

    sha256_transform(ctx);

    for (int i = 0; i < 8; i++) {
        hash[i * 4] = uint8_t(ctx.state[i] >> 24);
        hash[i * 4 + 1] = uint8_t(ctx.state[i] >> 16);
        hash[i * 4 + 2] = uint8_t(ctx.state[i] >> 8);
        hash[i * 4 + 3] = uint8_t(ctx.state[i]);
    }
}

void sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
    Sha256Ctx ctx;
    sha256_init(ctx);
    sha256_update(ctx, data, len);
    sha256_final(ctx, hash);
}

} // 命名空间 boot
