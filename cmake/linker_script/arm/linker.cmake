# ARM Linker script generator
# This file generates linker scripts for ARM Cortex-M architectures

set(COMMON_ZEPHYR_LINKER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake/linker_script/common)

# Set alignment based on MPU requirements
if(DEFINED CONFIG_CUSTOM_SECTION_MIN_ALIGN_SIZE)
    set_ifndef(region_min_align ${CONFIG_CUSTOM_SECTION_MIN_ALIGN_SIZE})
endif()

if(DEFINED CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE)
    set_ifndef(region_min_align ${CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE})
endif()

# Default 4-byte alignment
set_ifndef(region_min_align 4)

# Calculate memory regions
math(EXPR FLASH_ADDR
    "${CONFIG_FLASH_BASE_ADDRESS} + ${CONFIG_FLASH_LOAD_OFFSET} + 0"
    OUTPUT_FORMAT HEXADECIMAL
)

if(CONFIG_FLASH_LOAD_SIZE GREATER 0)
    math(EXPR FLASH_SIZE
        "(${CONFIG_FLASH_LOAD_SIZE} + 0) - (${CONFIG_ROM_END_OFFSET} + 0)"
        OUTPUT_FORMAT HEXADECIMAL
    )
else()
    math(EXPR FLASH_SIZE
        "(${CONFIG_FLASH_SIZE} + 0) * 1024 - (${CONFIG_FLASH_LOAD_OFFSET} + 0) - (${CONFIG_ROM_END_OFFSET} + 0)"
        OUTPUT_FORMAT HEXADECIMAL
    )
endif()

set(RAM_ADDR ${CONFIG_SRAM_BASE_ADDRESS})
math(EXPR RAM_SIZE "(${CONFIG_SRAM_SIZE} + 0) * 1024" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR IDT_ADDR "${RAM_ADDR} + ${RAM_SIZE}" OUTPUT_FORMAT HEXADECIMAL)

# Define linker script content
set(LINKER_SCRIPT_CONTENT
"/* Generated linker script for ARM Cortex-M */
/* DO NOT EDIT - This file is auto-generated */

#include <zephyr/linker/sections.h>
#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/linker/linker-tool.h>

MEMORY
{
    FLASH (rx) : ORIGIN = ${FLASH_ADDR}, LENGTH = ${FLASH_SIZE}
    RAM   (wx) : ORIGIN = ${RAM_ADDR},   LENGTH = ${RAM_SIZE}
    IDT_LIST (wx) : ORIGIN = ${IDT_ADDR}, LENGTH = 2K
}

ENTRY(CONFIG_KERNEL_ENTRY)

SECTIONS
{
#include <zephyr/linker/rel-sections.ld>

    GROUP_START(ROMABLE_REGION)

    __rom_region_start = ${FLASH_ADDR};

    SECTION_PROLOGUE(rom_start,,)
    {
#include <snippets-rom-start.ld>
    } GROUP_LINK_IN(FLASH)

    SECTION_PROLOGUE(_TEXT_SECTION_NAME,,)
    {
        __text_region_start = .;
        *(.text)
        *(\".text.*\")
        *(\".TEXT.*\")
        *(.gnu.linkonce.t.*)
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
        . = ALIGN(4);
    } GROUP_LINK_IN(FLASH)

    __text_region_end = .;

    SECTION_PROLOGUE(.ARM.extab,,)
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } GROUP_LINK_IN(FLASH)

    SECTION_PROLOGUE(.ARM.exidx,,)
    {
        __exidx_start = .;
        *(.ARM.exidx* gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } GROUP_LINK_IN(FLASH)

    __rodata_region_start = .;

#include <zephyr/linker/common-rom.ld>
#include <snippets-rom-sections.ld>
#include <zephyr/linker/thread-local-storage.ld>

    SECTION_PROLOGUE(_RODATA_SECTION_NAME,,)
    {
        *(.rodata)
        *(\".rodata.*\")
        *(.gnu.linkonce.r.*)
#include <snippets-rodata.ld>
        . = ALIGN(4);
    } GROUP_LINK_IN(FLASH)

    __rodata_region_end = .;
    __rom_region_end = __rom_region_start + . - ADDR(rom_start);
    __rom_region_size = __rom_region_end - __rom_region_start;

    GROUP_END(ROMABLE_REGION)

    GROUP_START(RAMABLE_REGION)

    . = ${RAM_ADDR};
    . = ALIGN(${region_min_align});
    _image_ram_start = .;

#include <snippets-ram-sections.ld>

    GROUP_START(DATA_REGION)

    SECTION_DATA_PROLOGUE(_DATA_SECTION_NAME,,)
    {
        __data_region_start = .;
        __data_start = .;
        *(.data)
        *(\".data.*\")
        *(\".kernel.*\")
#include <snippets-rwdata.ld>
        __data_end = .;
    } GROUP_DATA_LINK_IN(RAM, FLASH)

    __data_size = __data_end - __data_start;
    __data_load_start = LOADADDR(_DATA_SECTION_NAME);
    __data_region_load_start = LOADADDR(_DATA_SECTION_NAME);

#include <zephyr/linker/common-ram.ld>

    __data_region_end = .;

#include <snippets-sections.ld>
#include <zephyr/linker/debug-sections.ld>

    /DISCARD/ : { *(.note.GNU-stack) }

    SECTION_PROLOGUE(.ARM.attributes, 0,)
    {
        KEEP(*(.ARM.attributes))
        KEEP(*(.gnu.attributes))
    }

    SECTION_DATA_PROLOGUE(_BSS_SECTION_NAME,(NOLOAD),)
    {
        . = ALIGN(4);
        __bss_start = .;
        __kernel_ram_start = .;
        *(.bss)
        *(\".bss.*\")
        *(COMMON)
        *(\".kernel_bss.*\")
        __bss_end = ALIGN(4);
    } GROUP_DATA_LINK_IN(RAM, RAM)

#include <zephyr/linker/common-noinit.ld>

    __kernel_ram_end = ${RAM_ADDR} + ${RAM_SIZE};
    __kernel_ram_size = __kernel_ram_end - __kernel_ram_start;

#include <zephyr/linker/ram-end.ld>

    GROUP_END(RAMABLE_REGION)
}
")

# Write the linker script
file(WRITE ${PROJECT_BINARY_DIR}/linker.ld ${LINKER_SCRIPT_CONTENT})

message(STATUS "Generated linker script: ${PROJECT_BINARY_DIR}/linker.ld")
