# Portable ECU core.
#
# This file contains no Zephyr CMake helpers on purpose: it is reused verbatim
# by the null-platform host gate, which builds this library with no Zephyr,
# FreeRTOS, CMSIS, STM32 HAL, board tree or ecu_platform include path
# available.  If a future core file needs any of those to compile, that gate
# must fail.

add_library(ecu_core STATIC
    common/ecu_core_contract.c
)

target_include_directories(ecu_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
