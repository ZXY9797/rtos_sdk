/*
 * Demo loader-upgrade app: 152KB @ 0x08032000.
 */

MEMORY
{
    FLASH (rx) : ORIGIN = BOOT_UPGRADE_ORIGIN, LENGTH = BOOT_UPGRADE_SIZE
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
        . = ALIGN(4);
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
        __init_end = .;
        . = ALIGN(4);
    } > FLASH

    initlevel_error :
    {
        KEEP(*(SORT(.z_init_*)))
    }
    ASSERT(SIZEOF(initlevel_error) == 0,
           "Upgrade image cannot use post-kernel or undefined init levels.")

    rodata : ALIGN(4)
    {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > FLASH

    loader_payload : ALIGN(4)
    {
        _loader_payload_start = .;
        *(.loader_payload)
        *(.loader_payload.*)
        _loader_payload_end = .;
        _loader_payload_size = _loader_payload_end - _loader_payload_start;
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

    .stack (NOLOAD) : ALIGN(8)
    {
        _stack_end = .;
        . += 0x400;
        _estack = .;
    } > RAM
}
