/*
 * Portable ECU core contract self-check.
 *
 * The core is host-native C.  It must not include Zephyr, FreeRTOS, CMSIS,
 * STM32 HAL, Devicetree or board headers.
 */

#ifndef DRG27_ECU_CORE_CONTRACT_H_
#define DRG27_ECU_CORE_CONTRACT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Oracle identity this core is being migrated against. */
#define ECU_CORE_ORACLE_REVISION "DER26-ECU-v2.10.7-SAFETY2-20260827"

/*
 * Returns 0 when the portable core's own representation assumptions hold.
 * A nonzero return is a fatal startup condition for the application.
 */
int ecu_core_contract_check(void);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_ECU_CORE_CONTRACT_H_ */
