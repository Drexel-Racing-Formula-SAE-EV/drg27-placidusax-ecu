# MCU Breakout rev1 — Pin Map

**Source:** `Schematic_PDF__No_Variations_.pdf` (9 sheets, MCU Breakout rev1), derived from
the embedded netlist annotation rather than read off the drawing. Pin numbers are
STM32F767ZIT6 LQFP144 package pins as drawn on `Microcontroller.SchDoc` (U201).

**Scope warning.** This board is shared between AMS and ECU and uses deliberately generic
net names (`GP_OUTn`, `GP_INn`, `MISC_IOn`, `ADC3(n)`). Semantic meaning — which output is
the pump gate, which input is a shutdown-loop tap — is assigned downstream on ECU Backplane
rev2 and the mezzanines, and is **not** in this document. Do not infer function from a
`GP_OUT` name.

**Columns not yet filled.** `Inversion`, `External pull`, and `Safe state` require the
backplane + mezzanine schematics and design intent. They are intentionally blank rather
than guessed. See "Open items".

---

## STM32F767ZIT6 (U201) pin assignments

| Port | Pin | Net | Leaves board at |
|---|---|---|---|
| PA0 | 34 | `ADC3(6)` | J601.25 |
| PA1 | 35 | `ADC3(7)` | J601.27 |
| PA3 | 37 | `ADC1` | J601.2 |
| PA4 | 40 | `MISC_IO3` | J801.8 |
| PA5 | 41 | `MISC_IO4` | J801.10 |
| PA7 | 43 | `Firmware_Ok` | J801.12 |
| PA13 | 105 | `TMS_SWD` | J901 (SWD header) |
| PA14 | 109 | `TCK_SWD` | J901 (SWD header) |
| PB1 | 47 | `MISC_IO2` | J601.14 |
| PB2 | 48 | `MTR_Fault` | J601.16 |
| PB3 | 133 | `SWO_SWD` | J901 (SWD header) |
| **PB8** | **139** | **`GP_OUT6`** | **J801.29** |
| PB9 | 140 | `GP_OUT5` | J801.27 |
| PC0 | 26 | `ADC2` | J601.6 |
| PC3 | 29 | `ADC3(1)` | J601.4 |
| PC4 | 44 | `MISC_IO1` | J601.10 |
| PC5 | 45 | `TSAL_HV_SIG` | J601.12 |
| PD0 | 114 | `CAN_RX` | U501 transceiver (on-board) |
| PD1 | 115 | `CAN_TX` | U501 transceiver (on-board) |
| PD8 | 77 | `USART3_TX` | U701 FT231XQ (on-board USB) |
| PD9 | 78 | `USART3_RX` | U701 FT231XQ (on-board USB) |
| PD14 | 85 | `Brake_Light` | J801.20 |
| PD15 | 86 | `GP_OUT2` | J801.22 |
| PE0 | 141 | `GP_OUT4` | J801.25 |
| PE2 | 1 | `GP_OUT3` | J801.23 |
| PE4 | 3 | `GP_OUT1` | J801.21 |
| PE7 | 58 | `GP_IN4` | J601.30 |
| PE13 | 66 | `BSPD_Fail` | J601.29 |
| PF0 | 10 | `I2C2_SDA` | J801.9 + U401 MPU6050 |
| PF1 | 11 | `I2C2_SCL` | J801.11 + U401 MPU6050 |
| PF3 | 13 | `ADC3(2)` | J601.8 |
| PF4 | 14 | `ADC3(4)` | J601.21 |
| PF5 | 15 | `ADC3(3)` | J601.19 |
| PF6 | 18 | `UART7_RX` | J801.2 |
| PF7 | 19 | `UART7_TX` | J801.4 |
| PF8 | 20 | `PWRGD` | U301 regulator (on-board) |
| PF9 | 21 | `ADC3(5)` | J601.23 |
| PF10 | 22 | `MTR_EN` | J801.6 |
| PF11 | 49 | `GP_IN1` | J601.18 |
| PF12 | 50 | `MTR_Ok` | J601.20 |
| PF13 | 53 | `Buzzer` | J801.14 |
| PF14 | 54 | `IMD_Fail` | J601.22 |
| PF15 | 55 | `BMS_Fail` | J601.24 |
| PG0 | 56 | `GP_IN2` | J601.26 |
| PG1 | 57 | `GP_IN3` | J601.28 |
| PG8 | 93 | `SPI6_NSS` | J801.24 |
| PG12 | 127 | `SPI6_MISO` | J801.30 |
| PG13 | 128 | `SPI6_SCK` | J801.28 |
| PG14 | 129 | `SPI6_MOSI` | J801.26 |
| BOOT0 | 138 | `BOOT0` | J601.17, via R202 10k |
| NRST | 25 | `NRST` | J901.6, R203 10k pull-up + C220 100nF |

