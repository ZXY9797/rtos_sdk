#include "device.h"
#include <drivers/gpio.h>
#include <errno.h>

// __pinned_func
static inline int gpio_port_get_raw(const struct device * port, gpio_port_value_t * value)
{
    return DEVICE_API_GET(gpio, port)->port_get_raw(port, value);
}

static inline int gpio_pin_set_raw(const struct device *port, gpio_pin_t pin,
				   int value)
{
	__unused const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;
	int ret;
    // TODO: 实现ASSERT
	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
	// 	 "Unsupported pin");
	if (value != 0)	{
		ret = DEVICE_API_GET(gpio, port)->port_set_bits_raw(port, (gpio_port_pins_t)BIT(pin));
	} else {
		ret = DEVICE_API_GET(gpio, port)->port_clear_bits_raw(port, (gpio_port_pins_t)BIT(pin));
	}

	return ret;
}

/**
 * @brief Get logical level of all input pins in a port.
 *
 * Get logical level of an input pin taking into account GPIO_ACTIVE_LOW flag.
 * If pin is configured as Active High, a low physical level will be interpreted
 * as logical value 0. If pin is configured as Active Low, a low physical level
 * will be interpreted as logical value 1.
 *
 * Value of a pin with index n will be represented by bit n in the returned
 * port value.
 *
 * @param port Pointer to the device structure for the driver instance.
 * @param value Pointer to a variable where pin values will be stored.
 *
 * @retval 0 If successful.
 * @retval -EIO I/O error when accessing an external GPIO chip.
 * @retval -EWOULDBLOCK if operation would block.
 */
static inline int gpio_port_get(const struct device *port,
				gpio_port_value_t *value)
{
	const struct gpio_driver_data *const data =
			(const struct gpio_driver_data *)port->data;
	int ret;

	ret = gpio_port_get_raw(port, value);
	if (ret == 0) {
		*value ^= data->invert;
	}

	return ret;
}


/**
 * @brief Get direction of select pins in a port.
 *
 * Retrieve direction of each pin specified in @p map.
 *
 * If @p inputs or @p outputs is NULL, then this function does not get the
 * respective input or output direction information.
 *
 * @param port Pointer to the device structure for the driver instance.
 * @param map Bitmap of pin directions to query.
 * @param inputs Pointer to a variable where input directions will be stored.
 * @param outputs Pointer to a variable where output directions will be stored.
 *
 * @retval 0 If successful.
 * @retval -ENOSYS if the underlying driver does not support this call.
 * @retval -EIO I/O error when accessing an external GPIO chip.
 * @retval -EWOULDBLOCK if operation would block.
 */
static inline int gpio_port_get_direction(const struct device *port, gpio_port_pins_t map,
				      gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
#ifdef CONFIG_GPIO_GET_DIRECTION
	const struct gpio_driver_api *api = (const struct gpio_driver_api *)port->api;
	int ret;

	// SYS_PORT_TRACING_FUNC_ENTER(gpio_port, get_direction, port, map, inputs, outputs);

	if (api->port_get_direction == NULL) {
		SYS_PORT_TRACING_FUNC_EXIT(gpio_port, get_direction, port, -ENOSYS);
		return -ENOSYS;
	}

	ret = api->port_get_direction(port, map, inputs, outputs);
	// SYS_PORT_TRACING_FUNC_EXIT(gpio_port, get_direction, port, ret);
	return ret;
#else
    return -ENOTSUP;
#endif
}


