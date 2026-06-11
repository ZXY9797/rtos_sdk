target_include_directories(soc
    PUBLIC
        .
        include
    PRIVATE
        ${BSP_DIR}/include
)

# Core GD32 SoC sources.
set(SOC_SOURCES
    soc.c
    src/system_gd32f50x.c
    src/gd32f50x_fmc.c
)

target_sources(soc
    PRIVATE
        ${SOC_SOURCES}
)

target_sources_ifdef(CONFIG_CAN_GD32 soc
    PRIVATE
        src/gd32f50x_can.c
        src/gd32f50x_dbg.c
        src/gd32f50x_rcu.c
)

target_link_libraries(soc
    PUBLIC
        cmsis
    PRIVATE
        arch
)
