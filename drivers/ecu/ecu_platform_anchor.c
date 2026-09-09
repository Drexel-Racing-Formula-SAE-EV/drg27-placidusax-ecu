/*
 * E-001 anchor translation unit.
 *
 * zephyr_library_named() requires at least one source before any real adapter
 * exists.  This file exists only so the ecu_platform library target is created
 * from the very first migration stage, which is what makes the app/CMakeLists
 * FATAL_ERROR guard a real structural invariant rather than a later addition.
 *
 * It is replaced by genuine adapters from E-007 onward and must never acquire
 * product behavior of its own.
 */

#include <zephyr/kernel.h>

int ecu_platform_anchor(void);

int ecu_platform_anchor(void)
{
    return 0;
}
