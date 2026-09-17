/*
 * DER26 ECU emergency safe-output primitive.
 *
 * This is the ONLY file in the repository permitted direct MCU register
 * access. It is board-owned rather than application-owned because the safety
 * outputs must be driven low before the kernel and driver stack exist, and
 * again after a fatal kernel error when those subsystems can no longer be
 * trusted.
 *
 * Ported from the v2.10.7 oracle's ecu_force_safe_outputs() in Core/Src/app.c.
 * The register write set and the final register state are identical. One
 * ordering change is made deliberately and is documented at the write site.
 */

#include <ecu_platform/fail_low.h>

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <soc.h>

#define ECU_SAFETY_NODE DT_NODELABEL(ecu_safety_io)

#define ECU_PIN_MISC_IO4      5U   /* PA5, net MISC_IO4 -> J801.10 */
#define ECU_PIN_FIRMWARE_OK   7U   /* PA7, net Firmware_Ok -> J801.12 */
#define ECU_PIN_MTR_EN        10U  /* PF10, net MTR_EN -> J801.6 */
#define ECU_PIN_BUZZER        13U  /* PF13, net Buzzer -> J801.14 */
#define ECU_PIN_GP_OUT6       8U   /* PB8, net GP_OUT6 -> J801.29 */

#define ECU_GPIO_MODE_WIDTH  2U
#define ECU_GPIO_MODE_OUTPUT 1U

/* Reset a pin through BSRR's upper half-word. Atomic; no read-modify-write. */
#define ECU_BSRR_RESET(pin) ((uint32_t)BIT((pin) + 16U))

#define ECU_MODE_MASK(pin)  ((uint32_t)0x3UL << ((pin) * ECU_GPIO_MODE_WIDTH))
#define ECU_MODE_OUT(pin) \
    ((uint32_t)ECU_GPIO_MODE_OUTPUT << ((pin) * ECU_GPIO_MODE_WIDTH))

/*
 * The primitive is tied to the frozen DER26 board and the STM32F767 register
 * model. Devicetree remains the single wiring contract: if the board is ever
 * re-pinned there, these assertions fail the build rather than letting this
 * file silently drive the wrong pins.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_BOARD_DER26_ECU),
             "DER26 fail-low primitive requires the DER26 ECU board");
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_STM32F767XX),
             "DER26 fail-low primitive requires STM32F767XX");
BUILD_ASSERT(DT_NODE_EXISTS(ECU_SAFETY_NODE),
             "typed ECU safety I/O node must exist");

BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, firmware_ok_gpios),
                          DT_NODELABEL(gpioa)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, firmware_ok_gpios) ==
                 ECU_PIN_FIRMWARE_OK,
             "Firmware_Ok must remain PA7 (schematic U201 pin 43)");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, misc_io4_gpios),
                          DT_NODELABEL(gpioa)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, misc_io4_gpios) == ECU_PIN_MISC_IO4,
             "MISC_IO4 must remain PA5 (schematic U201 pin 41)");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, mtr_en_gpios),
                          DT_NODELABEL(gpiof)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, mtr_en_gpios) == ECU_PIN_MTR_EN,
             "MTR_EN must remain PF10 (schematic U201 pin 22)");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, buzzer_gpios),
                          DT_NODELABEL(gpiof)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, buzzer_gpios) == ECU_PIN_BUZZER,
             "Buzzer must remain PF13 (schematic U201 pin 53)");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, gp_out6_gpios),
                          DT_NODELABEL(gpiob)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, gp_out6_gpios) == ECU_PIN_GP_OUT6,
             "GP_OUT6 must remain PB8 (schematic U201 pin 139)");

/*
 * Flags must stay zero on every net here.
 *
 * MCU Breakout rev1 is shared between the AMS and the ECU and names these
 * signals generically; what GP_OUT6 and MISC_IO4 actually drive, and whether
 * the path to them inverts, is assigned downstream on the backplane and
 * mezzanines. The board layer therefore holds NO polarity opinion: a zero
 * flags cell here means "no opinion", not "active high".
 *
 * TODO(E-009): the pump -> GP_OUT6 mapping and its GPIO_ACTIVE_* flag belong
 * in the ECU application layer, which is the layer that knows about a pump.
 * Blocked on ECU Backplane rev2 / MCU Misc rev3.1: whether the gate stage
 * inverts, and whether an external pull defines the state while the MCU is in
 * reset or hung. Do not build E-009 work on an assumed polarity.
 */
