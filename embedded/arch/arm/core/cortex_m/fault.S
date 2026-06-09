    .syntax unified
    .thumb

@ 通用异常入口宏
@ 保存软件帧 (r4-r11) + 复制硬件帧 (r0-xpsr)，然后调用 arm_fault_handler
.macro FAULT_ENTRY name
    .thumb_func
    .weak \name
\name:
    TST     lr, #0x04
    ITE     EQ
    MRSEQ   r12, msp
    MRSNE   r12, psp
    SUB     sp, sp, #32
    STMIA   sp, {r4-r11}
    @ 复制硬件帧 (r0-xpsr) 到 [SP+32]..[SP+60]
    LDR     r4, [r12, #0]
    LDR     r5, [r12, #4]
    LDR     r6, [r12, #8]
    LDR     r7, [r12, #12]
    LDR     r8, [r12, #16]
    LDR     r9, [r12, #20]
    LDR     r10, [r12, #24]
    LDR     r11, [r12, #28]
    STR     r4, [sp, #32]
    STR     r5, [sp, #36]
    STR     r6, [sp, #40]
    STR     r7, [sp, #44]
    STR     r8, [sp, #48]
    STR     r9, [sp, #52]
    STR     r10, [sp, #56]
    STR     r11, [sp, #60]
    MOV     r0, sp
    MOV     r1, lr
    BL      arm_fault_handler
1:  B       1b
.endm

@ HardFault 弱定义（RT-Thread context_gcc.S 提供强符号覆盖）
    .thumb_func
    .weak      HardFault_Handler
HardFault_Handler:
    TST     lr, #0x04
    ITE     EQ
    MRSEQ   r12, msp
    MRSNE   r12, psp
    SUB     sp, sp, #32
    STMIA   sp, {r4-r11}
    @ 复制硬件帧 (r0-xpsr) 到 [SP+32]..[SP+60]
    LDR     r4, [r12, #0]
    LDR     r5, [r12, #4]
    LDR     r6, [r12, #8]
    LDR     r7, [r12, #12]
    LDR     r8, [r12, #16]
    LDR     r9, [r12, #20]
    LDR     r10, [r12, #24]
    LDR     r11, [r12, #28]
    STR     r4, [sp, #32]
    STR     r5, [sp, #36]
    STR     r6, [sp, #40]
    STR     r7, [sp, #44]
    STR     r8, [sp, #48]
    STR     r9, [sp, #52]
    STR     r10, [sp, #56]
    STR     r11, [sp, #60]
    MOV     r0, sp
    MOV     r1, lr
    BL      arm_fault_handler
1:  B       1b

@ MemManage/BusFault/UsageFault 异常入口
FAULT_ENTRY MemManage_Handler
FAULT_ENTRY BusFault_Handler
FAULT_ENTRY UsageFault_Handler
