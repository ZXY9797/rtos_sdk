/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>

#define NVIC_BASE (0xE000E100UL)

typedef struct {
    volatile uint32_t ISER[8];
    volatile uint32_t RESERVED0[24];
    volatile uint32_t ICER[8];
    volatile uint32_t RESERVED1[24];
    volatile uint32_t ISPR[8];
    volatile uint32_t RESERVED2[24];
    volatile uint32_t ICPR[8];
    volatile uint32_t RESERVED3[24];
    volatile uint32_t IABR[8];
    volatile uint32_t RESERVED4[56];
    volatile uint8_t  IP[240];
    volatile uint32_t RESERVED5[644];
    volatile uint32_t STIR;
} NVIC_Type;

#define NVIC ((NVIC_Type *)NVIC_BASE)

void arch_irq_disable(unsigned int irq)
{
    NVIC->ICER[irq / 32] = (1UL << (irq % 32));
}

void arch_irq_enable(unsigned int irq)
{
    NVIC->ISER[irq / 32] = (1UL << (irq % 32));
}

void arch_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags)
{
    ARG_UNUSED(flags);

    NVIC->IP[irq] = (uint8_t)((prio << 4U) & 0xFFUL);
}

bool arch_irq_is_enabled(unsigned int irq)
{
    return (NVIC->ISER[irq / 32] & (1UL << (irq % 32))) != 0;
}
