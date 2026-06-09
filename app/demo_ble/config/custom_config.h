#ifndef __CUSTOM_CONFIG_H__
#define __CUSTOM_CONFIG_H__

/* Goodix GR5525 SDK custom configuration for GR5525RGNI */

/* Chip selection - GR5525 uses SOC_GR5X25 */
#define SOC_GR5X25

/* System configuration */
#define SYSTEM_CLOCK                    (96000000)

/* Platform configuration for GR5525RGNI */
#define CHIP_VER                        (0x5525)
#define APP_CODE_LOAD_ADDR              (0x01000000)
#define APP_CODE_RUN_ADDR               (0x01000000)
#define BOOT_CHECK_IMAGE                (1)
#define BOOT_LONG_TIME                  (0)
#define SECURITY_CFG_VAL                (0x0)

/* Peripheral drivers */
#define APP_DRIVER_ENABLED
#define APP_UART_ENABLED
#define APP_SPI_ENABLED
#define APP_I2C_ENABLED
#define APP_DMA_ENABLED
#define APP_GPIOTE_ENABLED
#define APP_PWM_ENABLED
#define APP_ADC_ENABLED
#define APP_RTC_ENABLED
#define APP_TIM_ENABLED
#define APP_DUAL_TIM_ENABLED
#define APP_QSPI_ENABLED
#define APP_DSPI_ENABLED
#define APP_I2S_ENABLED
#define APP_PDM_ENABLED
#define APP_RNG_ENABLED
#define APP_WDT_ENABLED
#define APP_BOD_ENABLED
#define APP_COMP_ENABLED
#define APP_PWR_MGMT_ENABLED

/* BLE configuration */
#define BLE_ENABLED
#define BLE_MAX_CONNECTIONS             (1)
#define CFG_MAX_CONNECTIONS             (1)
#define CFG_MAX_ADVS                    (1)
#define CFG_MAX_SCAN                    (0)
#define CFG_CONNLESS_AOA_AOD_SUPPORT    (0)
#define CFG_MAX_BOND_DEVS               (1)
#define CFG_MAX_PRFS                    (8)
#define CFG_CONTROLLER_ONLY             (0)
#define CFG_MAX_DATA_LEN                (251)

/* Debug configuration */
#define DEBUG_INFO_ENABLE               (1)

#endif /* __CUSTOM_CONFIG_H__ */
