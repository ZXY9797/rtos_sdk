#ifndef MSDK_INCLUDE_SOC_HOOKS_H_
#define MSDK_INCLUDE_SOC_HOOKS_H_

#ifdef __cplusplus
extern "C" {
#endif

void soc_reset_hook(void);
void soc_early_init_hook(void);
void soc_late_init_hook(void);

#ifdef __cplusplus
}
#endif

#endif
