# E-003a — response to the migration handoff

Implements the five decisions in `ZEPHYR_MIGRATION_HANDOFF.md` §1 and applies
the hardware ground truth in §2. Also opens the deviation register the handoff's
working agreements call for.

Companion finding: `docs/hardware/PINMAP_RECONCILIATION.md`.

## §1.1 `OTYPER` before `MODER` — reverted

Reverted to oracle order (MODER, then OTYPER) on all three ports. The reasoning
is recorded at the write site: `OTYPER` resets to push-pull, a soft reset resets
the GPIO registers, and the only inherently open-drain bus on this board is I2C2
on PF0/PF1 — a port the primitive never touches. A line-for-line diff against
the oracle is worth more than an optimization for a case that does not occur.

`check_fail_low_contract.py` now **enforces** the oracle order per port, so
re-introducing the reorder requires a recorded deviation rather than a quiet
edit. Verified: the reorder is caught.

## §1.2 PB8 polarity — restructured, flag deferred

The board layer no longer holds a polarity opinion, and no longer repeats the
oracle's functional names for nets the schematic leaves generic.

`drexel,ecu-safety-io` is now named by **schematic net**:

| Property | Pin | Net | Source |
| --- | --- | --- | --- |
| `firmware-ok-gpios` | PA7 | `Firmware_Ok` | schematic-named |
| `mtr-en-gpios` | PF10 | `MTR_EN` | schematic-named |
| `buzzer-gpios` | PF13 | `Buzzer` | schematic-named |
| `misc-io4-gpios` | PA5 | `MISC_IO4` | generic |
| `gp-out6-gpios` | PB8 | `GP_OUT6` | generic |

Every flags cell is `0`, and both the binding and a `BUILD_ASSERT` state that
this means **"no polarity opinion"**, not "active high". `check_board_contract.py`
rejects any `GPIO_ACTIVE_*` flag appearing at board level. Verified by mutation.

`TODO(E-009)` is recorded at the assert: the `pump` → `GP_OUT6` mapping and its
flag belong in the ECU application layer. The handoff's recommendation —
`GPIO_ACTIVE_LOW` plus `GPIO_OUTPUT_ACTIVE`, wrapped in a `pump_set(bool)` with
raw `gpio_pin_set_dt` off-limits — is captured there but deliberately **not
implemented**, pending the two blocking facts.

What did **not** change: the primitive still drives PB8 low and still releases
TIM4 CH3 first. Dropping either would be a deviation from the oracle on no
evidence. The uncertainty is now stated in `fail_low.h` rather than the previous
confident claim about an inverting gate driver.

## §1.3 Fatal handler — kept, decisions recorded, blocking item opened

Handler retained. The comment now records both non-transcription decisions: the
widened blast radius (Zephyr routes every fatal here, and the option to abort
only a thread-scoped `K_ERR_KERNEL_OOPS` is given up deliberately), and what
ends the spin — IWDG keeps counting through `irq_lock()`, so this becomes
reset-in-N-ms once E-015 arms it, and is dead-until-power-cycle before then.

Of the three items to resolve:

1. **`CONFIG_ASSERT` in release** — currently `y` in `app/prj.conf`. Acceptable
   at E-003 because no release build exists. Recorded in the register; must be
   `n`, or the enabled paths audited, before any vehicle-authority image.
2. **IWDG** — not armed until E-015; stated in the comment.
3. **Shutdown circuit with a hung CPU** — **BLOCKING, unresolved.** Needs
   Shutdown rev2. Driving `Firmware_Ok` low is necessary but only sufficient if
   the ECU's contribution is a static level; if it is a strobe or charge pump,
   safety is by construction, and if it is static, a spinning ECU holds HV live.

## §1.4 `PRE_KERNEL_2` re-assertion — now detect-then-fix

Changed from a silent re-assert to instrumented detection.
`ecu_safe_outputs_check_mismatch()` reads `MODER`/`OTYPER`/`ODR` for the five
owned pins and returns a bitmask of any that are not a push-pull output latched
low. `ecu_safe_outputs_verify_and_restore()` re-asserts **only** on mismatch,
incrementing a counter and latching the mask.

`main()` reports both. Never fires by end of season → delete the guard. Fires →
a real init-ordering bug that would otherwise have been invisible.