int gpio_pin_interrupt_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;
	__unused const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;
	const struct gpio_driver_data *const data =
		(const struct gpio_driver_data *)port->data;
	enum gpio_int_trig trig;
	enum gpio_int_mode mode;
	int ret;

	// SYS_PORT_TRACING_FUNC_ENTER(gpio_pin, interrupt_configure, port, pin, flags);

	if (api->pin_interrupt_configure == NULL) {
		// SYS_PORT_TRACING_FUNC_EXIT(gpio_pin, interrupt_configure, port, pin, -ENOSYS);
		return -ENOSYS;
	}

	// __ASSERT((flags & (GPIO_INT_DISABLE | GPIO_INT_ENABLE))
	// 	 != (GPIO_INT_DISABLE | GPIO_INT_ENABLE),
	// 	 "Cannot both enable and disable interrupts");

	// __ASSERT((flags & (GPIO_INT_DISABLE | GPIO_INT_ENABLE)) != 0U,
	// 	 "Must either enable or disable interrupts");

	// __ASSERT(((flags & GPIO_INT_ENABLE) == 0) ||
	// 	 ((flags & GPIO_INT_EDGE) != 0) ||
	// 	 ((flags & (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)) !=
	// 	  (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)),
	// 	 "Only one of GPIO_INT_LOW_0, GPIO_INT_HIGH_1 can be "
	// 	 "enabled for a level interrupt.");

#ifdef CONFIG_GPIO_ENABLE_DISABLE_INTERRUPT
#define GPIO_INT_ENABLE_DISABLE_ONLY_VALUE  GPIO_INT_ENABLE_DISABLE_ONLY
#else
#define GPIO_INT_ENABLE_DISABLE_ONLY_VALUE  0
#endif /* CONFIG_GPIO_ENABLE_DISABLE_INTERRUPT */

	// __ASSERT(((flags & GPIO_INT_ENABLE) == 0) ||
	// 		 ((flags & (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)) != 0) ||
	// 		 (flags & GPIO_INT_ENABLE_DISABLE_ONLY_VALUE) != 0,
	// 	 "At least one of GPIO_INT_LOW_0, GPIO_INT_HIGH_1 has to be enabled.");
#undef GPIO_INT_ENABLE_DISABLE_ONLY_VALUE
	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
	// 	 "Unsupported pin");

	if (((flags & GPIO_INT_LEVELS_LOGICAL) != 0) &&
	    ((data->invert & (gpio_port_pins_t)BIT(pin)) != 0)) {
		/* Invert signal bits */
		flags ^= (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1);
	}

	trig = (enum gpio_int_trig)(flags & (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1 | GPIO_INT_WAKEUP));
#ifdef CONFIG_GPIO_ENABLE_DISABLE_INTERRUPT
	mode = (enum gpio_int_mode)(flags & (GPIO_INT_EDGE | GPIO_INT_DISABLE | GPIO_INT_ENABLE |
					     GPIO_INT_ENABLE_DISABLE_ONLY));
#else
	mode = (enum gpio_int_mode)(flags & (GPIO_INT_EDGE | GPIO_INT_DISABLE | GPIO_INT_ENABLE));
#endif /* CONFIG_GPIO_ENABLE_DISABLE_INTERRUPT */

	ret = api->pin_interrupt_configure(port, pin, mode, trig);
	// SYS_PORT_TRACING_FUNC_EXIT(gpio_pin, interrupt_configure, port, pin, ret);
	return ret;
}

int gpio_pin_is_input(const struct device *port, gpio_pin_t pin)
{
	int rv;
	gpio_port_pins_t pins;
	__unused const struct gpio_driver_config *cfg =
		(const struct gpio_driver_config *)port->config;

	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U, "Unsupported pin");

	rv = gpio_port_get_direction(port, BIT(pin), &pins, NULL);
	if (rv < 0) {
		return rv;
	}

	return (int)!!((gpio_port_pins_t)BIT(pin) & pins);
}

int gpio_pin_is_output(const struct device *port, gpio_pin_t pin)
{
	int rv;
	gpio_port_pins_t pins;
	__unused const struct gpio_driver_config *cfg =
		(const struct gpio_driver_config *)port->config;

	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U, "Unsupported pin");

	rv = gpio_port_get_direction(port, BIT(pin), NULL, &pins);
	if (rv < 0) {
		return rv;
	}

	return (int)!!((gpio_port_pins_t)BIT(pin) & pins);
}

