# E-003 closeout — fail-low safe-output foundation

Stage: **E-003**, per `docs/migration/E000_MIGRATION_PLAN.md`.
Analogue of the AMS Z-003 BMS_OK fail-low foundation.

Oracle: `DER26-ECU-v2.10.7-SAFETY2-20260827`, `ecu_force_safe_outputs()` in
`Core/Src/app.c`.

## Why this lands before any actuator adapter

Every later stage adds something that can drive a pin. Until a primitive exists
that unconditionally returns the safety outputs to their safe level — before
the kernel exists, and after a fatal error when the driver stack cannot be
trusted — each of those additions would be a way to leave the vehicle in an
unknown physical state. The AMS sequenced this the same way at Z-003.

## The five outputs

| Signal | Pin | Safe level | Meaning of the safe level |
| --- | --- | --- | --- |
| `Firmware_Ok` | PA7 | LOW | Drops the shutdown-circuit participant |
| `Cascadia_ON` | PA5 | LOW | Removes CM200 power sequencing |
| `MTR_EN` | PF10 | LOW | Removes CM200 hardware enable |
| `Buzzer` | PF13 | LOW | Silent |
| Pump S gate | PB8 | LOW | **Releases S — pump goes to full-speed fallback** |

The PB8 inversion is load-bearing and deliberately preserved. PB8 drives an
inverting low-side switch on the coolant pump's S input, so gate-low releases S
and invokes the pump's no-valid-PWM fallback. A fatal fault therefore cannot
leave a partial-speed TIM4 waveform running. Encoding that inversion as
`GPIO_ACTIVE_LOW` in Devicetree would flip the safe level, so the binding
forbids it and both checkers assert against it.

## What landed

- `include/ecu_platform/fail_low.h` — the narrow platform interface.
- `boards/drexel/der26_ecu/ecu_fail_low_stm32.c` — the primitive, registered
  with `SYS_INIT` at `PRE_KERNEL_1` priority 0. **The only file in the
  repository permitted direct MCU register access.**
- `boards/drexel/der26_ecu/CMakeLists.txt` — `zephyr_library_named(ecu_board_safety)`,
  a separate library from ordinary adapters so register access cannot spread.
- `dts/bindings/ecu/drexel,ecu-safety-io.yaml` and the `ecu_safety_io` node —
  Devicetree is the single wiring contract; the primitive asserts every pin
  against it with `BUILD_ASSERT`, so re-pinning the board fails the build
  instead of silently driving the wrong pins.
- `app/src/ecu_fatal.c` — application-owned fatal *ordering*: outputs safe,
  then diagnostics, then re-assert, then `irq_lock()` and spin. The app never
  touches registers; it calls the board primitive through the interface.
- A `PRE_KERNEL_2` re-assertion, so a device initialization between the board
  primitive and kernel start cannot leave a shared GPIO port reconfigured.
- `scripts/check_fail_low_contract.py` and `scripts/check_all_contracts.py`.

## Deliberate deviation from the oracle

**One**, documented at the write site: the oracle writes `MODER` before
clearing `OTYPER`; this port clears `OTYPER` first. The write set and the final
register state are identical — only the transient window in which a pin left
open-drain by a previous configuration could become an output is removed. This
cannot alter steady-state behavior.

Everything else is preserved exactly: the clock-enable-and-readback, the
BSRR preload before `MODER`, the second BSRR re-assert after it, the guarded
`TIM4->CCER` CH3 release before PB8 is reclaimed, and the port ordering
A/F then B. `__DSB()`/`__ISB()` are added so writes retire before a fatal path
halts or resets the core.

## Evidence

Devicetree revalidated clean under Zephyr v4.4.0 `gen_edt.py` with
`--edtlib-Werror`. The `ecu_safety_io` node binds to the custom binding and all
five pins resolve to PA7, PA5, PF10, PF13, PB8 with active-high flags.

`check_all_contracts.py` passes all three gates.

### Mutation results — 14/14 caught

`check_board_contract.py`:

| Mutation | Result |
| --- | --- |
| `Firmware_Ok` PA7 &rarr; PA6 | caught |
| Pump gate `ACTIVE_HIGH` &rarr; `ACTIVE_LOW` | caught |
| `MTR_EN` PF10 &rarr; PA10 | caught |
| `ecu_safety_io` disabled | caught |

`check_fail_low_contract.py`:

| Mutation | Result |
| --- | --- |
| GPIOA/F second BSRR re-assert removed | caught |
| GPIOB post-MODER BSRR removed | caught |
| GPIOA preload BSRR removed | caught |
| GPIOB (pump) never dereferenced | caught |
| TIM4 CH3 release removed | caught |
| `PRE_KERNEL_1` &rarr; `POST_KERNEL` | caught |
| Pump pin 8 &rarr; 9 | caught |
| Primitive takes a mutex | caught |
| `printk` before safe outputs in fatal handler | caught |
| Fatal handler no longer forces outputs | caught |

### Two checker defects found by mutation testing, and fixed

Worth recording, because both would have left a safety gate that passed while
the property it claimed to protect was broken:

1. **Substring collision.** The port-coverage check tested `if "GPIOB" not in
   body`, which the clock-enable macro `RCC_AHB1ENR_GPIOBEN` satisfies. The
   pump gate could have gone undriven with the gate still green. Now tests for
   a real `GPIOB->BSRR` / `GPIOB->MODER` dereference.
2. **Global instead of per-port.** The BSRR-brackets-MODER check searched the
   whole function, so a later BSRR write on a *different* port satisfied it.
   Removing the GPIOA/GPIOF re-assert escaped detection. Now checked per port.

## What is explicitly NOT claimed

- **`ECU_CAP_FAIL_LOW_PHYSICALLY_VALIDATED` remains `n`.** Nothing has been
  scoped. The plan's E-003 gate requires scope verification that all five
  outputs are low before kernel init and again after a forced fatal error;
  that is open.
- **No target build.** Still no Zephyr SDK or ARM toolchain in this
  environment, so the primitive has never been compiled. The CMSIS register
  macros it uses were each confirmed present in this Zephyr's
  `stm32f767xx.h`, but that is not a compile.
- **No authority.** All three locks remain disabled, now backstopped by
  `BUILD_ASSERT` in both `main.c` and the fatal path.
- The `irq_lock()`-and-spin fatal behavior matches the oracle's hooks, but the
  watchdog that would turn that spin into a reset does not arrive until E-015.

## Next

**E-004**, the static thread topology: 11 threads at the exact v2.10.7
priorities and periods, with heartbeat accounting.

## Reproducing

```text
python3 scripts/check_all_contracts.py . --edt-pickle <edt.pickle>
```

See `E002_CLOSEOUT.md` for how to produce the EDT pickle without a target
toolchain.
