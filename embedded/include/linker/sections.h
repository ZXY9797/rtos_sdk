#ifndef ZEPHYR_INCLUDE_LINKER_SECTIONS_H_
#define ZEPHYR_INCLUDE_LINKER_SECTIONS_H_

/* Interrupts */
#define _IRQ_VECTOR_TABLE_SECTION_NAME	.gnu.linkonce.irq_vector_table
#define _IRQ_VECTOR_TABLE_SECTION_SYMS	.gnu.linkonce.irq_vector_table*

#define _SW_ISR_TABLE_SECTION_NAME	.gnu.linkonce.sw_isr_table
#define _SW_ISR_TABLE_SECTION_SYMS	.gnu.linkonce.sw_isr_table*


#if defined(_ASMLANGUAGE)

#endif /* _ASMLANGUAGE */

#include <linker/section_tags.h>

#endif /* ZEPHYR_INCLUDE_LINKER_SECTIONS_H_ */
