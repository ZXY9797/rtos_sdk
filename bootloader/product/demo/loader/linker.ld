/*
 * Demo loader. Address and size come from product layout.json.
 */

MEMORY
{
    FLASH (rx) : ORIGIN = BOOT_LOADER_ORIGIN, LENGTH = BOOT_LOADER_SIZE
    RAM   (rw) : ORIGIN = BOOT_RAM_BASE, LENGTH = BOOT_RAM_SIZE
}

__flash_size__ = LENGTH(FLASH);
__rom_region_start = ORIGIN(FLASH);
__rom_region_size  = LENGTH(FLASH);
__ram_region_start = ORIGIN(RAM);
__ram_region_size  = LENGTH(RAM);
__ram_end = ORIGIN(RAM) + LENGTH(RAM);

ENTRY(Reset_Handler)

SECTIONS
{
    rom_start : ALIGN(4)
    {
        . = ALIGN(4);
        _vector_start = .;
        KEEP(*(.exc_vector_table))
        KEEP(*(.exc_vector_table.*))
        . = ALIGN(4);
        _vector_end = .;
    } > FLASH

    text : ALIGN(4)
    {
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
        *(.init)
        *(.fini)
        . = ALIGN(4);
    } > FLASH

    preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    initlevel : ALIGN(4)
    {
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

    rodata : ALIGN(4)
    {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > FLASH

    __rom_end = .;
    _flash_used = __rom_end - __rom_region_start;

    datas : ALIGN(4)
    {
        __data_start = .;
        *(.data)
        *(.data.*)
        __data_end = .;
    } > RAM AT > FLASH

    __data_load_start = LOADADDR(datas);
    ASSERT((LOADADDR(datas) + SIZEOF(datas))
               <= (ORIGIN(FLASH) + LENGTH(FLASH)),
           "Loader image exceeds its physical partition")

    bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    } > RAM

    noinit (NOLOAD) : ALIGN(4)
    {
        *(.noinit)
        *(.noinit.*)
    } > RAM

    .stack (NOLOAD) : ALIGN(8)
    {
        _stack_end = .;
        . += BOOT_STACK_SIZE;
        _estack = .;
    } > RAM
}
