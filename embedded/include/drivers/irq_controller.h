#ifndef _IRQ_CONTROLLER_H_
#define _IRQ_CONTROLLER_H_
#include "stdint.h"
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*irq_handler)(uint32_t irqno, const void *arg);

struct irq_controller_driver_api {
    int32_t (*set_handler)(struct device *dev, uint32_t irqno, irq_handler handler, const void *arg);
    int32_t (*priority_set)(struct device *dev, uint32_t irqno, uint32_t preempt, uint32_t sub);
    int32_t (*enable)(struct device *dev, uint32_t irqno);
    int32_t (*disable)(struct device *dev, uint32_t irqno);
};

int32_t irq_set_handler(struct device *dev, uint32_t irqno, irq_handler handler, const void *arg);
int32_t irq_priority_set(struct device *dev, uint32_t irqno, uint32_t preempt, uint32_t sub);
int32_t irq_enable(struct device *dev, uint32_t irqno);
int32_t irq_disable(struct device *dev, uint32_t irqno);

#ifdef __cplusplus
}
#endif

#endif