BUILD_ASSERT(DT_GPIO_FLAGS(ECU_SAFETY_NODE, firmware_ok_gpios) == 0 &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, misc_io4_gpios) == 0 &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, mtr_en_gpios) == 0 &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, buzzer_gpios) == 0 &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, gp_out6_gpios) == 0,
             "board layer must hold no polarity opinion on these nets");

/*
 * Mismatch bits reported by ecu_safe_outputs_check_mismatch().
 */
#define ECU_SAFE_MISMATCH_FIRMWARE_OK BIT(0)
#define ECU_SAFE_MISMATCH_MISC_IO4    BIT(1)
#define ECU_SAFE_MISMATCH_MTR_EN      BIT(2)
#define ECU_SAFE_MISMATCH_BUZZER      BIT(3)
#define ECU_SAFE_MISMATCH_GP_OUT6     BIT(4)

static uint32_t reassert_count;
static uint32_t last_mismatch;

/* True when `pin` on `port` is not a push-pull output latched low. */
static bool pin_not_safe(const GPIO_TypeDef *port, uint32_t pin)
{
    const uint32_t mode =
        (port->MODER >> (pin * ECU_GPIO_MODE_WIDTH)) & 0x3UL;

    if (mode != ECU_GPIO_MODE_OUTPUT) {
        return true;
    }
    if ((port->OTYPER & BIT(pin)) != 0UL) {
        return true;
    }
    return (port->ODR & BIT(pin)) != 0UL;
}

uint32_t ecu_safe_outputs_check_mismatch(void)
{
    uint32_t mismatch = 0U;

    if (pin_not_safe(GPIOA, ECU_PIN_FIRMWARE_OK)) {
        mismatch |= ECU_SAFE_MISMATCH_FIRMWARE_OK;
    }
    if (pin_not_safe(GPIOA, ECU_PIN_MISC_IO4)) {
        mismatch |= ECU_SAFE_MISMATCH_MISC_IO4;
    }
    if (pin_not_safe(GPIOF, ECU_PIN_MTR_EN)) {
        mismatch |= ECU_SAFE_MISMATCH_MTR_EN;
    }
    if (pin_not_safe(GPIOF, ECU_PIN_BUZZER)) {
        mismatch |= ECU_SAFE_MISMATCH_BUZZER;
    }
    if (pin_not_safe(GPIOB, ECU_PIN_GP_OUT6)) {
        mismatch |= ECU_SAFE_MISMATCH_GP_OUT6;
    }

    return mismatch;
}

uint32_t ecu_safe_outputs_reassert_count(void)
{
    return reassert_count;
}

uint32_t ecu_safe_outputs_last_mismatch(void)
{
    return last_mismatch;
}

void ecu_safe_outputs_verify_and_restore(void)
{
    const uint32_t mismatch = ecu_safe_outputs_check_mismatch();

    if (mismatch != 0U) {
        last_mismatch = mismatch;
        reassert_count++;
        ecu_force_safe_outputs_direct();
    }
}

