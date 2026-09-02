cmake_minimum_required(VERSION 3.10)

# 项目名称
project(ControllerUnit)

file(GLOB SRCS
    ${PROJECT_SOURCE_DIR}/app/*.cpp
    ${PROJECT_SOURCE_DIR}/app/gui/*.cpp
    ${PROJECT_SOURCE_DIR}/app/env/*.cpp
    ${PROJECT_SOURCE_DIR}/app/systimer/*.cpp
    ${PROJECT_SOURCE_DIR}/app/ventilate_service/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/core/*.c
    ${PROJECT_SOURCE_DIR}/system/drivers/adc/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/motor/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/console/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/console/*.c
    ${PROJECT_SOURCE_DIR}/system/drivers/lte/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/aht2415/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/twi/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/tm1750/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/rain_sensor/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/ntc/*.cpp
    ${PROJECT_SOURCE_DIR}/system/drivers/config/*.cpp
    ${PROJECT_SOURCE_DIR}/system/libs/fwlib/*.c
)

include_directories(
    ${PROJECT_SOURCE_DIR}/app
    ${PROJECT_SOURCE_DIR}/app/gui
    ${PROJECT_SOURCE_DIR}/app/env
    ${PROJECT_SOURCE_DIR}/app/systimer
    ${PROJECT_SOURCE_DIR}/app/ventilate_service
    ${PROJECT_SOURCE_DIR}/system
    ${PROJECT_SOURCE_DIR}/system/drivers/core
    ${PROJECT_SOURCE_DIR}/system/drivers/adc
    ${PROJECT_SOURCE_DIR}/system/drivers/motor
    ${PROJECT_SOURCE_DIR}/system/drivers/console
    ${PROJECT_SOURCE_DIR}/system/drivers/lte
    ${PROJECT_SOURCE_DIR}/system/drivers/aht2415
    ${PROJECT_SOURCE_DIR}/system/drivers/twi
    ${PROJECT_SOURCE_DIR}/system/drivers/tm1750
    ${PROJECT_SOURCE_DIR}/system/drivers/rain_sensor
    ${PROJECT_SOURCE_DIR}/system/drivers/ntc
    ${PROJECT_SOURCE_DIR}/system/drivers/config
    ${PROJECT_SOURCE_DIR}/system/libs/fwlib
)

set(STARTUP_FILE ${PROJECT_SOURCE_DIR}/system/startup.s)

set(LINKER_SCRIPT ${PROJECT_SOURCE_DIR}/system/link.x)

set(LIBS_FLAG "-lc -lstdc++ -lnosys -lm")

enable_language(ASM)
# 设置交叉编译工具链
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(MCU_FLAGS "-mcpu=cortex-m0plus -mthumb")

set(COMPILE_MARCOS "-DPY32L090xC")

set(CMAKE_ASM_FLAGS "${COMPILE_MARCOS} ${MCU_FLAGS} -g -Og -Wall -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS "${COMPILE_MARCOS} ${MCU_FLAGS} -g -Og -Wall -fdata-sections -ffunction-sections")
set(CMAKE_CXX_FLAGS "${COMPILE_MARCOS} ${MCU_FLAGS} -g -Og  -Wall -fdata-sections -ffunction-sections")

set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T ${LINKER_SCRIPT} -g -Og -specs=nano.specs -specs=nosys.specs ${LIBS} -u _printf_float -Wl,--no-warn-rwx-segments -Wl,-Map=${PROJECT_NAME}.map,--cref -Wl,--gc-sections")

add_executable(${PROJECT_NAME}.elf ${STARTUP_FILE} ${SRCS})

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
COMMENT "Building binary file and hex file..."
COMMAND ${CMAKE_OBJCOPY} -Obinary ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.elf ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin
COMMAND ${CMAKE_OBJCOPY} -Oihex ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.elf ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex
COMMAND ${CMAKE_SIZE} --format=berkeley ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
COMMENT "Invoking: Cross ARM GNU Print Size"
)

