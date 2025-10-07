/* Program Entry, set to mark it as "used" and avoid gc */
ENTRY(Reset_Handler)

_region_min_align = 32;

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
    __rom_region_start = (0x8000000 + 0x0);

    rom_start :
    {
        . = ALIGN(4);
        _vector_start = .;
        KEEP(*(.isr_vector))
        _vector_end = .;
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
    } > FLASH
    __text_region_end = .;

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

    _siiram_code = LOADADDR (.ITCM);
    .ITCM :
    {
        . = ALIGN(4);
        _siram_code = .;
        *(.text.atan2f)
        *(.text.sqrt)
        *(.text.tanf)
        *(.text.pow)
        *(.text.lwrb*)
        *(.text.ddw*)
        *(.text.link*)
        *(.text.*memcpy*)
        *(.text.*memset*)
        *(.ITCM)
        *(.ITCM.*)
        . = ALIGN(4);
        _eiram_code = .;
    } > ITCM AT>FLASH

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
        *(.data)
        *(".data.*")
        *(".kernel.*")
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
