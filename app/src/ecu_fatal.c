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
     * Spin with interrupts masked, as the oracle's stack-overflow and
     * malloc-failure hooks do. Two things about this are NOT transcription and
     * are recorded as decisions:
     *
     * 1. Blast radius. The oracle had two narrow hooks; Zephyr routes every
     *    fatal here - CPU faults, k_panic, k_oops, stack canary failures and
     *    __ASSERT. Zephyr could abort just the offending thread for a
     *    thread-scoped K_ERR_KERNEL_OOPS; spinning the system gives that up
     *    deliberately, because a vehicle controller running on after an
     *    unexplained fault is worse than one that stops.
     *
     * 2. What ends the spin. IWDG is independent of the core and keeps
     *    counting through irq_lock(), so once E-015 arms it this is
     *    reset-in-N-ms, not hang-forever. Until E-015 lands, IWDG is NOT
     *    armed and this really is dead-until-power-cycle.
     *
     * BLOCKING, tracked in the deviation register: whether a spinning ECU
     * still holds the shutdown loop closed. If this board's contribution is a
     * static GPIO level, dead firmware holds HV live; if it is a strobe or
     * charge pump that decays when the CPU stops toggling, it is safe by
     * construction. Needs Shutdown rev2. Driving Firmware_Ok low above is
     * necessary but is only sufficient in the static-level case.
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
     * Detect-then-fix rather than blind re-assert. If a PRE_KERNEL_1 device
     * initialization between the board primitive and here reconfigured a
     * shared GPIO port, the mismatch is counted and reported by main() instead
     * of being silently repaired. During bring-up that answers whether this
     * guard is needed at all: never fires by end of season -> delete it;
     * fires -> an init-ordering bug that would otherwise be invisible.
     */
    ecu_safe_outputs_verify_and_restore();
    safe_outputs_confirmed = true;
    return 0;
}

SYS_INIT(ecu_fatal_policy_init, PRE_KERNEL_2, 0);
