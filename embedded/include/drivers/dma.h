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

class DmaChannel {
public:
    constexpr DmaChannel(uint32_t dma_base, uint8_t channel, uint8_t mux_channel)
        : m_dma(dma_base), m_ch(channel), m_mux_ch(mux_channel) {}

    Status config(const DmaConfig &cfg);
    Status start();
    Status stop();
    bool is_transfer_complete() const;
    void clear_flags();

private:
    uint32_t m_dma;
    uint8_t m_ch;
    uint8_t m_mux_ch;
};

} /* namespace hal */