int gpio_pin_get_config(const struct device *port, gpio_pin_t pin,
				  gpio_flags_t *flags)
{
#ifdef CONFIG_GPIO_GET_CONFIG
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;
	int ret;

	// SYS_PORT_TRACING_FUNC_ENTER(gpio_pin, get_config, port, pin, *flags);

	if (api->pin_get_config == NULL) {
		// SYS_PORT_TRACING_FUNC_EXIT(gpio_pin, get_config, port, pin, -ENOSYS);
		return -ENOSYS;
	}

	ret = api->pin_get_config(port, pin, flags);
	// SYS_PORT_TRACING_FUNC_EXIT(gpio_pin, get_config, port, pin, ret);
	return ret;
#else
    return -ENOTSUP;
#endif
}

int gpio_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;
	__unused const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;
	struct gpio_driver_data *data =
		(struct gpio_driver_data *)port->data;
	int ret;
    // TODO: 实现ASSERT
	// SYS_PORT_TRACING_FUNC_ENTER(gpio_pin, configure, port, pin, flags);

	// __ASSERT((flags & GPIO_INT_MASK) == 0,
	// 	 "Interrupt flags are not supported");

	// __ASSERT((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) !=
	// 	 (GPIO_PULL_UP | GPIO_PULL_DOWN),
	// 	 "Pull Up and Pull Down should not be enabled simultaneously");

	// __ASSERT(!((flags & GPIO_INPUT) && !(flags & GPIO_OUTPUT) && (flags & GPIO_SINGLE_ENDED)),
	// 	 "Input cannot be enabled for 'Open Drain', 'Open Source' modes without Output");

	// __ASSERT_NO_MSG((flags & GPIO_SINGLE_ENDED) != 0 ||
	// 		(flags & GPIO_LINE_OPEN_DRAIN) == 0);

	// __ASSERT((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) == 0
	// 	 || (flags & GPIO_OUTPUT) != 0,
	// 	 "Output needs to be enabled to be initialized low or high");

	// __ASSERT((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH))
	// 	 != (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH),
	// 	 "Output cannot be initialized low and high");

	if (((flags & GPIO_OUTPUT_INIT_LOGICAL) != 0)
	    && ((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) != 0)
	    && ((flags & GPIO_ACTIVE_LOW) != 0)) {
		flags ^= GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH;
	}

	flags &= ~GPIO_OUTPUT_INIT_LOGICAL;

	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
	// 	 "Unsupported pin");

	if ((flags & GPIO_ACTIVE_LOW) != 0) {
		data->invert |= (gpio_port_pins_t)BIT(pin);
	} else {
		data->invert &= ~(gpio_port_pins_t)BIT(pin);
	}

	ret = api->pin_configure(port, pin, flags);
	// SYS_PORT_TRACING_FUNC_EXIT(gpio_pin, configure, port, pin, ret);
	return ret;
}

int gpio_pin_get(const struct device *port, gpio_pin_t pin)
{
	__unused const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;
	gpio_port_value_t value;
	int ret;
    // TODO: 实现ASSERT
	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
	// 	 "Unsupported pin");

	ret = gpio_port_get(port, &value);
	if (ret == 0) {
		ret = (value & (gpio_port_pins_t)BIT(pin)) != 0 ? 1 : 0;
	}

	return ret;
}

int gpio_pin_set(const struct device *port, gpio_pin_t pin, int value)
{
	__unused const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;
	const struct gpio_driver_data *const data =
			(const struct gpio_driver_data *)port->data;
    // TODO: 实现ASSERT
	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
	// 	 "Unsupported pin");

	if (data->invert & (gpio_port_pins_t)BIT(pin)) {
		value = (value != 0) ? 0 : 1;
	}

	return gpio_pin_set_raw(port, pin, value);
}

int gpio_pin_toggle(const struct device *port, gpio_pin_t pin)
{
	__unused const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;

    // TODO: 实现ASSERT
	// __ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
	// 	 "Unsupported pin");

	return DEVICE_API_GET(gpio, port)->port_toggle_bits(port, (gpio_port_pins_t)BIT(pin));
}
