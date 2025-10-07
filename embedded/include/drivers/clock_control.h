#ifndef MSDK_DRIVERS_CLOCK_CONTROL_H_
#define MSDK_DRIVERS_CLOCK_CONTROL_H_

#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * clock_control_subsys_t is a type to identify a clock controller sub-system.
 * Such data pointed is opaque and relevant only to the clock controller
 * driver instance being used.
 */
typedef void *clock_control_subsys_t;

/**
 * clock_control_subsys_rate_t is a type to identify a clock
 * controller sub-system rate.  Such data pointed is opaque and
 * relevant only to set the clock controller rate of the driver
 * instance being used.
 */
typedef void *clock_control_subsys_rate_t;

/** @brief Callback called on clock started.
 *
 * @param dev		Device structure whose driver controls the clock.
 * @param subsys	Opaque data representing the clock.
 * @param user_data	User data.
 */
typedef void (*clock_control_cb_t)(const struct device *dev,
				   clock_control_subsys_t subsys,
				   void *user_data);

typedef int (*clock_control)(const struct device *dev,
			     clock_control_subsys_t sys);

typedef int (*clock_control_get)(const struct device *dev,
				 clock_control_subsys_t sys,
				 uint32_t *rate);

typedef int (*clock_control_async_on_fn)(const struct device *dev,
					 clock_control_subsys_t sys,
					 clock_control_cb_t cb,
					 void *user_data);

typedef enum clock_control_status (*clock_control_get_status_fn)(
						    const struct device *dev,
						    clock_control_subsys_t sys);

typedef int (*clock_control_set)(const struct device *dev,
				 clock_control_subsys_t sys,
				 clock_control_subsys_rate_t rate);

typedef int (*clock_control_configure_fn)(const struct device *dev,
					  clock_control_subsys_t sys,
					  void *data);

struct clock_control_driver_api {
	clock_control			on;
	clock_control			off;
	clock_control_async_on_fn	async_on;
	clock_control_get		get_rate;
	clock_control_get_status_fn	get_status;
	clock_control_set		set_rate;
	clock_control_configure_fn	configure;
};

int clock_control_on(const struct device *dev, clock_control_subsys_t sys);
int clock_control_off(const struct device *dev, clock_control_subsys_t sys);
int clock_control_get_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *rate);

#ifdef __cplusplus
}
#endif

#endif
