cmake_minimum_required(VERSION 3.10)
project(ADC_MOTOR_TEST)

set(MOTOR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../04.controller_unit_motor_freertos)

file(GLOB FWLIB_SRCS ${MOTOR_ROOT}/system/libs/fwlib/*.c)

set(SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/py32f071_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/py32f071_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/usart_board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/system_entry.c
    ${MOTOR_ROOT}/system/drivers/core/system_py32f071.c
    ${FWLIB_SRCS}
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${MOTOR_ROOT}/system/drivers/core
    ${MOTOR_ROOT}/system/libs/fwlib
)

set(STARTUP_FILE ${MOTOR_ROOT}/system/startup.s)
set(LINKER_SCRIPT ${MOTOR_ROOT}/system/link.x)

enable_language(ASM)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(MCU_FLAGS "-mcpu=cortex-m0plus -mthumb")
set(COMPILE_MARCOS "-DPY32F071xB")

set(CMAKE_ASM_FLAGS "${COMPILE_MARCOS} ${MCU_FLAGS} -g -Og -Wall -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS "${COMPILE_MARCOS} ${MCU_FLAGS} -g -Og -Wall -fdata-sections -ffunction-sections")
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T ${LINKER_SCRIPT} -g -Og -specs=nano.specs -specs=nosys.specs -u _printf_float -Wl,--no-warn-rwx-segments -Wl,-Map=${PROJECT_NAME}.map,--cref -Wl,--gc-sections")

add_executable(${PROJECT_NAME}.elf ${SRCS} ${STARTUP_FILE})
set_target_properties(${PROJECT_NAME}.elf PROPERTIES LINKER_LANGUAGE C)
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
)
