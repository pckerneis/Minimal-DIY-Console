# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(bidule-01-hardware-tester C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(bidule-01-hardware-tester main.c)

pico_set_program_name(bidule-01-hardware-tester "bidule-01-hardware-tester")
pico_set_program_version(bidule-01-hardware-tester "0.1")

pico_enable_stdio_uart(bidule-01-hardware-tester 0)
pico_enable_stdio_usb(bidule-01-hardware-tester 1)

target_link_libraries(bidule-01-hardware-tester
        pico_stdlib)

target_include_directories(bidule-01-hardware-tester PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(bidule-01-hardware-tester
        hardware_i2c
        )

if (PICO_CYW43_SUPPORTED)
    target_link_libraries(bidule-01-hardware-tester pico_cyw43_arch_none)
endif()

pico_add_extra_outputs(bidule-01-hardware-tester)

