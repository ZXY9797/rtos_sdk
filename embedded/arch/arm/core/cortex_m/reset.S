
  .syntax unified
  .thumb

.extern  z_prep_c
.extern  z_cstart
.extern  soc_reset_hook
.extern  soc_early_init_hook
.extern  soc_late_init_hook

  .section  .text.Reset_Handler
  .weak  Reset_Handler
  .type  Reset_Handler, %function
Reset_Handler:
    movs.n r0, #0
    msr CONTROL, r0
    isb
    ldr   sp, =_estack      /* set stack pointer */

    isb
    bl    soc_reset_hook
    bl    z_prep_c
    bl    soc_early_init_hook
    bl    soc_late_init_hook
    bl    z_cstart
