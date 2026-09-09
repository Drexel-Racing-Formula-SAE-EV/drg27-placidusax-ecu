# Host-native project that builds lib/ecu_core with NO Zephyr, FreeRTOS,
# CMSIS, STM32 HAL, board tree or ecu_platform include path available.

cmake_minimum_required(VERSION 3.20)
project(ecu_core_null_platform C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Werror)

get_filename_component(
    ECU_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/ecu_core" ABSOLUTE
)

# The real production CMakeLists, reused verbatim.
add_subdirectory("${ECU_CORE_DIR}" "${CMAKE_BINARY_DIR}/ecu_core")

enable_testing()

add_executable(null_platform_smoke null_platform_smoke.c)
target_link_libraries(null_platform_smoke PRIVATE ecu_core)

add_test(NAME null_platform_smoke COMMAND null_platform_smoke)
