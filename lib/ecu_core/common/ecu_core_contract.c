#include <ecu_core/ecu_core_contract.h>

#include <limits.h>
#include <stdint.h>

/*
 * The v2.10.7 oracle encodes torque in 0.1 Nm and pack current in 0.1 A across
 * fixed-width CAN payloads.  Those assumptions are checked here rather than
 * assumed, so a future toolchain or target change cannot silently reinterpret
 * a safety quantity.
 */
int ecu_core_contract_check(void)
{
    if (CHAR_BIT != 8) {
        return -1;
    }

    if (sizeof(int16_t) != 2u || sizeof(uint16_t) != 2u) {
        return -2;
    }

    if (sizeof(int32_t) != 4u || sizeof(uint32_t) != 4u) {
        return -3;
    }

    /* Signed right shift must be arithmetic: the torque paths rely on it. */
    if (((int32_t)-8 >> 1) != (int32_t)-4) {
        return -4;
    }

    /* Two's complement wrap behavior assumed by the rolling-counter logic. */
    if ((uint8_t)(0xFFu + 1u) != 0u) {
        return -5;
    }

    return 0;
}
