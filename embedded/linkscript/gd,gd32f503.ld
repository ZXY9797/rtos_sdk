/* Program Entry, set to mark it as "used" and avoid gc */
ENTRY(Reset_Handler)

_region_min_align = 4;

/* GCC RW Compress: flash size = physical flash + physical RAM */
__flash_size__ = 384K + 128K;

MEMORY
{
    FLASH (rx) :    ORIGIN = 0x08000000,    LENGTH = __flash_size__ /* flash + RAM for compress */
    RAM   (rw) :    ORIGIN = 0x20000000,    LENGTH = 128K  /* 128K SRAM */
}

_system_stack_size = 0x800;

SECTIONS
{
    __rom_region_start = (0x08000000 + 0x0);

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
        *(".TEXT.*")
        *(.gnu.linkonce.t.*)
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
        . = ALIGN(4);

        KEEP(*(.init))
        KEEP(*(.fini))
        /* .ctors */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
    __ctors_start__ = ALIGN(4);
        *(SORT(.ctors.*))
        *(.ctors)
    __ctors_end__ = ALIGN(4);

        /* .dtors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
    __dtors_start__ = ALIGN(4);
        *(SORT(.dtors.*))
        *(.dtors)
    __dtors_end__ = ALIGN(4);
        KEEP(*(.eh_frame*))
    } > FLASH
    __text_region_end = .;

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    __rodata_region_start = .;
    initlevel :
    {
        __init_start = .;
        __init_EARLY_start = .;
            KEEP(*(SORT(.z_init_EARLY_P_?_*)));
            KEEP(*(SORT(.z_init_EARLY_P_??_*)));
            KEEP(*(SORT(.z_init_EARLY_P_???_*)));
        __init_PRE_KERNEL_1_start = .;
            KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_?_*)));
            KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_??_*)));
            KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_???_*)));
        __init_PRE_KERNEL_2_start = .;
            KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_?_*)));
            KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_??_*)));
            KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_???_*)));
        __init_PRE_KERNEL_3_start = .;
            KEEP(*(SORT(.z_init_PRE_KERNEL_3_P_?_*)));
            KEEP(*(SORT(.z_init_PRE_KERNEL_3_P_??_*)));
            KEEP(*(SORT(.z_init_PRE_KERNEL_3_P_???_*)));
        __init_POST_KERNEL_start = .;
            KEEP(*(SORT(.z_init_POST_KERNEL_P_?_*)));
            KEEP(*(SORT(.z_init_POST_KERNEL_P_??_*)));
            KEEP(*(SORT(.z_init_POST_KERNEL_P_???_*)));
        __init_APPLICATION_start = .;
            KEEP(*(SORT(.z_init_APPLICATION_P_?_*)));
            KEEP(*(SORT(.z_init_APPLICATION_P_??_*)));
            KEEP(*(SORT(.z_init_APPLICATION_P_???_*)));
        __init_end = .;
    } > FLASH

    initlevel_error :
    {
        KEEP(*(SORT(.z_init_*)))
    }
    ASSERT(SIZEOF(initlevel_error) == 0, "Undefined initialization levels used.")

    rodata :
    {
        *(.rodata)
        *(".rodata.*")
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
    } > FLASH
    __rodata_region_end = .;

    . = ALIGN(_region_min_align);
    . = ALIGN( 1 << LOG2CEIL(__rodata_region_end - ADDR(rom_start)));
    __rom_region_end = __rom_region_start + . - ADDR(rom_start);
    __rom_region_size = __rom_region_end - __rom_region_start;

    . = 0x20000000;
    . = ALIGN(_region_min_align);
    _image_ram_start = .;

    datas :
    {
        __data_region_start = .;
        __data_start = .;
        *(vtable)
        *(.dma)
        *(".dma.*")

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);
        . = ALIGN(4);

        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);
        KEEP(*(.jcr*))
        . = ALIGN(4);

        __data_end = .;
    } > RAM AT > FLASH
    __data_size = __data_end - __data_start;
    __data_load_start = LOADADDR(datas);
    __data_region_load_start = LOADADDR(datas);

    __data_region_end = .;

    .bss (NOLOAD) : ALIGN_WITH_INPUT
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss)
        *(".bss.*")
        *(COMMON)
        __bss_end = ALIGN(4);
    } > RAM AT > RAM

    /* Heap starts after BSS */
    PROVIDE(end = .);
    PROVIDE(_end = .);

    noinit (NOLOAD) :
    {
        *(.noinit)
        *(".noinit.*")
    } > RAM AT > RAM

    .stack :
    {
        . = ALIGN(4);
        _sstack = .;
        . = . + _system_stack_size;
        . = ALIGN(4);
        _estack = .;
    } >RAM

    .ARM.attributes 0 :
    {
        KEEP(*(.ARM.attributes))
        KEEP(*(.gnu.attributes))
    }

    .sct_load_config :
    {
        . = ALIGN(4);
        KEEP(*(.sct_load_config))
        . = ALIGN(4);
    } > FLASH

    .last_section :
    {
        KEEP(*(.last_section))
    } > FLASH

    _flash_used = LOADADDR(.last_section) + SIZEOF(.last_section) - __rom_region_start;
}
