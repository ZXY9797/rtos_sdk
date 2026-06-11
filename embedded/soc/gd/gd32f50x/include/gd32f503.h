#ifndef GD32F503_H
#define GD32F503_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "gd32f50x_libopt.h"

/* Interrupt Number Definition */
typedef enum {
    /* Cortex-M4 processor exceptions numbers */
    NonMaskableInt_IRQn     = -14,
    HardFault_IRQn          = -13,
    MemoryManagement_IRQn   = -12,
    BusFault_IRQn           = -11,
    UsageFault_IRQn         = -10,
    SVCall_IRQn             = -5,
    DebugMonitor_IRQn       = -4,
    PendSV_IRQn             = -2,
    SysTick_IRQn            = -1,

    /* GD32F50x specific interrupt numbers */
    WWDGT_IRQn              = 0,
    LVD_IRQn                = 1,
    TAMPER_IRQn             = 2,
    RTC_IRQn                = 3,
    FMC_IRQn                = 4,
    RCU_IRQn                = 5,
    EXTI0_IRQn              = 6,
    EXTI1_IRQn              = 7,
    EXTI2_IRQn              = 8,
    EXTI3_IRQn              = 9,
    EXTI4_IRQn              = 10,
    DMA0_Channel0_IRQn      = 11,
    DMA0_Channel1_IRQn      = 12,
    DMA0_Channel2_IRQn      = 13,
    DMA0_Channel3_IRQn      = 14,
    DMA0_Channel4_IRQn      = 15,
    DMA0_Channel5_IRQn      = 16,
    DMA0_Channel6_IRQn      = 17,
    ADC0_1_IRQn             = 18,
    CAN0_TX_IRQn            = 19,
    CAN0_RX0_IRQn           = 20,
    CAN0_RX1_IRQn           = 21,
    CAN0_EWMC_IRQn          = 22,
    EXTI5_9_IRQn            = 23,
    TIMER0_BRK_TIMER8_IRQn  = 24,
    TIMER0_UP_TIMER9_IRQn   = 25,
    TIMER0_TRG_CMT_TIMER10_IRQn = 26,
    TIMER0_Channel_IRQn     = 27,
    TIMER1_IRQn             = 28,
    TIMER2_IRQn             = 29,
    TIMER3_IRQn             = 30,
    I2C0_EV_IRQn            = 31,
    I2C0_ER_IRQn            = 32,
    I2C1_EV_IRQn            = 33,
    I2C1_ER_IRQn            = 34,
    SPI0_IRQn               = 35,
    SPI1_IRQn               = 36,
    USART0_IRQn             = 37,
    USART1_IRQn             = 38,
    USART2_IRQn             = 39,
    EXTI10_15_IRQn          = 40,
    RTC_Alarm_IRQn          = 41,
    USBFS_WKUP_IRQn         = 42,
    TIMER4_IRQn             = 45,
    SPI2_IRQn               = 51,
    UART3_IRQn              = 52,
    UART4_IRQn              = 53,
    TIMER5_IRQn             = 54,
    TIMER6_IRQn             = 55,
    DMA1_Channel0_IRQn      = 56,
    DMA1_Channel1_IRQn      = 57,
    DMA1_Channel2_IRQn      = 58,
    DMA1_Channel3_IRQn      = 59,
    DMA1_Channel4_IRQn      = 60,
    ENET_IRQn               = 61,
    ENET_WKUP_IRQn          = 62,
    CAN1_TX_IRQn            = 63,
    CAN1_RX0_IRQn           = 64,
    CAN1_RX1_IRQn           = 65,
    CAN1_EWMC_IRQn          = 66,
    USBFS_IRQn              = 67,
    DMA1_Channel5_IRQn      = 68,
    DMA1_Channel6_IRQn      = 69,
    USART5_IRQn             = 70,
    I2C2_EV_IRQn            = 71,
    I2C2_ER_IRQn            = 72,
    USBHS_EP1_Out_IRQn      = 73,
    USBHS_EP1_In_IRQn       = 74,
    USBHS_WKUP_IRQn         = 75,
    USBHS_IRQn              = 76,
    DCI_IRQn                = 77,
    CAU_IRQn                = 78,
    HAU_TRNG_IRQn           = 79,
    FPU_IRQn                = 80,
    UART6_IRQn              = 81,
    UART7_IRQn              = 82,
    SPI3_IRQn               = 83,
    SPI4_IRQn               = 84,
    SPI5_IRQn               = 85,
    TLI_IRQn                = 86,
    TLI_ER_IRQn             = 87,
    IPA_IRQn                = 88,
} IRQn_Type;

