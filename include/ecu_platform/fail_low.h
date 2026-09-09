#ifndef ECU_PLATFORM_FAIL_LOW_H_
#define ECU_PLATFORM_FAIL_LOW_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Unconditional board-level safe-output primitive.
 *
 * Forces every DER26 ECU safety output to its safe physical level:
 *
 *   PA7  Firmware_Ok   -> LOW  (drops the shutdown-circuit participant)
 *   PA5  Cascadia_ON   -> LOW  (removes CM200 power sequencing)
 *   PF10 MTR_EN        -> LOW  (removes CM200 hardware enable)
 *   PF13 Buzzer        -> LOW
 *   PB8  pump S gate   -> LOW  (releases S; see the inversion note below)
 *
 * PB8 drives an INVERTING low-side switch on the coolant pump's S input.
 * Gate-low RELEASES S and deliberately invokes the pump's no-valid-PWM
 * full-speed fallback, so a fatal fault cannot leave a partial-speed TIM4
 * waveform running. This inversion is a property of the DER26 hardware and
 * must never be "corrected".
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

#ifdef __cplusplus
}
#endif

#endif /* ECU_PLATFORM_FAIL_LOW_H_ */
