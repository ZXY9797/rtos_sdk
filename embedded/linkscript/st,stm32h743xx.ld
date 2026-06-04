/* Program Entry, set to mark it as "used" and avoid gc */
ENTRY(Reset_Handler)

_region_min_align = 32;

MEMORY
{
    FLASH (rx) :    ORIGIN = 0x08000000,    LENGTH = 2048K /* 2048K flash */
    RAM   (rw) :    ORIGIN = 0x24000000,    LENGTH = 512k  /* 512 AXI_RAM */
    ITCM  (rx) :    ORIGIN = 0x00000000,    LENGTH = 64k   /* 64K  ITCM */
    DTCM  (rw) :    ORIGIN = 0x20000000,    LENGTH = 128k  /* 128K DTCM */
    BACKUP_SRAM ( rw ) : ORIGIN = (0x38800000), LENGTH = 4K
    SRAM1(rw) :    ORIGIN = 0x30000000,    LENGTH = 128K
    SRAM2(rw) :    ORIGIN = 0x30020000,    LENGTH = 128K
    SRAM3(rw) :    ORIGIN = 0x30040000,    LENGTH = 32K
    SRAM4(rw) :    ORIGIN = 0x38000000,    LENGTH = 64K
}
_system_stack_size = 0x400;

SECTIONS
{
    __rom_region_start = (0x8000000 + 0x0);

    rom_start :
    {
        . = ALIGN(4);
        _vector_start = .;
        /* KEEP(*(.isr_vector)) */
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

    .itcm : SUBALIGN(4)
    {
        __itcm_start = .;
        *libosal.a:(.text*)
        *libirq_controller.a:(.text*)
        *libsystem.a:(.text*)
        *libsoc.a:(.text*)
        *libtimer.a:(.text*)
        *(.text._isr_wrapper)
        *(.itcm)
        *(".itcm.*")
        __itcm_end = .;
    } > ITCM AT> FLASH
    __itcm_size = __itcm_end - __itcm_start;
    __itcm_load_start = LOADADDR(.itcm);

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
        __init_POST_KERNEL_start = .;
            KEEP(*(SORT(.z_init_POST_KERNEL_P_?_*)));
            KEEP(*(SORT(.z_init_POST_KERNEL_P_??_*)));
            KEEP(*(SORT(.z_init_POST_KERNEL_P_???_*)));
        __init_APPLICATION_start = .;
            KEEP(*(SORT(.z_init_APPLICATION_P_?_*)));
            KEEP(*(SORT(.z_init_APPLICATION_P_??_*)));
            KEEP(*(SORT(.z_init_APPLICATION_P_???_*)));
        __init_SMP_start = .;
            KEEP(*(SORT(.z_init_SMP_P_?_*)));
            KEEP(*(SORT(.z_init_SMP_P_??_*)));
            KEEP(*(SORT(.z_init_SMP_P_???_*)));
        __init_end = .;
    } > FLASH

    device_area :
    {
        _device_list_start = .;
            KEEP(*(SORT(._device.static.*_?_*)));
            KEEP(*(SORT(._device.static.*_??_*)));
            KEEP(*(SORT(._device.static.*_???_*)));
            KEEP(*(SORT(._device.static.*_????_*)));
            KEEP(*(SORT(._device.static.*_?????_*)));
        _device_list_end = .;;
    } > FLASH

    initlevel_error :
    {
        KEEP(*(SORT(.z_init_*)))
    }
    ASSERT(SIZEOF(initlevel_error) == 0, "Undefined initialization levels used.")

    device_deps : ALIGN_WITH_INPUT
    {
        __device_deps_start = .;
        KEEP(*(SORT(.__device_deps_pass2*)));
        __device_deps_end = .;
    } > FLASH

    symbol_to_keep : ALIGN_WITH_INPUT
    {
        __symbol_to_keep_start = .;
        KEEP(*(SORT(.symbol_to_keep*)));
        __symbol_to_keep_end = .;
    } > FLASH

    irq_info : ALIGN_WITH_INPUT
    {
        . = ALIGN(4);
        __static_irq_info_start = .;
        KEEP(*(SORT(.static_irq_info*)));
        __static_irq_info_end = .;
        . = ALIGN(4);
    } > FLASH

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

    . = 0x24000000;
    . = ALIGN(_region_min_align);
    _image_ram_start = .;
    .ramfunc :
    {
        __ramfunc_region_start = .;
        . = ALIGN(_region_min_align); . = ALIGN( 1 << LOG2CEIL(__ramfunc_size));
        __ramfunc_start = .;
        *(.ramfunc)
        *(".ramfunc.*")
        . = ALIGN(_region_min_align); . = ALIGN( 1 << LOG2CEIL(__ramfunc_size));
        __ramfunc_end = .;
    } > RAM AT > FLASH
    __ramfunc_size = __ramfunc_end - __ramfunc_start;
    __ramfunc_load_start = LOADADDR(.ramfunc);

    datas :
    {
        __data_region_start = .;
        __data_start = .;
        *(vtable)
        *(.dma)
        *(".dma.*")
        *(.axiram)
        *(".axiram.*")

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

    device_states :
    {
        . = ALIGN(4);
        __device_states_start = .;
        KEEP(*(".z_devstate"));
        KEEP(*(".z_devstate.*"));
        __device_states_end = .;
        . = ALIGN(4);
    } > RAM AT > FLASH
    __data_region_end = .;

    .dtcm_bss (NOLOAD) : SUBALIGN(4)
    {
        __dtcm_start = .;
        __dtcm_bss_start = .;
        *(.dtcm_bss)
        *(".dtcm_bss.*")
        *(.bss)
        *(".bss.*")
        *(COMMON)
        __dtcm_bss_end = .;
    } > DTCM
    .dtcm_noinit (NOLOAD) : SUBALIGN(4)
    {
        __dtcm_noinit_start = .;
        *(.dtcm_noinit)
        *(".dtcm_noinit.*")
        __dtcm_noinit_end = .;
    } > DTCM
    .dtcm_data : SUBALIGN(4)
    {
        __dtcm_data_start = .;
        *(.data)
        *(".data.*")
        *(".kernel.*")
        *(.dtcm_data)
        *(".dtcm_data.*")
        __dtcm_data_end = .;
    } > DTCM AT> FLASH
    __dtcm_end = .;
    __dtcm_data_load_start = LOADADDR(.dtcm_data);

    .stm32_backup_sram (NOLOAD) :
    {
        *(.stm32_backup_sram)
        *(".stm32_backup_sram.*")
    } > BACKUP_SRAM

    .ARM.attributes 0 :
    {
        KEEP(*(.ARM.attributes))
        KEEP(*(.gnu.attributes))
    }

    .last_section :
    {
        KEEP(*(.last_section))
    } > FLASH

    /* C++ Initcall sections */
    preinitcall_table :
    {
        PROVIDE_HIDDEN(__preinitcall_start = .);
        KEEP(*(SORT_BY_NAME(.preinitcall.*)))
        PROVIDE_HIDDEN(__preinitcall_end = .);
    } > FLASH

    initcall_table :
    {
        PROVIDE_HIDDEN(__initcall_start = .);
        KEEP(*(SORT_BY_NAME(.initcall.*)))
        PROVIDE_HIDDEN(__initcall_end = .);
    } > FLASH
    _flash_used = LOADADDR(.last_section) + SIZEOF(.last_section) - __rom_region_start;

    bss (NOLOAD) : ALIGN_WITH_INPUT
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.gnu.linkonce.sw_isr_table*)
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
    } >DTCM
}
