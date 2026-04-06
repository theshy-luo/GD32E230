cmake_minimum_required(VERSION 3.20)

project(gd32e23x_gcc_template C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GD32_FW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/GD32E23x_Firmware_Library_V2.5.0"
    CACHE PATH "Path to GD32E23x_Firmware_Library_V2.5.0")

if(NOT EXISTS "${GD32_FW_ROOT}/Firmware/CMSIS/GD/GD32E23x/Source/GCC/startup_gd32e23x.S")
    message(FATAL_ERROR "GD32 firmware library not found under GD32_FW_ROOT: ${GD32_FW_ROOT}")
endif()

set(GD32_CPU_FLAGS
    -mcpu=cortex-m23
    -mthumb
)

set(GD32_COMMON_DEFINES
    GD32E23x
    GD32E230
    USE_STDPERIPH_DRIVER
    HXTAL_VALUE=8000000U
    HXTAL_STARTUP_TIMEOUT=0x0500U
    IRC8M_VALUE=8000000U
    IRC28M_VALUE=28000000U
    IRC48M_VALUE=48000000U
    IRC40K_VALUE=40000U
)

set(GD32_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${GD32_FW_ROOT}/Firmware/CMSIS
    ${GD32_FW_ROOT}/Firmware/CMSIS/GD/GD32E23x/Include
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Include
    ${GD32_FW_ROOT}/Utilities
)

set(GD32_SOURCES
    app/main.c
    app/systick.c
    app/gd32e23x_it.c
    ${GD32_FW_ROOT}/Firmware/CMSIS/GD/GD32E23x/Source/system_gd32e23x.c
    ${GD32_FW_ROOT}/Firmware/CMSIS/GD/GD32E23x/Source/GCC/startup_gd32e23x.S
    ${GD32_FW_ROOT}/Firmware/CMSIS/GD/GD32E23x/Source/GCC/newlib/syscalls.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_adc.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_dma.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_gpio.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_rcu.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_timer.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_usart.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_misc.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_exti.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_fmc.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_syscfg.c
    ${GD32_FW_ROOT}/Firmware/GD32E23x_standard_peripheral/Source/gd32e23x_pmu.c
    ${GD32_FW_ROOT}/Utilities/gd32e230c_eval.c
)

add_executable(${PROJECT_NAME}.elf ${GD32_SOURCES})

target_include_directories(${PROJECT_NAME}.elf PRIVATE ${GD32_INCLUDES})
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${GD32_COMMON_DEFINES})

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    ${GD32_CPU_FLAGS}
    -Wall
    -Wextra
    -Wundef
    -ffunction-sections
    -fdata-sections
    -fno-common
)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    ${GD32_CPU_FLAGS}
    -T${GD32_FW_ROOT}/Firmware/CMSIS/GD/GD32E23x/Source/GCC/Ld/gd32e230x8_flash.ld
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map
    --specs=nano.specs
    --specs=nosys.specs
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${PROJECT_NAME}.elf>
)
