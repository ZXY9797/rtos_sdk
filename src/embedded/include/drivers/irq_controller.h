#ifndef _IRQ_CONTROLLER_H_
#define _IRQ_CONTROLLER_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*irq_handler)(unsigned int irqno, void *arg);

typedef struct {
    void (*irq_set_handler)(unsigned int irqno, irq_handler handler, void *arg);
    void (*irq_priority_set)(unsigned int irqno, unsigned int prio);
    void (*irq_enable)(unsigned int irqno);
    void (*irq_disable)(unsigned int irqno);
} irq_controller_api_t;

#ifdef __cplusplus
}
#endif

#endif
