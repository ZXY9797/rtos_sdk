#include <toolchain.h>
_ASM_FILE_PROLOGUE

GTEXT(Reset_Handler)
GTEXT(soc_reset_hook)
GTEXT(soc_late_init_hook)
GTEXT(z_prep_c)
GTEXT(z_cstart)

SECTION_SUBSEC_FUNC(text,Reset_Handler,Reset_Handler)

SECTION_SUBSEC_FUNC(text,Reset_Handler,__start)

    movs.n r0, #0
    msr CONTROL, r0
    isb
    ldr   sp, =_estack      /* set stack pointer */

    isb
    bl    soc_reset_hook
    bl    z_prep_c
    bl    z_cstart
