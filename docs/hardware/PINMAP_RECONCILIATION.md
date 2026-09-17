# Pin map reconciliation — oracle firmware vs MCU Breakout rev1 schematic

Compares `DER26-ECU/docs/PIN_MAP.md` (what the v2.10.7 firmware believes) against
`mcu_breakout_rev1_pinmap.md` (what the schematic netlist says). Produced
mechanically, then each difference checked against the oracle's actual code.

Per the handoff's working agreements: **where the two disagree the schematic
wins, and the disagreement is a finding.** Hardware facts cannot be inferred
from firmware — the oracle assigning a meaning to a pin is evidence about the
oracle, not about the board.

37 pins in the oracle map, 48 assigned on the schematic. **24 agree exactly.**

## Blocking conflicts

### 1. PE13 — `BSPD_OK` vs `BSPD_Fail`: possible inverted safety input

The schematic net is **`BSPD_Fail`** (U201 pin 66, J601.29). The oracle calls
it `BSPD_OK` and interprets it as active-high-OK:

```c
/* Core/Src/ext_drivers/ecu_safety.c */
bool ecu_bspd_raw_is_fault(bool raw_pin_high)
{
#if ECU_BSPD_OK_ACTIVE_HIGH          /* defined as 1 */
    return !raw_pin_high;            /* HIGH = OK, LOW = fault */
#endif
}
```

If the net name reflects the downstream sense, the oracle's interpretation is
**exactly inverted**: a BSPD fault would read as OK and the ECU would permit
torque during a brake-plausibility fault.

Mitigating evidence, which is why this is a question and not yet a defect: the
oracle's `ecu_config.h` documents a deliberate 2026-08-25 hardware change
adding "a protected fail-low 3.3 V interface before PE13", i.e. it believes the
conditioning inverts a fail-low signal into active-high-OK. The conditioning is
not on the breakout, so the schematic cannot confirm or deny it. The net name
may simply predate that change.

**Resolve before any torque-authority work.** Needs BSPD rev4.1 and the
backplane. Cannot be settled from anything in either repository.

### 2. PE4 — input in firmware, output net on the board

Oracle: `RTD_Go`, an active-low button **input** with an internal pull-up, read
with `HAL_GPIO_ReadPin` in `rtd_task.c`.
Schematic: **`GP_OUT1`**, U201 pin 3, on **J801 — the Outputs connector**, pin 21.

A direction conflict, not just a naming one. Note the AMS uses PE4 on this same
shared board as an ADBMS chip select, which is consistent with it being an
output. Either the ECU mezzanine repurposes J801.21 as an input, or the oracle's
pin assignment is wrong.

**Resolve before E-014** (RTD state machine).

### 3. PA0 — timer capture in firmware, analog net on the board

Oracle: `CoolFlow`, TIM5 CH1/CH2 input capture
(`flow_sensor_init(..., htim5, TIM5, TIM_CHANNEL_2, TIM_CHANNEL_1)`).
Schematic: **`ADC3(6)`**, U201 pin 34, J601.25 — the analog input group.

PA0 is electrically capable of both TIM5_CH1 and ADC3_IN0, so this is a question
of what the mezzanine presents, not of pin capability. If the flow sensor
arrives as an analog level rather than a frequency, the entire E-010 capture
approach is wrong.

**Resolve before E-010.** Also affects PA1 (`ADC3(7)`), which the oracle does
not use at all.

## Generic nets the oracle names but the schematic does not

These are not contradictions. MCU Breakout rev1 is shared between the AMS and
the ECU and deliberately uses generic names; function is assigned downstream.
Listed because firmware must not treat the oracle's name as a hardware fact.

| Pin | Oracle name | Schematic net | Affects |
| --- | --- | --- | --- |
| PA5 | `Cascadia_ON` | `MISC_IO4` (J801.10) | E-003 safe-output set |
| PB8 | `CoolPump` | `GP_OUT6` (J801.29) | E-003, E-009 polarity |
| PB1 | `SSA_LED` | `MISC_IO2` (J601.14) | E-009 |
| PA3 | `APPS1` | `ADC1` (J601.2) | E-008 |
| PC0 | `APPS2` | `ADC2` (J601.6) | E-008 |
| PC3 | `BSE1` | `ADC3(1)` (J601.4) | E-008 |
| PF3 | `BSE2` | `ADC3(2)` (J601.8) | E-008 |
| PF5 | `CoolTemp1` | `ADC3(3)` (J601.19) | E-008 |
| PF4 | `CoolTemp2` | `ADC3(4)` (J601.21) | E-008 |
| PF9 | `CoolPress` | `ADC3(5)` (J601.23) | E-008 |

The ADC group ordering is internally consistent — `ADC3(1)`..`ADC3(7)` are all
valid ADC3 channels — so the analog assignments are plausible, just unconfirmed
as to which sensor lands on which.

## Confirmed agreements worth recording

24 pins match exactly, including every signal this stage drives that the
schematic names: **PA7 `Firmware_Ok`, PF10 `MTR_EN`, PF13 `Buzzer`**, plus
`MTR_Fault` PB2, `MTR_Ok` PF12, `IMD_Fail` PF14, `BMS_Fail` PF15,
`TSAL_HV_SIG` PC5, `Brake_Light` PD14, CAN PD0/PD1, USART3 PD8/PD9,
UART7 PF6/PF7, I2C2 PF0/PF1, SPI6 PG8/PG12/PG13/PG14, SWD PA13/PA14/PB3.

**The MPU6050 address is already correct in the oracle.** The handoff warns
that assuming 0x68 would fail to enumerate; `board.c` passes `MPU6050_ADDR1`,
which `mpu6050.h` defines as `0x69`. Matches the schematic's AD0-to-3V3. No
change needed.

## Pins on the board the oracle never uses

`MISC_IO1` PC4, `MISC_IO3` PA4, `GP_OUT2` PD15, `GP_OUT3` PE2, `GP_OUT4` PE0,
`GP_OUT5` PB9, `GP_IN1` PF11, `GP_IN2` PG0, `GP_IN3` PG1, `GP_IN4` PE7,
`ADC3(7)` PA1, `PWRGD` PF8.

`PWRGD` (PF8, regulator power-good) is unmonitored by the oracle and may be
worth adding as a diagnostic. `GP_OUT4` is PE0, which the AMS drives as
`BMS_OK` — consistent with the shared-board design.
