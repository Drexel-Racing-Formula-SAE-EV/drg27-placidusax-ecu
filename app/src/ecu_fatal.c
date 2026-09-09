/*
 * Application-owned fatal policy.
 *
 * The app owns the ORDERING of fatal actions; the board owns the physical
 * mechanism. This file must therefore never touch MCU registers directly --
 * it calls the board primitive through the narrow platform interface.
 */

#include "ecu_fatal.h"

#include <ecu_platform/fail_low.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/fatal.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/*
 * Authority backstop at the fatal path itself. A build that somehow enabled an
 * authority lock must not reach a fatal handler that assumes outputs are safe.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_ECU_TORQUE_AUTHORITY),
             "torque authority must remain disabled during the migration");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ECU_INVERTER_ENABLE_AUTHORITY),
             "inverter enable authority must remain disabled during the migration");

/* Set once the PRE_KERNEL_1 primitive has been observed to have run. */
static bool safe_outputs_confirmed;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    ARG_UNUSED(esf);

    /*
     * Outputs first, diagnostics second. Console output can block, recurse or
     * fault; nothing may run ahead of driving the vehicle outputs safe.
     */
    ecu_force_safe_outputs_direct();

    printk("\n*** ECU FATAL %u - safe outputs forced ***\n", reason);

    /*
     * Re-assert after the console write. Anything reached during printk may
     * have touched a shared peripheral, and this primitive is idempotent.
     */
    ecu_force_safe_outputs_direct();

    /*
     * The v2.10.7 oracle's stack-overflow and malloc-failure hooks force safe
     * outputs, disable interrupts and spin forever rather than continuing.
     * Preserve that: no thread is abandoned into an unknown state, and the
     * watchdog is left to produce the reset once E-015 arms it.
     */
    (void)irq_lock();

    for (;;) {
        /* Deliberate: hold outputs low until the watchdog or a manual reset. */
    }
}

bool ecu_safe_outputs_confirmed(void)
{
    return safe_outputs_confirmed;
}

/*
 * Runs after the board primitive's PRE_KERNEL_1 registration. Its only job is
 * to record that the earlier stage was reached, so main() can report the fact
 * rather than assume it.
 */
static int ecu_fatal_policy_init(void)
{
    /*
     * Idempotent re-assertion. If any PRE_KERNEL_1 device initialization
     * between the board primitive and here reconfigured a shared GPIO port,
     * this restores the safe level before the kernel starts threads.
     */
    ecu_force_safe_outputs_direct();
    safe_outputs_confirmed = true;
    return 0;
}

SYS_INIT(ecu_fatal_policy_init, PRE_KERNEL_2, 0);
