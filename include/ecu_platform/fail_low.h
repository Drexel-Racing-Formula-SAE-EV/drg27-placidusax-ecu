#ifndef ECU_PLATFORM_FAIL_LOW_H_
#define ECU_PLATFORM_FAIL_LOW_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Unconditional board-level safe-output primitive.
 *
 * Drives every output the ECU owns on MCU Breakout rev1 to a low push-pull
 * level. Signals are named by SCHEMATIC NET, because the breakout is shared
 * between the AMS and the ECU and assigns no function to the generic nets:
 *
 *   PA7  Firmware_Ok -> LOW   J801.12, schematic-named
 *   PF10 MTR_EN      -> LOW   J801.6,  schematic-named
 *   PF13 Buzzer      -> LOW   J801.14, schematic-named
 *   PA5  MISC_IO4    -> LOW   J801.10, generic net
 *   PB8  GP_OUT6     -> LOW   J801.29, generic net
 *
 * The v2.10.7 oracle calls PA5 "Cascadia_ON" and PB8 "CoolPump". Those are
 * facts about the oracle, not about the board: the schematic carries no such
 * names, and the function is assigned downstream on the backplane and
 * mezzanines. This primitive reproduces the oracle's low-drive set exactly
 * while declining to repeat its naming.
 *
 * UNVERIFIED: that LOW is the correct safe state for GP_OUT6 and MISC_IO4.
 * On STM32 reset every GPIO goes analog/floating, so if a load's safe state is
 * energized, only an external pull delivers it and no software convention can.
 * Resolving this needs ECU Backplane rev2 and MCU Misc rev3.1.
 *
 * This API deliberately has no scheduler, heap, logging, mutex or ordinary
 * Zephyr GPIO-driver dependency. The board implementation is permitted to use
 * direct MCU registers so pre-kernel startup failures and fatal error handling
 * can still drive the outputs safe.
 *
 * It is idempotent and safe to call from any context, including from a fault
 * handler with interrupts masked and before the kernel exists.
 */
void ecu_force_safe_outputs_direct(void);

/*
 * Returns a bitmask of owned outputs that are NOT currently a push-pull output
 * latched low. Zero means every output is in its safe state. Read-only.
 */
uint32_t ecu_safe_outputs_check_mismatch(void);

/*
 * Check, and re-assert only if something has drifted. Instrumented rather than
 * silent: a nonzero reassert count means some initialization between the
 * PRE_KERNEL_1 primitive and this call reconfigured a shared port, which is a
 * real ordering bug worth finding. If this never fires by end of season, the
 * guard can be deleted.
 */
void ecu_safe_outputs_verify_and_restore(void);

uint32_t ecu_safe_outputs_reassert_count(void);
uint32_t ecu_safe_outputs_last_mismatch(void);

#ifdef __cplusplus
}
#endif

#endif /* ECU_PLATFORM_FAIL_LOW_H_ */
