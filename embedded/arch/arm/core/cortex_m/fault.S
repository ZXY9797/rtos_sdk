    .syntax unified
    .thumb

@ HardFault 弱定义（RT-Thread context_gcc.S 提供强符号覆盖）
@ MemManage/BusFault/UsageFault handler 在 fault.cc 中用 naked 函数实现
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
