# ECU-specific Zephyr platform boundary. Application/task code consumes only
# the generic headers under include/ecu_platform; STM32/Zephyr driver details
# remain compiled in this dedicated Zephyr library.
#
# This file is reached through zephyr/CMakeLists.txt while Zephyr is in kernel
# CMake mode.  Do not add this directory from app/CMakeLists.txt.

get_filename_component(
    DRG27_ECU_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    ABSOLUTE
)

zephyr_library_named(ecu_platform)

zephyr_library_sources(
    ecu_platform_anchor.c
)

zephyr_library_include_directories(
    ${DRG27_ECU_ROOT}/include
    ${DRG27_ECU_ROOT}/lib/ecu_core/include
)
