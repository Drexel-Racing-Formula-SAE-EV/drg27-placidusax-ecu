# Board-only emergency safety primitive. This is deliberately a separate Zephyr
# library from normal platform adapters so direct STM32 register access cannot
# spread into ordinary driver or application code.
#
# Zephyr loads this CMakeLists automatically for board der26_ecu. The
# application must never add this board directory manually: doing so would
# instantiate the safety target twice.

get_filename_component(
    DRG27_ECU_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)

zephyr_library_named(ecu_board_safety)

zephyr_library_sources(
    ecu_fail_low_stm32.c
)

zephyr_library_include_directories(
    ${DRG27_ECU_ROOT}/include
)
