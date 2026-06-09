ENTRY(Reset_Handler)

_region_min_align = 4;

/* GCC RW Compress: flash size = physical flash + physical RAM */
__flash_size__ = 1024K + 256K;

MEMORY
{
    FLASH (rx) :    ORIGIN = 0x00200000,    LENGTH = __flash_size__
    RAM   (rw) :    ORIGIN = 0x20000000,    LENGTH = 256K
}

_system_stack_size = 0x800;

SECTIONS
{
    __rom_region_start = (0x00200000 + 0x0);

    rom_start :
    {
        . = ALIGN(4);
        _vector_start = .;
        KEEP(*(.exc_vector_table))
        KEEP(*(".exc_vector_table.*"))
        _vector_end = .;
        . = ALIGN(4);
    } > FLASH

    prepc :
    {
        . = ALIGN(4);
        *(.text.soc_reset_hook)
        *(.text.soc_early_init_hook)
        *(.text.SystemInit)
        . = ALIGN(4);
    } > FLASH

    text :
    {
        __text_region_start = .;
        *(.text)
        *(".text.*")
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        /*ctors*/
        KEEP(*crtbegin.o(.ctors))
        KEEP(*crtbegin?.o(.ctors))
        KEEP(*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))

        /*dtors*/
        KEEP(*crtbegin.o(.dtors))
        KEEP(*crtbegin?.o(.dtors))
        KEEP(*(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        __text_region_end = .;
    } > FLASH

    .ARM.extab :
    {
        . = ALIGN(4);
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        . = ALIGN(4);
    } > FLASH

    .ARM :
    {
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
        . = ALIGN(4);
    } > FLASH

    __rodata_region_start = .;

    initlevel :
    {
        . = ALIGN(4);
        __init_start = .;
        KEEP(*(.z_init_EARLY*))
        __init_EARLY_end = .;
        __init_PRE_KERNEL_1_start = .;
        KEEP(*(.z_init_PRE_KERNEL_1*))
        __init_PRE_KERNEL_1_end = .;
        __init_PRE_KERNEL_2_start = .;
        KEEP(*(.z_init_PRE_KERNEL_2*))
        __init_PRE_KERNEL_2_end = .;
        __init_PRE_KERNEL_3_start = .;
        KEEP(*(.z_init_PRE_KERNEL_3*))
        __init_PRE_KERNEL_3_end = .;
        __init_POST_KERNEL_start = .;
        KEEP(*(.z_init_POST_KERNEL*))
        __init_POST_KERNEL_end = .;
        __init_APPLICATION_start = .;
        KEEP(*(.z_init_APPLICATION*))
        __init_APPLICATION_end = .;
        __init_end = .;
        . = ALIGN(4);
    } > FLASH

    device_area :
    {
        . = ALIGN(4);
        __device_start = .;
        KEEP(*(".device_area.*"))
        __device_end = .;
        . = ALIGN(4);
    } > FLASH

    device_deps :
    {
        . = ALIGN(4);
        __device_deps_start = .;
        KEEP(*(".device_deps.*"))
        __device_deps_end = .;
        . = ALIGN(4);
    } > FLASH

    rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(".rodata.*")
        __rodata_region_end = .;
        . = ALIGN(4);
    } > FLASH

    datas :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.vtable)
        *(.data)
        *(".data.*")
        *(.gnu.linkonce.r.*)

        . = ALIGN(4);
        *(.ramfunc)
        *(".ramfunc.*")
        . = ALIGN(4);

        __data_region_start = .;
        *(.data_sram)
        *(".data_sram.*")
        __data_region_end = .;

        . = ALIGN(4);
        /* pre-init data */
        KEEP(*(.pre_init_array))
        PROVIDE_HIDDEN (__pre_init_array_end = .);

        /* init data */
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
        PROVIDE_HIDDEN (__init_array_end = .);

        /* fini data */
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.dtors))
        PROVIDE_HIDDEN (__fini_array_end = .);

        _edata = .;
    } > RAM AT > FLASH

    __data_size__ = _edata - _sdata;

    __data_load_start = LOADADDR(datas);

    bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss)
        *(".bss.*")
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit)
        *(".noinit.*")
        . = ALIGN(4);
    } > RAM

    /* Globals from LD should be at the end so that they get the
     * highest addresses. This region is used for sct_load
     * configuration in the linker script.
     */
    .sct_load_config :
    {
        . = ALIGN(4);
        _sct_load_start = .;
        KEEP(*(.sct_load_config))
        _sct_load_end = .;
        . = ALIGN(4);
    } > RAM

    /* Make sure there is enough room for the stack */
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        *(.stack)
        *(".stack.*")
        . = . + _system_stack_size;
        . = ALIGN(8);
    } > RAM

    _end = .;
    __bss_size__ = __bss_end - __bss_start;
}
