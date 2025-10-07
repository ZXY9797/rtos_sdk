#include <drivers/clock_control.h>
#include <errno.h>

int clock_control_on(const struct device *dev, clock_control_subsys_t sys)
{
    if (DEVICE_API_GET(clock_control, dev)->on != NULL) {
        return DEVICE_API_GET(clock_control, dev)->on(dev, sys);
    }
    return -ENOTSUP;
}

int clock_control_off(const struct device *dev, clock_control_subsys_t sys)
{
    if (DEVICE_API_GET(clock_control, dev)->off != NULL) {
        return DEVICE_API_GET(clock_control, dev)->off(dev, sys);
    }
    return -ENOTSUP;
}

int clock_control_get_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *rate)
{
    if (DEVICE_API_GET(clock_control, dev)->get_rate != NULL) {
        return DEVICE_API_GET(clock_control, dev)->get_rate(dev, sys, rate);
    }
    return -ENOTSUP;
}
