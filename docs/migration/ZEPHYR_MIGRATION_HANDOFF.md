# Zephyr Migration — Handoff

**For:** Claude Code sessions working the FreeRTOS→Zephyr ECU migration branch.
**Status:** answers to the five open review items, plus hardware ground truth that was not
available when those items were raised.

Read this before touching `boards/`, devicetree, or any GPIO init code.

---

## 0. Context you need

**Target:** STM32F767ZIT6 (LQFP144), on MCU Breakout rev1. Same board design is used on both
the AMS and the ECU — it is a generic breakout, not an ECU-specific board. This matters more
than it sounds; see §2.

**Bus:** single classic CAN bus at 500 kbps. bxCAN, classic CAN only, 1 Mbps ceiling.
Nodes are ECU, AMS, and the Cascadia CM200DX inverter. The Elcon charger is fixed at
250 kbps / J1939 / 29-bit and cannot share this bus — a charger gateway board is planned but
does not exist yet. Do not propose bitrate changes as part of this migration; changing it
requires reconfiguring every node in lockstep and validating the harness.

**Known gap, do not paper over:** the ECU currently broadcasts no vehicle state on CAN.
Throttle, brake, RTD, and fault flags exist only on a legacy UART and the SD card. There is
also no shared DBC between AMS and ECU — the CAN contract lives in a markdown document. The
migration should not invent CAN messages to fill this gap. If a Zephyr port needs a message
definition that doesn't exist, stop and say so.

**The oracle** is the existing C + FreeRTOS ECU firmware. Equivalence against it is the bar,
with the exceptions recorded below.

---

## 1. Decisions on the five review items

### 1.1 `OTYPER` before `MODER` — revert to oracle order

The reorder only buys a narrower transient if some pin in the port is configured open-drain.
`OTYPER` resets to all-zeros (push-pull), and a soft reset resets GPIO registers, so writing
`OTYPER = 0` early is a no-op.

On GPIOB the assigned pins are PB1 (`MISC_IO2`), PB2 (`MTR_Fault`), PB3 (`SWO_SWD`), PB8
(`GP_OUT6`), PB9 (`GP_OUT5`). The only inherently open-drain bus on this board is I²C2, and
that is PF0/PF1 — a different port. So there is no evidence GPIOB needs open-drain at all.

**Action:** revert to oracle order. During a migration whose whole claim is mechanical
equivalence, a clean line-for-line diff against the reference is worth more than an
optimization for a case that doesn't occur. If you later find `MISC_IO2` is open-drain
downstream, raise it then rather than pre-emptively.

### 1.2 PB8 polarity — restructure the question, then defer the flag

Hardware fact, newly confirmed from the schematic: **PB8 = `GP_OUT6` = J801 pin 29.** It is a
generic GPIO output handed to the backplane. Nothing on the MCU Breakout names a pump. The
inverting gate driver is downstream on ECU Backplane rev2 / MCU Misc rev3.1, neither of which
has been read yet.

That reframes the design:

- The board-level devicetree should describe **what the board provides**: `gp-out6-gpios` on
  `&gpiob 8`, no polarity opinion, because the breakout genuinely doesn't know.
- The **application/ECU overlay** owns the mapping `pump` → `gp_out6`, and that is the single
  place the `GPIO_ACTIVE_*` flag belongs, because that is the layer that knows about the pump.

This mirrors the hardware hierarchy and means the polarity question has exactly one home
instead of being an argument about naming.

**On the flag itself, when the downstream data arrives:** the recommendation is
`GPIO_ACTIVE_LOW` with `GPIO_OUTPUT_ACTIVE` in the configure flags, not the active-high
encoding currently on the branch. Rationale: `ACTIVE_LOW` makes `gpio_pin_set_dt(&pump, 1)`
mean *pump on*, which is what every future reader will assume. The active-high encoding
defends against a rare error-path mistake by guaranteeing confusion in the common path.
Pair it with a `pump_set(bool on)` wrapper and treat raw `gpio_pin_set_dt` on this pin as
off-limits outside that module — that defuses the whole class of error regardless of flag.

**Do not lock this in yet.** It is blocked on two facts that are not in the repo: whether the
gate stage actually inverts, and whether there is an external pull that defines the state when
the MCU is in reset or hung. The second matters more than the flag: on STM32 reset all GPIOs
go to analog/floating, so if the safe state is pump-on, only a hardware pull delivers it. No
software convention can.

**Action:** restructure into board-level + application-level as above. Leave a `TODO(E-009)`
at the polarity flag. Do not build further E-009 work on the current encoding.

### 1.3 Fatal handler — conditionally accept, needs two facts

`irq_lock()` + spin matches `vApplicationStackOverflowHook`, but the oracle had two narrow
hooks and Zephyr's `k_sys_fatal_error_handler` catches everything: CPU faults, `k_panic`,
`k_oops`, stack canary failures, and `__ASSERT`s. The blast radius is much wider.

Three things to resolve before this is signed off:

1. **`CONFIG_ASSERT` in the release build.** If it's `y`, an assert the oracle compiled out now
   bricks the ECU mid-run. Confirm it's `n` for release or audit the enabled paths.
