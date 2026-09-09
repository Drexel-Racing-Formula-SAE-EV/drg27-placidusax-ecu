cmake_minimum_required(VERSION 3.20.0)

get_filename_component(DRG27_ECU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
list(APPEND BOARD_ROOT "${DRG27_ECU_ROOT}")
list(APPEND DTS_ROOT "${DRG27_ECU_ROOT}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drg27_placidusax_ecu)

target_sources(app PRIVATE
    src/main.c
)

target_include_directories(app PRIVATE
    "${DRG27_ECU_ROOT}/include"
)

# ---------------------------------------------------------------------------
# Portable ECU core
# ---------------------------------------------------------------------------
#
# Kept as ordinary CMake.  lib/ecu_core/CMakeLists.txt contains no Zephyr
# helpers and is reused verbatim by the null-platform host gate.  Platform
# integration points *toward* this core, never the reverse.
get_filename_component(
    ECU_CORE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib/ecu_core"
    ABSOLUTE
)

add_subdirectory(
    "${ECU_CORE_DIR}"
    "${CMAKE_BINARY_DIR}/ecu_core"
)

# drivers/ecu is intentionally NOT added here.  This repository is a Zephyr
# module and zephyr/CMakeLists.txt creates ecu_platform while Zephyr is still
# in kernel CMake mode.  Calling zephyr_library_named() from this application
# CMakeLists would happen too late (application mode).
if(NOT TARGET ecu_platform)
    message(FATAL_ERROR
        "ecu_platform Zephyr module library was not created; "
        "check zephyr/module.yml and zephyr/CMakeLists.txt")
endif()

# Platform adapters may consume portable ECU types/functions.  The portable
# core itself never receives a dependency back on the platform or Zephyr.
target_link_libraries(ecu_platform PRIVATE ecu_core)
target_link_libraries(app PRIVATE ecu_core)
