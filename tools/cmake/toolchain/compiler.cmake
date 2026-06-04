include(extensions)

# 包含内核定义
dt_arch_type(arch_core 0)
include(${arch_core})

# 编译选项
set(CFCOMMON "${MCPU_FLAGS} ${VFP_FLAGS} ${SYSTEM_PATH} --specs=nano.specs -specs=rdimon.specs --specs=nosys.specs -Wall -fmessage-length=0 -ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS   "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H}")
set(CMAKE_CXX_FLAGS "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H} -fno-exceptions -fno-rtti -std=c++20")
set(CMAKE_ASM_FLAGS "${MCPU_FLAGS} ${VFP_FLAGS} -x assembler-with-cpp -imacros ${AUTOCONF_H}")

add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-D_ASMLANGUAGE>)

# 获得SOC名字
dt_get_soc_name(soc_name)
set(LINKER_SCRIPT ${BSP_DIR}/linkscript/${soc_name}.ld)
message(STATUS "Link script: ${LINKER_SCRIPT}")

set(CMAKE_C_LINK_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections,--print-memory-usage"
)
set(CMAKE_CXX_LINK_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections,--print-memory-usage"
)

# 搜索规则
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