51 signals assigned. Every other GPIO on the package is unrouted on this board.

---

## Connectors

Both mezzanine connectors are **Molex 524653071**, 30-pin.

### J601 — Inputs (`Inputs.SchDoc`)
Odd pins 1/3/5/7/9/13 are GND. Pin 11 `AVDD`, pin 15 `+3.3V_OUT`.
ESD protection: D601 `RCLAMP0504S.TCT` on ADC1, ADC2, ADC3(1), ADC3(2).

| 2 ADC1 | 4 ADC3(1) | 6 ADC2 | 8 ADC3(2) | 10 MISC_IO1 | 12 TSAL_HV_SIG |
| 14 MISC_IO2 | 16 MTR_Fault | 17 BOOT0 | 18 GP_IN1 | 19 ADC3(3) | 20 MTR_Ok |
| 21 ADC3(4) | 22 IMD_Fail | 23 ADC3(5) | 24 BMS_Fail | 25 ADC3(6) | 26 GP_IN2 |
| 27 ADC3(7) | 28 GP_IN3 | 29 BSPD_Fail | 30 GP_IN4 |

### J801 — Outputs (`Outputs.SchDoc`)
Pins 1/7/15/19 GND, 13 `+VBAT`, 17 `+3.3V`. CAN_H/CAN_L exit here (pins 3/5).
ESD: D801 and D803 `RCLAMP0504S.TCT`; D802 `PESD2CANFD24V-TR` on the CAN pair.

| 2 UART7_RX | 3 CAN_H | 4 UART7_TX | 5 CAN_L | 6 MTR_EN | 8 MISC_IO3 |
| 9 I2C2_SDA | 10 MISC_IO4 | 11 I2C2_SCL | 12 Firmware_Ok | 14 Buzzer | 16 USART_TX |
| 18 USART_RX | 20 Brake_Light | 21 GP_OUT1 | 22 GP_OUT2 | 23 GP_OUT3 | 24 SPI6_NSS |
| 25 GP_OUT4 | 26 SPI6_MOSI | 27 GP_OUT5 | 28 SPI6_SCK | **29 GP_OUT6** | 30 SPI6_MISO |

---

## On-board facts that affect firmware

**MPU6050 (U401) I²C address is 0x69, not 0x68.** AD0 (pin 9) sits on the same net as
VLOGIC (8), VDD (13), and the R401/R402 pull-ups — i.e. tied to +3.3V. Bus is I²C2
(PF0/PF1), pull-ups 10k each. INT (pin 12) is not connected — no interrupt-driven reads.

**CAN transceiver is SN65HVD230QDR (U501) in slope-control mode.** RS (pin 8) goes to GND
through R501 = 10k, not a direct short. Slope control limits edge rate and therefore
caps usable bitrate. Check the datasheet slew-rate curve before committing to a bus speed.

**R502 = 120R is fitted on-board.** This board terminates the CAN bus. It must be at a
physical end of the bus, and there must be exactly one other terminator elsewhere.

**PB3 is SWO.** Not available as a general-purpose GPIO while SWO trace is in use.

**BOOT0 is externally selectable** via J601.17 and J201 (2-pin B2B-XH). R201 is DNP,
R202 = 10k. Confirm the idle level before assuming normal-boot on every reset.

**3.3V rail:** MCP1726T-3302E (U301), 5V in via D301, PWRGD → PF8.
**USB-serial:** FT231XQ (U701) on USART3 (PD8/PD9), separate from the UART7 pair on J801.

---

## Flags to verify

These come from single-pin nets in the netlist annotation, which usually means "not
connected on this board" but can also mean a drawing artifact. Worth a look before relying
on them:

- J801.16 `USART_TX` and J801.18 `USART_RX` — appear unconnected. Note these are labelled
  `USART_*` without a peripheral number and are distinct from PD8/PD9 USART3, which go to
  the FT231XQ instead.
- J801.13 `+VBAT` — appears unconnected.
- J601.11 `AVDD` — appears unconnected, though the drawing shows an AVDD symbol.

---

## Open items

The following cannot be answered from this board and need ECU Backplane rev2 + the
mezzanine schematics exported the same way (File » Export » PDF, "No Variations"):

1. **What `GP_OUT6` (PB8) actually drives**, and whether the path to it inverts. This is the
   open question blocking the devicetree polarity decision.
2. **External pulls** on each `GP_OUT`/`GP_IN` net downstream of J601/J801 — these determine
   pin behaviour when the MCU is in reset or hung, which no software convention can override.
3. **Open-drain / wired-OR nets**, which determine whether GPIO init register ordering
   (`OTYPER` before `MODER`) matters at all.
4. **Safe state per signal** — what each load should do when the ECU stops executing. Not
   derivable from any schematic; this is design intent and has to be written down by the
   team.
