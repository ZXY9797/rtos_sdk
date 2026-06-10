/**
 * custom_config.h — Kconfig → GR5525 SDK 宏映射
 *
 * 将 Kconfig 生成的 CONFIG_* 宏映射为 GR5525 SDK 期望的宏名
 * （CFG_*、SOC_GR5X25 等）。所有 GR5525 项目共用此文件，
 * 项目间差异通过 prj.conf 中的 Kconfig 选项控制。
 *
 * autoconf.h 已通过 -imacros 全局包含，CONFIG_* 宏始终可用。
 */

#ifndef __CUSTOM_CONFIG_H__
#define __CUSTOM_CONFIG_H__

/* ── 芯片选择 ── */
#ifndef SOC_GR5X25
#define SOC_GR5X25
#endif

/* ── 芯片版本 ── */
#ifndef CHIP_VER
#define CHIP_VER                        (0x5525)
#endif

/* ── Flash 地址（GR5525RGNI 固定值）── */
#ifndef APP_CODE_LOAD_ADDR
#define APP_CODE_LOAD_ADDR              (0x01000000)
#endif

#ifndef APP_CODE_RUN_ADDR
#define APP_CODE_RUN_ADDR               (0x01000000)
#endif

/* ── Boot 参数（来自 Kconfig）── */
#ifdef CONFIG_GR5525_BOOT_CHECK_IMAGE
    #if CONFIG_GR5525_BOOT_CHECK_IMAGE
        #define BOOT_CHECK_IMAGE        (1)
    #else
        #define BOOT_CHECK_IMAGE        (0)
    #endif
#else
    #define BOOT_CHECK_IMAGE            (1)
#endif

#ifdef CONFIG_GR5525_BOOT_LONG_TIME
    #if CONFIG_GR5525_BOOT_LONG_TIME
        #define BOOT_LONG_TIME          (1)
    #else
        #define BOOT_LONG_TIME          (0)
    #endif
#else
    #define BOOT_LONG_TIME              (0)
#endif

#ifdef CONFIG_GR5525_SECURITY_CFG
    #define SECURITY_CFG_VAL            (CONFIG_GR5525_SECURITY_CFG)
#else
    #define SECURITY_CFG_VAL            (0x0)
#endif

/* ── BLE 协议栈参数（来自 Kconfig）── */
#ifdef CONFIG_BLE_GR5525

    #ifndef CFG_MAX_CONNECTIONS
        #ifdef CONFIG_BLE_MAX_CONNECTIONS
            #define CFG_MAX_CONNECTIONS     (CONFIG_BLE_MAX_CONNECTIONS)
        #else
            #define CFG_MAX_CONNECTIONS     (1)
        #endif
    #endif

    #ifndef CFG_MAX_ADVS
        #ifdef CONFIG_BLE_MAX_ADVS
            #define CFG_MAX_ADVS            (CONFIG_BLE_MAX_ADVS)
        #else
            #define CFG_MAX_ADVS            (1)
        #endif
    #endif

    #ifndef CFG_MAX_SCAN
        #ifdef CONFIG_BLE_MAX_SCAN
            #define CFG_MAX_SCAN            (CONFIG_BLE_MAX_SCAN)
        #else
            #define CFG_MAX_SCAN            (0)
        #endif
    #endif

    #ifndef CFG_CONNLESS_AOA_AOD_SUPPORT
        #ifdef CONFIG_BLE_CONNLESS_AOA_AOD
            #define CFG_CONNLESS_AOA_AOD_SUPPORT (1)
        #else
            #define CFG_CONNLESS_AOA_AOD_SUPPORT (0)
        #endif
    #endif

    #ifndef CFG_MAX_BOND_DEVS
        #ifdef CONFIG_BLE_MAX_BOND_DEVS
            #define CFG_MAX_BOND_DEVS       (CONFIG_BLE_MAX_BOND_DEVS)
        #else
            #define CFG_MAX_BOND_DEVS       (1)
        #endif
    #endif

    #ifndef CFG_MAX_PRFS
        #ifdef CONFIG_BLE_MAX_PRFS
            #define CFG_MAX_PRFS            (CONFIG_BLE_MAX_PRFS)
        #else
            #define CFG_MAX_PRFS            (8)
        #endif
    #endif

    #ifndef CFG_CONTROLLER_ONLY
        #ifdef CONFIG_BLE_CONTROLLER_ONLY
            #if CONFIG_BLE_CONTROLLER_ONLY
                #define CFG_CONTROLLER_ONLY (1)
            #else
                #define CFG_CONTROLLER_ONLY (0)
            #endif
        #else
            #define CFG_CONTROLLER_ONLY     (0)
        #endif
    #endif

#endif /* CONFIG_BLE_GR5525 */

#endif /* __CUSTOM_CONFIG_H__ */