2. **IWDG.** It's independent of the core and keeps counting through `irq_lock()`. If IWDG is
   enabled, spin-forever actually means reset-in-N-ms, which is probably correct — but say so
   in a comment, because it is not obvious from the code. If IWDG is not enabled, spin-forever
   means dead until power cycle.
3. **Shutdown circuit behaviour with a hung CPU.** This is the one that matters. If the ECU's
   contribution to the shutdown loop is a static GPIO level, a spinning ECU holds HV live with
   dead firmware. If it's a strobe or charge-pump that decays when the CPU stops toggling, it's
   safe by construction. Needs Shutdown rev2.

Also note Zephyr can abort just the offending thread for a thread-scoped `K_ERR_KERNEL_OOPS`.
Spinning the whole system removes that option. For a vehicle controller that's probably right,
but record it as a decision, not a transcription.

**Action:** keep the handler, add the comment explaining the IWDG interaction once confirmed,
and open a blocking item on the shutdown circuit question.

### 1.4 `PRE_KERNEL_2` re-assertion — keep, but make it observable

Reasonable defence against a driver initializing between the board primitive and kernel start
and clobbering a shared port. Realistic risk is hand-written init or an `LL_GPIO_Init` with a
wider pin mask than intended; well-behaved Zephyr pinctrl is read-modify-write per pin.

**Action:** change it from silent-fix to detect-then-fix. Read the registers, compare against
expected, re-assert *and* log or increment a counter when they differ. During bring-up that
tells you whether the guard is ever needed. Never fires by end of season → delete it. Fires →
you found a real init-ordering bug that would otherwise be invisible.

### 1.5 `__DSB()` / `__ISB()` — accepted

Correct for Cortex-M7. The store buffer means a peripheral write can be in flight when the core
resets; `DSB` forces it out. The oracle omitted it because CubeMX-generated code usually does.

One note: if the fatal path ends in `NVIC_SystemReset()`, CMSIS already wraps the write in
`__DSB()`, so the barrier is redundant there. Harmless, but the comment should say which case
it's guarding.

---

## 2. Hardware ground truth

See `mcu_breakout_rev1_pinmap.md` for the full 51-pin table and both connector pinouts,
derived from the schematic netlist annotation rather than read off a drawing.

Items that directly affect firmware:

| Fact | Consequence |
|---|---|
| **MPU6050 is at I²C address 0x69** — AD0 tied to +3.3V | If the port assumes 0x68 it will not enumerate |
| **MPU6050 INT (pin 12) not connected** | Polled reads only; no interrupt-driven sampling |
| **SN65HVD230 RS pin → 10k → GND** | Slope-control mode, hardware cap on bitrate independent of bxCAN config |
| **R502 120R fitted on-board** | This board terminates CAN; must be at a bus end, exactly one other terminator |
| **PB3 = `SWO_SWD`** | Not available as GPIO while SWO trace is in use |
| **BOOT0 externally selectable** via J601.17 / J201, R201 DNP, R202 10k | Don't assume normal-boot on every reset |
| **`GP_*` and `MISC_IO*` names are generic** | They carry no functional meaning at board level |

Suspected single-pin nets, flagged for verification, not to be relied on: J801.16 `USART_TX`,
J801.18 `USART_RX`, J801.13 `+VBAT`, J601.11 `AVDD`.

---

## 3. What is still unknown — do not guess these

The following are not in the repo and cannot be inferred from the oracle firmware. If the work
requires one of them, stop and ask rather than picking a plausible value:

1. **What `GP_OUT6` drives and whether the path inverts.** Blocks §1.2.
2. **External pulls on any `GP_OUT` / `GP_IN` net downstream of J601/J801.** These determine
   behaviour when the MCU is in reset or hung.
3. **Which nets are open-drain / wired-OR downstream.** Relevant to §1.1 if revisited.
4. **Safe state per signal** — what each load should do when the ECU stops executing. This is
   design intent, not a schematic fact, and has to come from the team.
5. **Shutdown circuit behaviour with a hung CPU.** Blocks §1.3.

Needed to close 1–3: ECU Backplane rev2 and MCU Misc rev3.1 exported as vector PDF
(File » Export » PDF, "No Variations"). Shutdown rev2 and BSPD rev4.1 close 5.

---

## 4. Working agreements

- **Hardware facts cannot be inferred from firmware.** The oracle assigning meaning to PB8 is
  evidence about the oracle, not about the board. Where the two disagree, the schematic wins
  and the disagreement is a finding worth reporting.
- **Equivalence claims must be independent.** If the Zephyr devicetree is derived from the
  oracle's `MX_GPIO_Init`, then "the port matches the oracle" proves nothing. Derive the
  hardware model from the schematic where possible so the two paths can be cross-checked.
- **Deviations from the oracle get recorded, not assumed.** Every intentional difference goes
  in a list with its justification, the way the original five were. That list is the review
  surface.
- **Don't invent CAN messages.** See §0.
- **Flag rather than fill.** A `TODO` naming the missing fact is worth more than a plausible
  default, because a plausible default is indistinguishable from a verified one six weeks later.
