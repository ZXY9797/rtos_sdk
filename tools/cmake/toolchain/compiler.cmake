include(extensions)

# 包含内核定义
dt_arch_type(arch_core 0)
include(${arch_core})

# 编译选项
set(CFCOMMON "${MCPU_FLAGS} ${VFP_FLAGS} ${SYSTEM_PATH} --specs=nano.specs -specs=rdimon.specs --specs=nosys.specs -Wall -fmessage-length=0 -ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS   "-Os  ${CFCOMMON}")
set(CMAKE_CXX_FLAGS "-Os  ${CFCOMMON} -fno-exceptions")
set(CMAKE_ASM_FLAGS "${MCPU_FLAGS} ${VFP_FLAGS} -x assembler-with-cpp")

# 获得板子名
dt_get_board_name(board_name)
set(LINKER_SCRIPT ${BSP_DIR}/linkscript/${board_name}.ld)
message(STATUS "Link script: ${LINKER_SCRIPT}")

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections,--print-memory-usage"
)

# 搜索规则
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
