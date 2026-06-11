/*
 * Demo loader: 24KB @ 0x08004000.
 */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 24K + 128K
    RAM   (rw) : ORIGIN = 0x20000000, LENGTH = 128K
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
        KEEP(*(.ctors))
        KEEP(*(.dtors))
        . = ALIGN(4);
    } > FLASH

    initlevel : ALIGN(4)
    {
        __init_start = .;
        __init_EARLY_start = .;
        KEEP(*(.z_init_EARLY*))
        __init_PRE_KERNEL_1_start = .;
        KEEP(*(.z_init_PRE_KERNEL_1*))
        __init_PRE_KERNEL_2_start = .;
        KEEP(*(.z_init_PRE_KERNEL_2*))
        __init_PRE_KERNEL_3_start = .;
        KEEP(*(.z_init_PRE_KERNEL_3*))
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
        . += 0x800;
        _estack = .;
    } > RAM
}
