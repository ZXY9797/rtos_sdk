set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# 工具链设置
set(CMAKE_C_COMPILER    arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER  arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER  arm-none-eabi-gcc)
set(CMAKE_AR            arm-none-eabi-ar)
set(CMAKE_OBJCOPY       arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP       arm-none-eabi-objdump)
set(CMAKE_SIZE          arm-none-eabi-size)

set(CMAKE_OUTPUT_SUFFIX ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_ASM     ${CMAKE_OUTPUT_SUFFIX})
set(CMAKE_EXECUTABLE_SUFFIX_C       ${CMAKE_OUTPUT_SUFFIX})
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ${CMAKE_OUTPUT_SUFFIX})
