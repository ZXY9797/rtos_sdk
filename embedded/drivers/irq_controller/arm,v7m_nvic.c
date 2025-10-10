#include "stm32h743xx.h"
#include "toolchain.h"
#include <stdint.h>
#include "sw_isr_table.h"
#include <arch/cpu.h>
#include "soc/soc.h"
#include "drivers/irq_controller.h"

extern unsigned long _estack;

struct _isr_table_entry  _sw_isr_table[150] = {
};

static void _isr_wrapper(void) {
    int32_t irq_number = __get_IPSR();
    irq_number -= 16;

    struct _isr_table_entry *entry = &_sw_isr_table[irq_number];
    if (entry) {
        (entry->isr)(irq_number, entry->arg);
    }
}

void Dummy_Handler() {
    while(1);
}

extern void Reset_Handler(void);

__weak void NMI_Handler(void) {Dummy_Handler();}
__weak void HardFault_Handler(void) {Dummy_Handler();}
__weak void MemManage_Handler(void) {Dummy_Handler();}
__weak void BusFault_Handler(void) {Dummy_Handler();}
__weak void UsageFault_Handler(void) {Dummy_Handler();}
__weak void SVC_Handler(void) {Dummy_Handler();}
__weak void DebugMon_Handler(void) {Dummy_Handler();}
__weak void PendSV_Handler(void) {Dummy_Handler();}
__weak void SysTick_Handler(void) {Dummy_Handler();}
__weak void IRQ0_Handler(void) {_isr_wrapper();}
__weak void IRQ1_Handler(void) {_isr_wrapper();}
__weak void IRQ2_Handler(void) {_isr_wrapper();}
__weak void IRQ3_Handler(void) {_isr_wrapper();}
__weak void IRQ4_Handler(void) {_isr_wrapper();}
__weak void IRQ5_Handler(void) {_isr_wrapper();}
__weak void IRQ6_Handler(void) {_isr_wrapper();}

const uintptr_t __GENERIC_SECTION(.isr_vector) __vector_table[] = {
  (uintptr_t)&_estack,
  (uintptr_t)&Reset_Handler,
  (uintptr_t)&NMI_Handler,
  (uintptr_t)&HardFault_Handler,
  (uintptr_t)&MemManage_Handler,
  (uintptr_t)&BusFault_Handler,
  (uintptr_t)&UsageFault_Handler,
  (uintptr_t)0,
  (uintptr_t)0,
  (uintptr_t)0,
  (uintptr_t)0,
  (uintptr_t)&SVC_Handler,
  (uintptr_t)&DebugMon_Handler,
  (uintptr_t)0,
  (uintptr_t)&PendSV_Handler,
  (uintptr_t)&SysTick_Handler,
};

static int32_t arm_v7m_nvic_set_handler(struct device *dev, uint32_t irqno, irq_handler handler, const void *arg)
{
    _sw_isr_table[irqno].isr = handler;
    _sw_isr_table[irqno].arg = arg;
    return 0;
}

static int32_t arm_v7m_nvic_priority_set(struct device *dev, uint32_t irqno, uint32_t preempt, uint32_t sub)
{
    uint32_t prioritygroup;

    /* Check the parameters */
    assert_param(IS_NVIC_SUB_PRIORITY(SubPriority));
    assert_param(IS_NVIC_PREEMPTION_PRIORITY(PreemptPriority));

    prioritygroup = NVIC_GetPriorityGrouping();

    NVIC_SetPriority(irqno, NVIC_EncodePriority(prioritygroup, preempt, sub));
    return 0;
}

static int32_t arm_v7m_nvic_irq_enable(struct device *dev, uint32_t irqno)
{
    NVIC_EnableIRQ((IRQn_Type)irqno);
    return 0;
}

static int32_t arm_v7m_nvic_irq_disable(struct device *dev, uint32_t irqno)
{
    NVIC_EnableIRQ((IRQn_Type)irqno);
    return 0;
}

static DEVICE_API(irq_controller, arm_v7m_nvic_api) = {
	.set_handler = arm_v7m_nvic_set_handler,
	.priority_set = arm_v7m_nvic_priority_set,
	.enable = arm_v7m_nvic_irq_enable,
	.disable = arm_v7m_nvic_irq_disable,
};

static int arm_v7m_nvic_init(const struct device *dev)
{
    NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_2);
    return 0;
}

DEVICE_DT_DEFINE(DT_NODELABEL(nvic),
		    arm_v7m_nvic_init,
		    NULL,
		    NULL, NULL,
		    PRE_KERNEL_1,
		    0,
		    &arm_v7m_nvic_api);