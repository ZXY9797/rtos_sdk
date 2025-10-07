#include "device.h"
#include "drivers/irq_controller.h"
#include "errno.h"

int32_t irq_set_handler(struct device *dev, uint32_t irqno, irq_handler handler, const void *arg)
{
    if (DEVICE_API_GET(irq_controller, dev)->set_handler != NULL) {
        return DEVICE_API_GET(irq_controller, dev)->set_handler(dev, irqno, handler, arg);
    }
    return -ENOTSUP;
}

int32_t irq_priority_set(struct device *dev, uint32_t irqno, uint32_t preempt, uint32_t sub)
{
    if (DEVICE_API_GET(irq_controller, dev)->priority_set != NULL) {
        return DEVICE_API_GET(irq_controller, dev)->priority_set(dev, irqno, preempt, sub);
    }
    return -ENOTSUP;
}

int32_t irq_enable(struct device *dev, uint32_t irqno)
{
    if (DEVICE_API_GET(irq_controller, dev)->enable != NULL) {
        return DEVICE_API_GET(irq_controller, dev)->enable(dev, irqno);
    }
    return -ENOTSUP;
}

int32_t irq_disable(struct device *dev, uint32_t irqno)
{
    if (DEVICE_API_GET(irq_controller, dev)->disable != NULL) {
        return DEVICE_API_GET(irq_controller, dev)->disable(dev, irqno);
    }
    return -ENOTSUP;
}