void ecu_force_safe_outputs_direct(void)
{
    const uint32_t porta_reset =
        ECU_BSRR_RESET(ECU_PIN_FIRMWARE_OK) |
        ECU_BSRR_RESET(ECU_PIN_MISC_IO4);
    const uint32_t portf_reset =
        ECU_BSRR_RESET(ECU_PIN_MTR_EN) |
        ECU_BSRR_RESET(ECU_PIN_BUZZER);
    const uint32_t portb_reset = ECU_BSRR_RESET(ECU_PIN_GP_OUT6);

    /*
     * Do not assume any GPIO initialization has run. Clock every port that
     * carries a safety output first; the readback provides the peripheral
     * clock-enable delay before the first access to those ports.
     */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOFEN;
    (void)RCC->AHB1ENR;

    /* Preload the output data latches low while the pins are still inputs. */
    GPIOA->BSRR = porta_reset;
    GPIOF->BSRR = portf_reset;

    /*
     * Oracle write order: MODER, then OTYPER. An earlier revision of this port
     * reordered these, on the theory that clearing OTYPER first narrows a
     * transient window for a pin left open-drain by a previous configuration.
     * That was reverted: OTYPER resets to all-zeros (push-pull) and a soft
     * reset resets the GPIO registers, so the early write is a no-op, and the
     * only inherently open-drain bus on MCU Breakout rev1 is I2C2 on PF0/PF1 -
     * a port this primitive does not touch. During a migration whose claim is
     * mechanical equivalence, a line-for-line diff against the oracle is worth
     * more than an optimization for a case that does not occur.
     */
    GPIOA->MODER = (GPIOA->MODER &
                    ~(ECU_MODE_MASK(ECU_PIN_MISC_IO4) |
                      ECU_MODE_MASK(ECU_PIN_FIRMWARE_OK))) |
                   ECU_MODE_OUT(ECU_PIN_MISC_IO4) |
                   ECU_MODE_OUT(ECU_PIN_FIRMWARE_OK);
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT_5 | GPIO_OTYPER_OT_7);

    GPIOF->MODER = (GPIOF->MODER &
                    ~(ECU_MODE_MASK(ECU_PIN_MTR_EN) |
                      ECU_MODE_MASK(ECU_PIN_BUZZER))) |
                   ECU_MODE_OUT(ECU_PIN_MTR_EN) |
                   ECU_MODE_OUT(ECU_PIN_BUZZER);
    GPIOF->OTYPER &= ~(GPIO_OTYPER_OT_10 | GPIO_OTYPER_OT_13);

    /*
     * Second BSRR write, as in the oracle: defense in depth against a
     * partially initialized peripheral state where the first latch write did
     * not take effect.
     */
    GPIOA->BSRR = porta_reset;
    GPIOF->BSRR = portf_reset;

    /*
     * Coolant pump. Disabling the TIM4 CH3 output compare stops the timer
     * driving PB8 before the pin is reclaimed as a GPIO, so no partial-speed
     * waveform can survive this call. Guarded because TIM4 may not be clocked
     * during an early startup failure.
     */
    if ((RCC->APB1ENR & RCC_APB1ENR_TIM4EN) != 0U) {
        TIM4->CCER &= ~TIM_CCER_CC3E;
    }

    GPIOB->BSRR = portb_reset;
    GPIOB->MODER = (GPIOB->MODER & ~ECU_MODE_MASK(ECU_PIN_GP_OUT6)) |
                   ECU_MODE_OUT(ECU_PIN_GP_OUT6);
    GPIOB->OTYPER &= ~GPIO_OTYPER_OT_8;
    GPIOB->BSRR = portb_reset;

    /*
     * Cortex-M7 store buffer: a peripheral write can still be in flight when
     * the core stops. DSB forces it out before this function returns.
     *
     * This guards the paths that do NOT already carry a barrier: the
     * irq_lock()-and-spin fatal path, and a core halted by the debugger. It is
     * redundant when the caller ends in NVIC_SystemReset(), because CMSIS
     * wraps that write in its own __DSB(); harmless, but not what it is for.
     */
    __DSB();
    __ISB();
}

/*
 * Earliest application-controlled board safety action.
 *
 * Registration lives beside the primitive so application orchestration never
 * owns the physical initialization mechanism. PRE_KERNEL_1 priority 0 places
 * this ahead of ordinary device initialization.
 */
static int ecu_safe_outputs_early_init(void)
{
    ecu_force_safe_outputs_direct();
    return 0;
}

SYS_INIT(ecu_safe_outputs_early_init, PRE_KERNEL_1, 0);
