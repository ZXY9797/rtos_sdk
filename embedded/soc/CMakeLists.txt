set(MODULE_NAME soc)
add_library(${MODULE_NAME} ${NULL_C})

set(SOC_YML_FILE "${CMAKE_CURRENT_SOURCE_DIR}/soc.yml")
set(SOC_PARSER_SCRIPT "${TOOLS_DIR}/scripts/soc/generate_soc_cmake.py")
set(SOC_INFO_FILE "${CMAKE_CURRENT_BINARY_DIR}/soc_info.cmake")

dt_get_soc_name(SOC_COMPATIBLE)
message(STATUS "SOC_COMPATIBLE: ${SOC_COMPATIBLE}")

execute_process(
    COMMAND ${PYTHON_EXECUTABLE}
            "${SOC_PARSER_SCRIPT}"
            "${SOC_COMPATIBLE}"
            "${SOC_YML_FILE}"
            "${SOC_INFO_FILE}"
            "--fuzzy"
    ERROR_VARIABLE SOC_GEN_ERROR
    RESULT_VARIABLE gen_result
)

# 检查生成结果
if(gen_result EQUAL 0)
    # 包含生成的CMake文件
    if(EXISTS "${SOC_INFO_FILE}")
        include("${SOC_INFO_FILE}")
        message(STATUS "SOC information loaded from: ${SOC_INFO_FILE}")
    else()
        message(FATAL_ERROR "Generated SOC info file not found: ${SOC_INFO_FILE}")
    endif()
else()
    message(FATAL_ERROR "Failed to generate SOC info: ${SOC_GEN_ERROR}")
endif()

set(SOC_VENDER_DIR ${CMAKE_CURRENT_LIST_DIR}/${SOC_VENDOR})
set(SOC_SERIES_DIR ${CMAKE_CURRENT_LIST_DIR}/${SOC_VENDOR}/${SOC_SERIES})

target_compile_definitions(soc
    PUBLIC
        ${STM32CUBE_CPU}
        USE_HAL_DRIVER
        USE_FULL_LL_DRIVER
        # SOC_FAMILY_${SOC_FAMILY_UPPER}
        # SOC_SERIES_${SOC_SERIES_UPPER}
        # SOC_${SOC_NAME_UPPER}
)

add_subdirectory(${SOC_VENDOR})

target_sources(${MODULE_NAME}
    PRIVATE
        soc.c
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
)
