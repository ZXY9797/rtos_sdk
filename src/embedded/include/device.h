#ifndef MDK_INCLUDE_DEVICE_H_
#define MDK_INCLUDE_DEVICE_H_

#include <init.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Device flags */
typedef uint8_t device_flags_t;

/** Device operations */
struct device_ops {
	/** Initialization function */
	int (*init)(const struct device *dev);
#ifdef CONFIG_DEVICE_DEINIT_SUPPORT
	/** De-initialization function */
	int (*deinit)(const struct device *dev);
#endif /* CONFIG_DEVICE_DEINIT_SUPPORT */
};

/**
 * @brief Runtime device structure (in ROM) per driver instance
 */
struct device {
	/** Name of the device instance */
	const char *name;
	/** Address of device instance config information */
	const void *config;
	/** Address of the API structure exposed by the device instance */
	const void *api;
	/** Address of the common device state */
	struct device_state *state;
	/** Address of the device instance private data */
	void *data;
	/** Device operations */
	struct device_ops ops;
	/** Device flags */
	device_flags_t flags;
#if defined(CONFIG_DEVICE_DEPS) || defined(__DOXYGEN__)
	/**
	 * Optional pointer to dependencies associated with the device.
	 *
	 * This encodes a sequence of sets of device handles that have some
	 * relationship to this node. The individual sets are extracted with
	 * dedicated API, such as device_required_handles_get(). Only available
	 * if @kconfig{CONFIG_DEVICE_DEPS} is enabled.
	 */
	Z_DEVICE_DEPS_CONST device_handle_t *deps;
#endif /* CONFIG_DEVICE_DEPS */
#if defined(CONFIG_PM_DEVICE) || defined(__DOXYGEN__)
	/**
	 * Reference to the device PM resources (only available if
	 * @kconfig{CONFIG_PM_DEVICE} is enabled).
	 */
	union {
		struct pm_device_base *pm_base;
		struct pm_device *pm;
		struct pm_device_isr *pm_isr;
	};
#endif
#if defined(CONFIG_DEVICE_DT_METADATA) || defined(__DOXYGEN__)
	const struct device_dt_metadata *dt_meta;
#endif /* CONFIG_DEVICE_DT_METADATA */
};
typedef struct devcie device_t;

#ifdef __cplusplus
}
#endif

#endif /* MDK_INCLUDE_DEVICE_H_ */
