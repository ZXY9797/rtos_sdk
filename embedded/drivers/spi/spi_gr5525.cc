#include <drivers/spi.h>
#include <arch/arch_interface.h>
#include <system_gr55xx.h>

namespace hal {
namespace {

/*
 * GR5525 SPI — DesignWare SSI compatible (spi_regs_t in gr5525_a0.h)
 *
 * Register offsets match the actual hardware layout.
 * DATA register is at offset 0x60, not immediately after STAT.
 */
struct Gr5525SpiRegs {
    volatile uint32_t CTRL0;           /* 0x00 */
    volatile uint32_t CTRL1;           /* 0x04 */
    volatile uint32_t SSI_EN;          /* 0x08 */
    volatile uint32_t MW_CTRL;         /* 0x0C */
    volatile uint32_t S_EN;            /* 0x10 */
    volatile uint32_t BAUD;            /* 0x14 */
    volatile uint32_t TX_FIFO_TL;      /* 0x18 */
    volatile uint32_t RX_FIFO_TL;      /* 0x1C */
    volatile uint32_t TX_FIFO_LEVEL;   /* 0x20 */
    volatile uint32_t RX_FIFO_LEVEL;   /* 0x24 */
    volatile uint32_t STAT;            /* 0x28 */
    volatile uint32_t INT_MASK;        /* 0x2C */
    volatile uint32_t INT_STAT;        /* 0x30 */
    volatile uint32_t RAW_INT_STAT;    /* 0x34 */
    volatile uint32_t TX_FIFO_OIC;     /* 0x38 */
    volatile uint32_t RX_FIFO_OIC;     /* 0x3C */
    volatile uint32_t RX_FIFO_UIC;     /* 0x40 */
    volatile uint32_t MULTI_M_IC;      /* 0x44 */
    volatile uint32_t INT_CLR;         /* 0x48 */
    volatile uint32_t DMA_CTRL;        /* 0x4C */
    volatile uint32_t DMA_TX_DL;       /* 0x50 */
    volatile uint32_t DMA_RX_DL;       /* 0x54 */
    volatile uint32_t RESERVED0[2];    /* 0x58 */
    volatile uint32_t DATA;            /* 0x60 */
};

/* CTRL0 bits — DW SSI */
constexpr uint32_t CTRL0_FRF_SPI     = (0U << 4);   /* Frame format: SPI */
constexpr uint32_t CTRL0_SCPHA       = (1U << 6);   /* Serial Clock Phase */
constexpr uint32_t CTRL0_SCPOL       = (1U << 7);   /* Serial Clock Polarity */
constexpr uint32_t CTRL0_XFE_MODE    = (0U << 8);   /* TX and RX (normal) */
constexpr uint32_t CTRL0_DFS_8BIT    = (7U << 16);  /* Data Frame Size = 8 (value = bits-1) */
constexpr uint32_t CTRL0_DFS_16BIT   = (15U << 16);

/* SSI_EN bits */
constexpr uint32_t SSI_EN            = (1U << 0);

/* S_EN bits */
constexpr uint32_t S_EN_SLAVE0       = (1U << 0);

/* STAT bits */
constexpr uint32_t STAT_BUSY         = (1U << 0);   /* SSI Busy */
constexpr uint32_t STAT_TFNF         = (1U << 1);   /* TX FIFO Not Full */
constexpr uint32_t STAT_TFE          = (1U << 2);   /* TX FIFO Empty */
constexpr uint32_t STAT_RFNE         = (1U << 3);   /* RX FIFO Not Empty */

} // anonymous namespace

Status SpiBase::init(const SpiConfig &config) {
    auto *regs = reinterpret_cast<Gr5525SpiRegs *>(m_base);

    /* Disable SSI before configuration */
    regs->SSI_EN = 0;

    /* CTRL0: SPI format, clock phase/polarity, data frame size */
    uint32_t ctl0 = CTRL0_FRF_SPI | CTRL0_XFE_MODE;

    if (config.mode == SpiMode::Mode1 || config.mode == SpiMode::Mode3)
        ctl0 |= CTRL0_SCPHA;
    if (config.mode == SpiMode::Mode2 || config.mode == SpiMode::Mode3)
        ctl0 |= CTRL0_SCPOL;

    if (config.data_bits == 16)
        ctl0 |= CTRL0_DFS_16BIT;
    else
        ctl0 |= CTRL0_DFS_8BIT;

    regs->CTRL0 = ctl0;

    /* Baud rate divider: sclk = SPI_CLK / (2 * divider) */
    if (config.clock_hz == 0) return Status::InvalidArgument;
    uint32_t divider = SystemCoreClock / (2U * config.clock_hz);
    if (divider < 1) divider = 1;
    if (divider > 0xFFFF) divider = 0xFFFF;
    regs->BAUD = divider;

    /* Enable slave 0 select (master mode uses S_EN to drive CS) */
    regs->S_EN = S_EN_SLAVE0;

    /* Enable SSI */
    regs->SSI_EN = SSI_EN;

    m_initialized = true;
    return Status::Ok;
}

Status SpiBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<Gr5525SpiRegs *>(m_base);
    regs->SSI_EN = 0;
    m_initialized = false;
    return Status::Ok;
}

Status SpiBase::sync_send(const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms) {
    if (!m_initialized || len == 0) return Status::InvalidArgument;

    auto *regs = reinterpret_cast<Gr5525SpiRegs *>(m_base);
    uint32_t timeout_loops = timeout_ms * (SystemCoreClock / 1000U / 4U);
    if (timeout_loops == 0) timeout_loops = 1;

    for (size_t i = 0; i < len; i++) {
        uint8_t tx_byte = tx ? tx[i] : 0xFF;

        /* Wait for TX FIFO not full */
        uint32_t remaining = timeout_loops;
        while (!(regs->STAT & STAT_TFNF)) {
            if (--remaining == 0) return Status::Timeout;
        }
        regs->DATA = tx_byte;

        /* Wait for RX data available */
        remaining = timeout_loops;
        while (!(regs->STAT & STAT_RFNE)) {
            if (--remaining == 0) return Status::Timeout;
        }
        uint8_t rx_byte = static_cast<uint8_t>(regs->DATA);

        if (rx) rx[i] = rx_byte;
    }

    return Status::Ok;
}

} // namespace hal
