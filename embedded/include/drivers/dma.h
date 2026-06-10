#pragma once

#include <cstdint>
#include <cstddef>
#include <drivers/status.h>

namespace hal {

enum class DmaDirection : uint8_t {
    PeriphToMemory = 0,
    MemoryToPeriph = 1,
    MemoryToMemory = 2,
};

enum class DmaWidth : uint8_t {
    Byte     = 0,
    HalfWord = 1,
    Word     = 2,
};

enum class DmaPriority : uint8_t {
    Low      = 0,
    Medium   = 1,
    High     = 2,
    VeryHigh = 3,
};

struct DmaConfig {
    uint32_t request_id;        /* DMAMUX request ID */
    uint32_t periph_addr;       /* Peripheral data register address */
    uint32_t memory_addr;       /* Memory buffer address */
    uint16_t count;             /* Number of data items to transfer */
    DmaDirection direction;
    DmaWidth periph_width;
    DmaWidth memory_width;
    DmaPriority priority;
    bool periph_inc;
    bool memory_inc;
    bool circular;
};

/// DMA 通道资源跟踪（用于 RAII 管理）
class DmaChannelPool {
public:
    /// 请求一个可用的 DMA 通道（标记为已占用）
    [[nodiscard]] static int8_t allocate(uint32_t dma_base);

    /// 释放一个 DMA 通道（标记为空闲）
    static void release(uint32_t dma_base, int8_t channel);
};

/**
 * @brief DMA 通道 — RAII 管理
 *
 * 析构时自动停止传输并释放通道。
 * 支持移动语义，禁止拷贝。
 *
 * 用法:
 *   auto ch = DmaChannel::request(0x40020000);
 *   if (ch.is_valid()) {
 *       ch.config(cfg);
 *       ch.start();
 *       // ... 传输完成后 ch 析构时自动释放
 *   }
 */
class DmaChannel {
public:
    /// 请求一个可用的 DMA 通道
    [[nodiscard]] static DmaChannel request(uint32_t dma_base, uint8_t mux_channel);

    /// 使用指定通道（不自动分配）
    constexpr DmaChannel(uint32_t dma_base, uint8_t channel, uint8_t mux_channel)
        : m_dma(dma_base), m_ch(channel), m_mux_ch(mux_channel) {}

    /// 析构时自动停止并释放通道
    ~DmaChannel();

    /// 移动构造（转移所有权）
    DmaChannel(DmaChannel &&other) noexcept;

    /// 移动赋值（转移所有权）
    DmaChannel &operator=(DmaChannel &&other) noexcept;

    /// 禁止拷贝
    DmaChannel(const DmaChannel &) = delete;
    DmaChannel &operator=(const DmaChannel &) = delete;

    [[nodiscard]] Status config(const DmaConfig &cfg);
    [[nodiscard]] Status start();
    [[nodiscard]] Status stop();
    [[nodiscard]] bool is_transfer_complete() const;
    void clear_flags();

    /// 检查通道是否有效
    [[nodiscard]] bool is_valid() const { return m_ch != 0xFF; }

    /// 获取通道号
    [[nodiscard]] uint8_t channel() const { return m_ch; }

private:
    uint32_t m_dma {0};
    uint8_t m_ch {0xFF};    // 0xFF 表示无效
    uint8_t m_mux_ch {0};
};

} /* namespace hal */
