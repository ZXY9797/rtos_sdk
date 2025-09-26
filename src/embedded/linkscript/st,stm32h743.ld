/* Program Entry, set to mark it as "used" and avoid gc */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) :    ORIGIN = 0x08000000,    LENGTH = 2048K /* 2048K flash */
    ITCM  (rx) :    ORIGIN = 0x00000008,    LENGTH = 64k   /* 64K  ITCM */
    DTCM  (rw) :    ORIGIN = 0x20000000,    LENGTH = 128k  /* 128K DTCM */
    RAM   (rw) :    ORIGIN = 0x24000000,    LENGTH = 512k  /* 256K AXI_RAM */
    RAM_D2(rw) :    ORIGIN = 0x30000000,    LENGTH = 288K  /* 288K D2RAM */
    RAM_D3(rw) :    ORIGIN = 0x38000000,    LENGTH = 64K   /* 64K  D3RAM */
}
_system_stack_size = 0x400;

SECTIONS
{
    .startup :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))            /* Startup code */
        *startup.s.obj(.text*)
        . = ALIGN(4);
    } > FLASH = 0

    _siitcm_code = LOADADDR (.ITCM);
    .ITCM :
    {
        . = ALIGN(4);
        _sitcm_code = .;
        *(.text.atan2f)
        *(.text.sqrt)
        *(.text.tanf)
        *(.text.pow)
        *(.text.lwrb*)
        *(.text.ddw*)
        *(.text.link*)
        *(.text.*memcpy*)
        *(.text.*memset*)
        *stm32h7xx_hal*(.text*)
        *librtt_nano.a(.text*)
        *(.ITCM)
        *(.ITCM.*)
        . = ALIGN(4);
        _eitcm_code = .;
    } > ITCM AT>FLASH

    .text :
    {
        . = ALIGN(4);
        /* _stext = .; */
        . = ALIGN(4);
        *(.text)                        /* remaining code */
        *(.text.*)                      /* remaining code */
        *(.rodata)                      /* read-only data (constants) */
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t*)

        /* section information for finsh shell */
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;

        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;

        /* section information for initial. */
        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;

        . = ALIGN(8);
        __rt_process_start = .;
        KEEP(*(SORT(.rti_process_fn*)))
        __rt_process_end = .;

        . = ALIGN(4);
        __ddw_data_cfg_start = .;
        KEEP(*(ddw_data_cfg))
        __ddw_data_cfg_end = .;

        . = ALIGN(4);
        __mlink_cb_desc_start = .;
        KEEP(*(.mlink_cb_desc))
        __mlink_cb_desc_end = .;

        . = ALIGN(4);
        _nlk_callbacks_start = ABSOLUTE(.);
        KEEP(*(SORT_BY_NAME(.nlk_callbacks) SORT_BY_NAME(.nlk_callbacks.*)))
        _nlk_callbacks_end = ABSOLUTE(.);

        . = ALIGN(4);

        PROVIDE(__ctors_start__ = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        PROVIDE(__ctors_end__ = .);

        . = ALIGN(4);

        _etext = .;
    } > FLASH

    /* .ARM.exidx is sorted, so has to go in its own output section.  */
    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)

        /* This is used by the startup in order to initialize the .data secion */

    } > FLASH
    __exidx_end = .;

    /* .data section which is used for initialized data */
    _sidata = LOADADDR (.data);

    .data :
    {
        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .data secion */
        _sdata = . ;

        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d*)

        PROVIDE(__dtors_start__ = .);
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        PROVIDE(__dtors_end__ = .);

        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .data secion */
        _edata = . ;
    } >DTCM AT>FLASH

    .stack :
    {
        . = ALIGN(4);
        _sstack = .;
        . = . + _system_stack_size;
        . = ALIGN(4);
        _estack = .;
    } >DTCM

    __bss_start = .;
    .bss :
    {
        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .bss secion */
        _sbss = .;

        *(.bss)
        *(.bss.*)
        *(COMMON)

        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .bss secion */
        _ebss = . ;

        *(.bss.init)
    } > DTCM
    __bss_end = .;
    _end = .;

	.axiram_data (NOLOAD) :
	{
        . = ALIGN(4);
        _axiram_data_begin = .;
        *(.axiram_data)
        *(.axiram_data*)
        . = ALIGN(4);
        _axiram_data_end = .;
	} > RAM

	.dmaram_data (NOLOAD) :
	{
        . = ALIGN(4);
        *(.dmaram_data)
        *(.dmaram_data*)
        . = ALIGN(4);
	} > RAM
}
