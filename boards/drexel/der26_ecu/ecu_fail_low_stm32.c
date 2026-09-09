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

#define ECU_PIN_CASCADIA_ON   5U   /* PA5  */
#define ECU_PIN_FIRMWARE_OK   7U   /* PA7  */
#define ECU_PIN_MTR_EN        10U  /* PF10 */
#define ECU_PIN_BUZZER        13U  /* PF13 */
#define ECU_PIN_PUMP_GATE     8U   /* PB8  */

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
             "Firmware_Ok must remain PA7");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, cascadia_on_gpios),
                          DT_NODELABEL(gpioa)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, cascadia_on_gpios) ==
                 ECU_PIN_CASCADIA_ON,
             "Cascadia_ON must remain PA5");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, inverter_enable_gpios),
                          DT_NODELABEL(gpiof)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, inverter_enable_gpios) ==
                 ECU_PIN_MTR_EN,
             "MTR_EN must remain PF10");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, buzzer_gpios),
                          DT_NODELABEL(gpiof)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, buzzer_gpios) == ECU_PIN_BUZZER,
             "Buzzer must remain PF13");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(ECU_SAFETY_NODE, pump_gate_gpios),
                          DT_NODELABEL(gpiob)) &&
             DT_GPIO_PIN(ECU_SAFETY_NODE, pump_gate_gpios) ==
                 ECU_PIN_PUMP_GATE,
             "coolant pump S gate must remain PB8");

/*
 * Every safety output is active-high at the pin, so LOW is the safe level for
 * all five. The pump gate's INVERSION is downstream of the pin, in the
 * low-side switch: see fail_low.h. Encoding it as GPIO_ACTIVE_LOW here would
 * make a future gpio_pin_set_dt(..., 0) drive the pin HIGH and hold the pump
 * off, which is the opposite of the required fallback.
 */
BUILD_ASSERT(DT_GPIO_FLAGS(ECU_SAFETY_NODE, firmware_ok_gpios) ==
                 GPIO_ACTIVE_HIGH &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, cascadia_on_gpios) ==
                 GPIO_ACTIVE_HIGH &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, inverter_enable_gpios) ==
                 GPIO_ACTIVE_HIGH &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, buzzer_gpios) == GPIO_ACTIVE_HIGH &&
             DT_GPIO_FLAGS(ECU_SAFETY_NODE, pump_gate_gpios) ==
                 GPIO_ACTIVE_HIGH,
             "all DER26 ECU safety outputs must remain active-high at the pin");

void ecu_force_safe_outputs_direct(void)
{
    const uint32_t porta_reset =
        ECU_BSRR_RESET(ECU_PIN_FIRMWARE_OK) |
        ECU_BSRR_RESET(ECU_PIN_CASCADIA_ON);
    const uint32_t portf_reset =
        ECU_BSRR_RESET(ECU_PIN_MTR_EN) |
        ECU_BSRR_RESET(ECU_PIN_BUZZER);
    const uint32_t portb_reset = ECU_BSRR_RESET(ECU_PIN_PUMP_GATE);

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
     * DELIBERATE DEVIATION from the v2.10.7 write order.
     *
     * The oracle writes MODER before clearing OTYPER. Clearing OTYPER first
     * removes a transient window in which a pin left open-drain by a previous
     * configuration would become an output before being forced push-pull. The
     * write set and the final register state are unchanged; only the order is
     * tightened, so this cannot alter steady-state behavior.
     */
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT_5 | GPIO_OTYPER_OT_7);
    GPIOA->MODER = (GPIOA->MODER &
                    ~(ECU_MODE_MASK(ECU_PIN_CASCADIA_ON) |
                      ECU_MODE_MASK(ECU_PIN_FIRMWARE_OK))) |
                   ECU_MODE_OUT(ECU_PIN_CASCADIA_ON) |
                   ECU_MODE_OUT(ECU_PIN_FIRMWARE_OK);

    GPIOF->OTYPER &= ~(GPIO_OTYPER_OT_10 | GPIO_OTYPER_OT_13);
    GPIOF->MODER = (GPIOF->MODER &
                    ~(ECU_MODE_MASK(ECU_PIN_MTR_EN) |
                      ECU_MODE_MASK(ECU_PIN_BUZZER))) |
                   ECU_MODE_OUT(ECU_PIN_MTR_EN) |
                   ECU_MODE_OUT(ECU_PIN_BUZZER);

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
    GPIOB->OTYPER &= ~GPIO_OTYPER_OT_8;
    GPIOB->MODER = (GPIOB->MODER & ~ECU_MODE_MASK(ECU_PIN_PUMP_GATE)) |
                   ECU_MODE_OUT(ECU_PIN_PUMP_GATE);
    GPIOB->BSRR = portb_reset;

    /*
     * Ensure the writes have retired before returning. The fatal path may
     * halt or reset the core immediately afterwards.
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
