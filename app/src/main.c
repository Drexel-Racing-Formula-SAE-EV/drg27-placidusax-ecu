#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>

#include <ecu_core/ecu_core_contract.h>

#include "ecu_fatal.h"

#include <ecu_platform/fail_low.h>

/*
 * Authority backstop.
 *
 * The Kconfig locks are the policy; these assertions make a mis-set
 * configuration a build failure rather than a runtime discovery. They are
 * duplicated at each output writer as those stages land.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_ECU_TORQUE_AUTHORITY),
             "torque authority must remain disabled during the migration");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ECU_INVERTER_ENABLE_AUTHORITY),
             "inverter enable authority must remain disabled during the migration");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ECU_FIRMWARE_OK_AUTHORITY),
             "Firmware_Ok authority must remain disabled during the migration");

int main(void)
{
    int ret;

    printk("\nDRG27 Placidusax ECU - Zephyr migration stage E-003a\n");
    printk("Oracle: %s\n", ECU_CORE_ORACLE_REVISION);
    printk("Board:  %s\n", CONFIG_BOARD_TARGET);

    ret = ecu_core_contract_check();

    if (ret != 0) {
        printk("ECU portable core contract FAILED: %d\n", ret);
        k_panic();
    }

    printk("ECU portable core: contract PASS\n");

    /*
     * Frozen DER26 clock contract, asserted from Devicetree rather than
     * trusted. v2.10.7 SystemClock_Config(): HSE 8 MHz crystal, PLL
     * M=4 N=216 P=2 -> 216 MHz SYSCLK, AHB /1, APB1 /4, APB2 /2.
     */
    printk("SYSCLK: %u Hz\n",
           (unsigned int)DT_PROP(DT_NODELABEL(rcc), clock_frequency));

    printk("Torque authority:          DISABLED\n");
    printk("Inverter enable authority: DISABLED\n");
    printk("Firmware_Ok authority:     DISABLED\n");
    if (!ecu_safe_outputs_confirmed()) {
        /*
         * The PRE_KERNEL_1 board primitive did not run, so the outputs were
         * never proven safe. Treat that as fatal rather than continuing with
         * an unknown physical state.
         */
        printk("ECU safe outputs NOT confirmed\n");
        k_panic();
    }

    /* Named by schematic net: MCU Breakout rev1 assigns no function to the
     * generic nets, so neither does this banner. */
    printk("Safe outputs LOW: Firmware_Ok(PA7) MTR_EN(PF10) Buzzer(PF13) "
           "MISC_IO4(PA5) GP_OUT6(PB8)\n");
    printk("Safe-output drift: reasserts=%u last_mismatch=0x%02x\n",
           (unsigned int)ecu_safe_outputs_reassert_count(),
           (unsigned int)ecu_safe_outputs_last_mismatch());

    for (;;) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
