  .syntax unified
  .thumb

.global arch_early_memset
.section .text
.thumb_func
arch_early_memset:
    push    {r0, r4, lr}
    mov     r3, r0

    cmp     r2, #0
    beq     memset_done

    and     r1, r1, #0xFF
    orr     r1, r1, r1, lsl #8
    orr     r1, r1, r1, lsl #16

memset_loop:
    strb    r1, [r0], #1
    subs    r2, r2, #1
    bne     memset_loop

memset_done:
    mov     r0, r3
    pop     {r0, r4, pc}


.global arch_early_memcpy
.section .text
.thumb_func
arch_early_memcpy:
    push    {r0, r4, lr}
    mov     r3, r0

    cmp     r2, #0
    beq     memcpy_done

memcpy_loop:
    ldrb    r4, [r1], #1
    strb    r4, [r0], #1
    subs    r2, r2, #1
    bne     memcpy_loop

memcpy_done:
    mov     r0, r3
    pop     {r0, r4, pc}