/* ================================================================================================================= */
/*                  Peripheral memory map                                                                            */
/* ================================================================================================================= */
/* Peripheral and SRAM base address */
#define PERIPH_BASE                     ((uint32_t)0x40000000U)
#define SRAM_BASE                       ((uint32_t)0x20000000U)

/* Peripheral bus base address */
#define APB1_BUS_BASE                   (PERIPH_BASE + 0x00000000U)
#define APB2_BUS_BASE                   (PERIPH_BASE + 0x00010000U)
#define AHB1_BUS_BASE                   (PERIPH_BASE + 0x00020000U)
#define AHB2_BUS_BASE                   (PERIPH_BASE + 0x08000000U)
#define AHB3_BUS_BASE                   (PERIPH_BASE + 0x10000000U)

/* APB1 peripherals base address */
#define TIMER_BASE                      (APB1_BUS_BASE + 0x00000000U)
#define TIMER1_BASE                     (TIMER_BASE + 0x00000000U)
#define TIMER2_BASE                     (TIMER_BASE + 0x00000400U)
#define TIMER3_BASE                     (TIMER_BASE + 0x00000800U)
#define TIMER4_BASE                     (TIMER_BASE + 0x00000C00U)
#define TIMER5_BASE                     (APB1_BUS_BASE + 0x00001000U)
#define TIMER6_BASE                     (APB1_BUS_BASE + 0x00001400U)
#define TIMER11_BASE                    (APB1_BUS_BASE + 0x00001800U)
#define TIMER12_BASE                    (APB1_BUS_BASE + 0x00001C00U)
#define TIMER13_BASE                    (APB1_BUS_BASE + 0x00002000U)

#define RTC_BASE                        (APB1_BUS_BASE + 0x00002800U)
#define WWDGT_BASE                      (APB1_BUS_BASE + 0x00002C00U)
#define FWDGT_BASE                      (APB1_BUS_BASE + 0x00003000U)
#define SPI1_BASE                       (APB1_BUS_BASE + 0x00003800U)
#define SPI2_BASE                       (APB1_BUS_BASE + 0x00003C00U)
#define USART_BASE                      (APB1_BUS_BASE + 0x00004400U)
#define USART1_BASE                     (APB1_BUS_BASE + 0x00004400U)
#define USART2_BASE                     (APB1_BUS_BASE + 0x00004800U)
#define UART3_BASE                      (APB1_BUS_BASE + 0x00004C00U)
#define UART4_BASE                      (APB1_BUS_BASE + 0x00005000U)
#define I2C0_BASE                       (APB1_BUS_BASE + 0x00005400U)
#define I2C1_BASE                       (APB1_BUS_BASE + 0x00005800U)
#define CAN0_BASE                       (APB1_BUS_BASE + 0x00006400U)
#define CAN1_BASE                       (APB1_BUS_BASE + 0x00006800U)
#define PMU_BASE                        (APB1_BUS_BASE + 0x00007000U)
#define DAC_BASE                        (APB1_BUS_BASE + 0x00007400U)
#define UART6_BASE                      (APB1_BUS_BASE + 0x00007800U)
#define UART7_BASE                      (APB1_BUS_BASE + 0x00007C00U)

/* APB2 peripherals base address */
#define TIMER0_BASE                     (APB2_BUS_BASE + 0x00000000U)
#define TIMER7_BASE                     (APB2_BUS_BASE + 0x00000400U)
#define TIMER8_BASE                     (APB2_BUS_BASE + 0x00004000U)
#define TIMER9_BASE                     (APB2_BUS_BASE + 0x00004400U)
#define TIMER10_BASE                    (APB2_BUS_BASE + 0x00004800U)

#define USART0_BASE                     (APB2_BUS_BASE + 0x00003800U)
#define USART5_BASE                     (APB2_BUS_BASE + 0x00005000U)

#define ADC0_BASE                       (APB2_BUS_BASE + 0x00002000U)
#define ADC1_BASE                       (APB2_BUS_BASE + 0x00002400U)

#define SPI0_BASE                       (APB2_BUS_BASE + 0x00003000U)
#define SPI3_BASE                       (APB2_BUS_BASE + 0x00003C00U)
#define SPI4_BASE                       (APB2_BUS_BASE + 0x00004C00U)
#define SPI5_BASE                       (APB2_BUS_BASE + 0x00005400U)

#define SYSCFG_BASE                     (APB2_BUS_BASE + 0x00000000U)
#define AFIO_BASE                       (APB2_BUS_BASE + 0x00000000U)
#define EXTI_BASE                       (APB2_BUS_BASE + 0x00000400U)
#define SDIO_BASE                       (APB2_BUS_BASE + 0x00002C00U)

