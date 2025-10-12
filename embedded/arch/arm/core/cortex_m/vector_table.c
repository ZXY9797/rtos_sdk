#include <toolchain.h>
#include <arch/cpu.h>

#if 0
extern unsigned long _estack;
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

  (uintptr_t)&IRQ0_Handler,
  (uintptr_t)&IRQ1_Handler,
  (uintptr_t)&IRQ2_Handler,
  (uintptr_t)&IRQ3_Handler,
};
#endif