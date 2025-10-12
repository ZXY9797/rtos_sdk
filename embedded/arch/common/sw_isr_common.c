#include "sw_isr_common.h"
#include <sw_isr_table.h>
#include <irq.h>
#include <sys/__assert.h>

#define CONFIG_GEN_IRQ_START_VECTOR 0
unsigned int __weak z_get_sw_isr_table_idx(unsigned int irq)
{
	unsigned int table_idx = irq - CONFIG_GEN_IRQ_START_VECTOR;

	__ASSERT_NO_MSG(table_idx < IRQ_TABLE_SIZE);

	return table_idx;
}
