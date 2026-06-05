target_include_directories(soc
    PUBLIC
        .
        include
    PRIVATE
        ${BSP_DIR}/include
)

# SOC 源文件 - 使用寄存器操作，不依赖外设库
set(SOC_SOURCES
    soc.c
    src/system_gd32f50x.c
)

target_sources(soc
    PRIVATE
        ${SOC_SOURCES}
)

target_link_libraries(soc
    PUBLIC
        cmsis
    PRIVATE
        arch
)
