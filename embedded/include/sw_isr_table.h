#ifndef ZEPHYR_INCLUDE_SW_ISR_TABLE_H_
#define ZEPHYR_INCLUDE_SW_ISR_TABLE_H_

#if !defined(_ASMLANGUAGE)

#include "stdint.h"
#include <device.h>
#include <sys/iterable_sections.h>
#include <toolchain.h>
#include <sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default vector for the IRQ vector table */
void _isr_wrapper(void);

struct _isr_table_entry {
	const void *arg;
	void (*isr)(const void *);
};

extern struct _isr_table_entry _sw_isr_table[];

struct _irq_parent_entry {
	const struct device *dev;
	uint32_t level;
	uint32_t irq;
	uint32_t offset;
};

/*
 * Data structure created in a special binary .intlist section for each
 * configured interrupt. gen_irq_tables.py pulls this out of the binary and
 * uses it to create the IRQ vector table and the _sw_isr_table.
 *
 * More discussion in include/linker/intlist.ld
 *
 * This is a version used when CONFIG_ISR_TABLES_LOCAL_DECLARATION is disabled.
 * See _isr_list_sname used otherwise.
 */
struct _isr_list {
	/** IRQ line number */
	int32_t irq;
	/** Flags for this IRQ, see ISR_FLAG_* definitions */
	int32_t flags;
	/** ISR to call */
	void *func;
	/** Parameter for non-direct IRQs */
	const void *param;
};

/*
 * Data structure created in a special binary .intlist section for each
 * configured interrupt. gen_isr_tables.py pulls this out of the binary and
 * uses it to create linker script chunks that would place interrupt table entries
 * in the right place in the memory.
 *
 * This is a version used when CONFIG_ISR_TABLES_LOCAL_DECLARATION is enabled.
 * See _isr_list used otherwise.
 */
struct _isr_list_sname {
	/** IRQ line number */
	int32_t irq;
	/** Flags for this IRQ, see ISR_FLAG_* definitions */
	int32_t flags;
	/** The section name */
	const char sname[];
};


/** This interrupt gets put directly in the vector table */
#define ISR_FLAG_DIRECT BIT(0)

#define _MK_ISR_NAME(x, y) __MK_ISR_NAME(x, y)
#define __MK_ISR_NAME(x, y) __isr_ ## x ## _irq_ ## y

// Modify
#define _MK_ISR_INFO_NAME(func, irqn, id) __MK_ISR_INFO_NAME(func, irqn, id)
#define __MK_ISR_INFO_NAME(func, irqn, id) __isr_info_ ## func ## _irq_ ## irqn ## _ ## id

#define __MK_ISR_ELEMENT_NAME(func, id) __isr_table_entry_ ## func ## _irq_ ## id
#define _MK_ISR_ELEMENT_NAME(func, id) __MK_ISR_ELEMENT_NAME(func, id)

#define _MK_ISR_SECTION_NAME(prefix, file, counter)                                                \
	"." Z_STRINGIFY(prefix) "." file "." Z_STRINGIFY(counter)

#define _MK_ISR_ELEMENT_SECTION(counter) _MK_ISR_SECTION_NAME(irq, __FILE__, counter)
#define _MK_IRQ_ELEMENT_SECTION(counter) _MK_ISR_SECTION_NAME(isr, __FILE__, counter)

/* Separated macro to create ISR table entry only.
 * Used by Z_ISR_DECLARE and ISR tables generation script.
 */
#define _Z_ISR_TABLE_ENTRY(func, param, sect) \
	static Z_DECL_ALIGN(struct _isr_table_entry)                              \
		__attribute__((section(sect)))                                        \
		__used _MK_ISR_ELEMENT_NAME(func, __COUNTER__) = {                    \
			.arg = (const void *)(param),                                     \
			.isr = (void (*)(const void *))(void *)(func)                     \
	}

#define _Z_ISR_DECLARE_C(irq, flags, func, param, counter)                               \
	static Z_DECL_ALIGN(struct _isr_list) Z_GENERIC_SECTION(.intList) __used             \
	_MK_ISR_NAME(func, counter) = {irq, flags, }}

#define Z_ISR_DECLARE_C(irq, flags, func, param, counter) \
	_Z_ISR_DECLARE_C(irq, flags, func, param, counter)

/* Create an entry for _isr_table to be then placed by the linker.
 * An instance of struct _isr_list which gets put in the .intList
 * section is created with the name of the section where _isr_table entry is placed to be then
 * used by isr generation script to create linker script chunk.
 */
#define Z_ISR_DECLARE(irq, flags, func, param)                                 \
  static const Z_DECL_ALIGN(struct _isr_list)                                  \
      Z_GENERIC_SECTION(.static_irq_info) __used                               \
      _MK_ISR_INFO_NAME(func, irq, __COUNTER__) = {irq, flags, (void *)&func,  \
                                                   (const void *)param}

#define IRQ_TABLE_SIZE (CONFIG_NUM_IRQS)

void z_isr_install(unsigned int irq, void (*routine)(const void *),
		   const void *param);

int z_isr_uninstall(unsigned int irq, void (*routine)(const void *),
		    const void *param);

#ifdef __cplusplus
}
#endif
#endif /* _ASMLANGUAGE */
#endif /* ZEPHYR_INCLUDE_SW_ISR_TABLE_H_ */

