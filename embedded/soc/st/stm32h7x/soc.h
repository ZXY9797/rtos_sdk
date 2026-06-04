#ifndef _STM32H7_SOC_H_
#define _STM32H7_SOC_H_

#ifndef _ASMLANGUAGE

#include <stm32h7xx.h>

#ifdef __cplusplus
extern "C" {
#endif

void soc_early_init_hook();
void soc_late_init_hook();

#ifdef __cplusplus
}
#endif

#endif /* !_ASMLANGUAGE */

#endif /* _STM32H7_SOC_H7_ */