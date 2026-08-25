/* GR5525 app linker script. Addresses come from product layout.json. */

ENTRY(Reset_Handler)

_region_min_align = 4;

/* GCC RW Compress: app flash slot + physical RAM. */
__flash_size__ = BOOT_APP_LINK_SIZE;

MEMORY
{
    FLASH (rx) :    ORIGIN = BOOT_SLOT0_PAYLOAD_ORIGIN, LENGTH = __flash_size__
    RAM   (rw) :    ORIGIN = BOOT_RAM_BASE, LENGTH = BOOT_RAM_SIZE
}

_system_stack_size = 0x800;

SECTIONS
{
    __image_header_start = BOOT_SLOT0_ORIGIN;
    __rom_region_start = BOOT_SLOT0_PAYLOAD_ORIGIN;

    rom_start :
    {
        . = ALIGN(4);
        _vector_start = .;
        KEEP(*(.exc_vector_table))
        KEEP(*(".exc_vector_table.*"))
        KEEP(*(.vectors))
        KEEP(*(".vectors.*"))
        _vector_end = .;
        . = ALIGN(4);
    } > FLASH

    .product_info BOOT_PRODUCT_INFO_ORIGIN :
    {
        . = ALIGN(4);
        KEEP(*(.product_info))
        KEEP(*(".product_info.*"))
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
        KEEP(*crtbegin.o(.ctors))
        KEEP(*crtbegin?.o(.ctors))
        KEEP(*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))

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
        __init_EARLY_start = .;
        KEEP(*(SORT(.z_init_EARLY_P_?_*)))
        KEEP(*(SORT(.z_init_EARLY_P_??_*)))
        KEEP(*(SORT(.z_init_EARLY_P_???_*)))
        __init_PRE_KERNEL_1_start = .;
        KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_?_*)))
        KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_??_*)))
        KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_???_*)))
        __init_PRE_KERNEL_2_start = .;
        KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_?_*)))
        KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_??_*)))
        KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_???_*)))
        __init_PRE_KERNEL_3_start = .;
        KEEP(*(SORT(.z_init_PRE_KERNEL_3_P_?_*)))
        KEEP(*(SORT(.z_init_PRE_KERNEL_3_P_??_*)))
        KEEP(*(SORT(.z_init_PRE_KERNEL_3_P_???_*)))
        __init_POST_KERNEL_start = .;
        KEEP(*(SORT(.z_init_POST_KERNEL_P_?_*)))
        KEEP(*(SORT(.z_init_POST_KERNEL_P_??_*)))
        KEEP(*(SORT(.z_init_POST_KERNEL_P_???_*)))
        __init_APPLICATION_start = .;
        KEEP(*(SORT(.z_init_APPLICATION_P_?_*)))
        KEEP(*(SORT(.z_init_APPLICATION_P_??_*)))
        KEEP(*(SORT(.z_init_APPLICATION_P_???_*)))
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

    link_handler :
    {
        . = ALIGN(4);
        __link_handler_start = .;
        KEEP(*(.link_handler))
        __link_handler_end = .;
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
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN (__init_array_end = .);

        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors))
        PROVIDE_HIDDEN (__fini_array_end = .);

        _edata = .;
    } > RAM AT > FLASH

    __data_size__ = _edata - _sdata;
    __data_load_start = LOADADDR(datas);

    FPB (NOLOAD) :
    {
        . = ALIGN(4);
        *(.FPB)
        *(.FPB.*)
        *(FPB)
        *(FPB.*)
        . = ALIGN(4);
    } > RAM

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

    .sct_load_config :
    {
        . = ALIGN(4);
        _sct_load_start = .;
        KEEP(*(.sct_load_config))
        _sct_load_end = .;
        . = ALIGN(4);
    } > RAM

    KE_MSG_TABLE (NOLOAD) :
    {
        . = ALIGN(4);
        *(KE_MSG_TABLE)
        *(KE_MSG_TABLE.*)
        . = ALIGN(4);
    } > RAM

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        *(.stack)
        *(".stack.*")
        . = . + _system_stack_size;
        . = ALIGN(8);
        _estack = .;
    } > RAM

    _end = .;
    end = .;
    __bss_size__ = __bss_end - __bss_start;
    ASSERT((LOADADDR(datas) + SIZEOF(datas) - __rom_region_start) <= BOOT_SLOT0_PAYLOAD_SIZE,
           "Application payload exceeds the physical slot")
}