/* AHB1 peripherals base address */
#define DMA0_BASE                       (AHB1_BUS_BASE + 0x00000000U)
#define DMA0_Channel0_BASE              (DMA0_BASE + 0x00000008U)
#define DMA0_Channel1_BASE              (DMA0_BASE + 0x0000001CU)
#define DMA0_Channel2_BASE              (DMA0_BASE + 0x00000030U)
#define DMA0_Channel3_BASE              (DMA0_BASE + 0x00000044U)
#define DMA0_Channel4_BASE              (DMA0_BASE + 0x00000058U)
#define DMA0_Channel5_BASE              (DMA0_BASE + 0x0000006CU)
#define DMA0_Channel6_BASE              (DMA0_BASE + 0x00000080U)

#define DMA1_BASE                       (AHB1_BUS_BASE + 0x00000400U)
#define DMA1_Channel0_BASE              (DMA1_BASE + 0x00000008U)
#define DMA1_Channel1_BASE              (DMA1_BASE + 0x0000001CU)
#define DMA1_Channel2_BASE              (DMA1_BASE + 0x00000030U)
#define DMA1_Channel3_BASE              (DMA1_BASE + 0x00000044U)
#define DMA1_Channel4_BASE              (DMA1_BASE + 0x00000058U)
#define DMA1_Channel5_BASE              (DMA1_BASE + 0x0000006CU)
#define DMA1_Channel6_BASE              (DMA1_BASE + 0x00000080U)

#define DMAMUX_BASE                    (AHB1_BUS_BASE + 0x00000800U)

#define RCU_BASE                        (AHB1_BUS_BASE + 0x00001000U)
#define FMC_BASE                        (AHB1_BUS_BASE + 0x00002000U)
#define FMC_OB_BASE                     (FMC_BASE + 0x00000040U)
#define CRC_BASE                        (AHB1_BUS_BASE + 0x00003000U)
#define ENET_BASE                       (AHB1_BUS_BASE + 0x00008000U)
#define ENET_MAC_BASE                   (ENET_BASE)
#define ENET_MMC_BASE                   (ENET_BASE + 0x00000100U)
#define ENET_PTP_BASE                   (ENET_BASE + 0x00000700U)
#define ENET_DMA_BASE                   (ENET_BASE + 0x00001000U)

#define GPIO_BASE                       (AHB1_BUS_BASE + 0x00000000U + 0x00020000U)
#define GPIOA_BASE                      (GPIO_BASE + 0x00000000U)
#define GPIOB_BASE                      (GPIO_BASE + 0x00000400U)
#define GPIOC_BASE                      (GPIO_BASE + 0x00000800U)
#define GPIOD_BASE                      (GPIO_BASE + 0x00000C00U)
#define GPIOE_BASE                      (GPIO_BASE + 0x00001000U)
#define GPIOF_BASE                      (GPIO_BASE + 0x00001400U)
#define GPIOG_BASE                      (GPIO_BASE + 0x00001800U)
#define GPIOH_BASE                      (GPIO_BASE + 0x00001C00U)
#define GPIOI_BASE                      (GPIO_BASE + 0x00002000U)

/* AHB2 peripherals base address */
#define USBFS_BASE                      (AHB2_BUS_BASE + 0x00000000U)
#define USBHS_BASE                      (AHB2_BUS_BASE + 0x00020000U)
#define TLI_BASE                        (AHB2_BUS_BASE + 0x00001000U)
#define DCI_BASE                        (AHB2_BUS_BASE + 0x00002000U)
#define CAU_BASE                        (AHB2_BUS_BASE + 0x00004000U)
#define HAU_BASE                        (AHB2_BUS_BASE + 0x00005000U)
#define TRNG_BASE                       (AHB2_BUS_BASE + 0x00006000U)
#define IPA_BASE                        (AHB2_BUS_BASE + 0x00008000U)

/* Core debug peripheral base address */
#define DBG_BASE                        ((uint32_t)0xE0042000U)

/* NVIC priority bits */
#define __NVIC_PRIO_BITS                4U

/* DSP extension - Cortex-M33 has DSP */
#define __DSP_PRESENT                   1U

/* FPU configuration */
#define __FPU_PRESENT                   1U
#define __FPU_USED                      1U

/* Cache configuration - GD32F503 has no cache */
#define __ICACHE_PRESENT                0U
#define __DCACHE_PRESENT                0U

/* TrustZone / SAU configuration - GD32F503 has no TrustZone */
#define __SAUREGION_PRESENT             0U

/* Include CMSIS core files */
#include "core_cm33.h"

#ifdef __cplusplus
}
#endif

#endif /* GD32F503_H */
