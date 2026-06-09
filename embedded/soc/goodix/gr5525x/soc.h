#ifndef _GR5525X_SOC_H_
#define _GR5525X_SOC_H_

#ifndef _ASMLANGUAGE

#include "gr55xx.h"

#ifdef __cplusplus
extern "C" {
#endif

void soc_early_init_hook(void);
void soc_late_init_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* !_ASMLANGUAGE */
#endif /* _GR5525X_SOC_H_ */
