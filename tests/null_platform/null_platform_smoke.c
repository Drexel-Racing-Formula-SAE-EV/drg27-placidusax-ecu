/*
 * Null-platform smoke test.
 *
 * This executable links ONLY lib/ecu_core. If it compiles and runs, the
 * portable core genuinely has no Zephyr, FreeRTOS, CMSIS, STM32 HAL, board or
 * ecu_platform dependency -- which source review alone cannot establish.
 */

#include <ecu_core/ecu_core_contract.h>

#include <stdio.h>

int main(void)
{
    int ret = ecu_core_contract_check();

    if (ret != 0) {
        printf("FAIL: ecu_core_contract_check() = %d\n", ret);
        return 1;
    }

    printf("PASS: ecu_core contract on null platform (oracle %s)\n",
           ECU_CORE_ORACLE_REVISION);
    return 0;
}
