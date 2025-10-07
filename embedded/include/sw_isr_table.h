#ifndef MSDK_INCLUDE_SW_ISR_TABLE_H_
#define MSDK_INCLUDE_SW_ISR_TABLE_H_

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _isr_table_entry {
	const void *arg;
	void (*isr)(uint32_t irqno, const void *);
};

extern struct _isr_table_entry _sw_isr_table[];

struct _irq_parent_entry {
	const struct device *dev;
	uint32_t level;
	uint32_t irq;
	uint32_t offset;
};

#ifdef __cplusplus
}
#endif

#endif