## §1.5 `__DSB()` / `__ISB()` — kept, comment clarified

The comment now names which case the barrier guards: the `irq_lock()`-and-spin
path and a debugger-halted core. It notes explicitly that the barrier is
redundant when the caller ends in `NVIC_SystemReset()`, since CMSIS wraps that
write in its own `__DSB()`.

## §2 hardware facts applied

Recorded in the devicetree at the nodes they affect:

- **CAN** — SN65HVD230QDR with `RS → 10k → GND` is slope-control mode, a
  hardware cap on bitrate independent of bxCAN config; `R502 = 120R` is fitted,
  so this board terminates the bus. Both noted at `&can1`, with the instruction
  not to propose a bitrate change as part of this migration.
- **MPU6050** — 0x69 with INT unconnected, noted at `&i2c2`. The oracle already
  uses `MPU6050_ADDR1` (0x69), so no defect.
- **PA0 conflict** — noted at `&timers5`, blocking E-010.
- **PB3 is SWO**, **BOOT0 externally selectable** — no code depends on either
  yet; captured in the reconciliation document.

## Deviation register

Every intentional difference from the v2.10.7 oracle. This list is the review
surface.

| # | Deviation | Status | Justification |
| --- | --- | --- | --- |
| D1 | `__DSB()`/`__ISB()` at the end of the primitive | accepted (§1.5) | Cortex-M7 store buffer; oracle omitted it because CubeMX does |
| D2 | `PRE_KERNEL_2` verify-and-restore, which the oracle has no analogue for | accepted (§1.4) | Guards driver init between the primitive and kernel start; instrumented so it can be deleted if it never fires |
| D3 | Devicetree names nets, not functions; no board-level polarity | accepted (§1.2) | Schematic carries no functional name for these nets; polarity has one home, in the application layer |
| D4 | Single `k_sys_fatal_error_handler` covering cases the oracle's two hooks did not | accepted (§1.3) | Zephyr routes all fatals here; stopping the vehicle controller is the intended behavior |
| D5 | `OTYPER` before `MODER` | **reverted** (§1.1) | No open-drain net on the affected ports; equivalence is worth more |

## Still blocking — do not guess

Carried from handoff §3, with the two added by the reconciliation:

1. What `GP_OUT6` drives and whether the path inverts — blocks E-009.
2. External pulls on any `GP_OUT`/`GP_IN` net — determines behavior while the
   MCU is in reset or hung; no software convention substitutes.
3. Which nets are open-drain downstream — would reopen D5.
4. Safe state per signal — design intent, must come from the team.
5. Shutdown circuit behavior with a hung CPU — blocks §1.3 sign-off.
6. **PE13 `BSPD_OK` vs `BSPD_Fail`** — possible inverted safety input.
7. **PE4 direction** and **PA0 analog-vs-capture** — block E-014 and E-010.

Needed: ECU Backplane rev2 and MCU Misc rev3.1 close 1–3; Shutdown rev2 and
BSPD rev4.1 close 5 and 6.

## Evidence

Devicetree revalidates clean under `gen_edt.py --edtlib-Werror`.
`check_all_contracts.py` green across all three gates.

Mutation results, 8/8 caught:

| Gate | Mutation | Result |
| --- | --- | --- |
| board | `GP_OUT6` given `GPIO_ACTIVE_LOW` | caught |
| board | `MISC_IO4` moved PA5 → PA4 | caught |
| board | `Firmware_Ok` moved PA7 → PA6 | caught |
| board | `gp-out6-gpios` removed | caught (binding rejects it) |
| fail-low | `OTYPER` before `MODER` returns | caught |
| fail-low | `GP_OUT6` dropped from the driven set | caught |
| fail-low | TIM4 CH3 release removed | caught |
| fail-low | `PRE_KERNEL_1` → `POST_KERNEL` | caught |

An earlier run of this suite reported four board mutations as caught when the
harness had in fact dropped the repository's own `--vendor-prefixes` file, so
the devicetree was being rejected for an unrelated reason. The table above is
the corrected run, with a baseline-validates-first assertion in the harness.

Still no target build: no Zephyr SDK or ARM toolchain in this environment.
