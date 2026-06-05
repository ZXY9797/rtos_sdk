#ifndef _GD32F50X_SOC_H_
#define _GD32F50X_SOC_H_

#ifndef _ASMLANGUAGE

#include "gd32f50x.h"

#ifdef __cplusplus
extern "C" {
#endif

void soc_early_init_hook(void);
void soc_late_init_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* !_ASMLANGUAGE */

#endif /* _GD32F50X_SOC_H_ */
