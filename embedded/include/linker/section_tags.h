#ifndef ZEPHYR_INCLUDE_LINKER_SECTION_TAGS_H_
#define ZEPHYR_INCLUDE_LINKER_SECTION_TAGS_H_
#include <toolchain.h>

#if !defined(_ASMLANGUAGE)
#include <linker/sections.h>

#define __noinit		__in_section_unique(_NOINIT_SECTION_NAME)
#define __noinit_named(name)	__in_section_unique_named(_NOINIT_SECTION_NAME, name)
#define __irq_vector_table	Z_GENERIC_SECTION(_IRQ_VECTOR_TABLE_SECTION_NAME)
#define __sw_isr_table		Z_GENERIC_SECTION(_SW_ISR_TABLE_SECTION_NAME)


#endif /* !_ASMLANGUAGE */
#endif
