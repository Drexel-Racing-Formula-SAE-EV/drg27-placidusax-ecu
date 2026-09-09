# DRG27 Placidusax ECU — FreeRTOS to Zephyr migration plan

Status: **E-000 planning draft**. No code has been migrated yet.

This plan converts the DER26 ECU firmware (STM32F767ZI + CubeMX + FreeRTOS,
source revision `DER26-ECU-v2.10.7-SAFETY2-20260827`) into a Zephyr RTOS
application in this repository, following the architecture and staging
discipline already established by the DRG27 Fortissax AMS migration.

## 0. Sources and roles

| Repository | Role in this migration |
| --- | --- |
| `DER26-ECU` @ `ae71970` | **Behavioral oracle.** v2.10.7 is the frozen reference. Zephyr replaces platform mechanisms; it does not redefine ECU safety behavior. |
| `drg27-fortissax-ams` @ `4f6bd82` | **Pattern reference.** Z-001..Z-016 AMS Zephyr port. Layering, contract gates, staging and several adapters are reused directly. |
| `DER26-AMS` @ `59e937d` | Counterpart oracle. Owns the AMS side of the shared CAN contract (`DER26-CAN-V4`, AMS power protocol v2). Not migrated here. |
| `drg27-placidusax-ecu` (this repo) | **Target.** Currently empty except LICENSE/README. |

Both MCUs are STM32F767ZI and — verified against `DER26-ECU.ioc` and
`der26_ams.dts` — both run the **same clock tree**: HSE 8 MHz crystal,
PLL M=4 N=216 P=2 → 216 MHz SYSCLK, AHB /1, APB1 /4, APB2 /2 (108 MHz timer
clocks). The AMS board DTS clock stanza transfers verbatim; the ECU adds LSE
for the RTC.

## 1. What "in a similar way as the AMS" means

`drg27-fortissax-ams/docs/ARCHITECTURE.md` freezes the rules. They carry over
unchanged, with AMS names replaced by ECU names:

```text
app/
  orchestration, thread topology, safety-policy ownership
          |
          +--------------------------+
          |                          |
          v                          v
lib/ecu_core/                include/ecu_platform/
portable algorithms          platform-neutral interfaces
NO Zephyr/HAL/STM32/FreeRTOS         |
                                     v
                              drivers/ecu/
                              Zephyr adapters
                                     |
                                     v
                       Zephyr device APIs + Devicetree
                                     |
                                     v
                                 STM32F767

Exceptional fatal path:
app fatal policy -> ecu_platform/fail_low.h
                 -> boards/.../ecu_fail_low_stm32.c
                 -> direct safe-output registers
```

The load-bearing rules:

1. **Dependencies point downward only.** `lib/ecu_core` may not include Zephyr,
   FreeRTOS, CMSIS, STM32 HAL, Devicetree or board headers.
2. **Typed Devicetree bindings, not `/zephyr,user`.** Wiring contracts become
   configuration-time schema errors, not C runtime surprises.
3. **Four separate capability states**, never inferred from one another: an
   adapter exists / the actor is live / the actor counts as safety evidence /
   physical target validation is complete. Encoded as hidden `ECU_CAP_*`
   Kconfig symbols plus user-facing `ECU_*_VALIDATED` evidence gates.
4. **Contract checkers are the regression gate**, run on every target build.
5. **Staged commits with evidence docs.** One numbered stage per reviewable
   package, each with its own closeout doc and host validation log.
6. **The mechanical portability gate.** `lib/ecu_core` must build and pass its
   tests in a plain host CMake project with no Zephyr/HAL/board include path
   available. Source review is not sufficient proof of the boundary.

## 2. ECU inventory and portability triage

~19,000 lines of application C (excluding vendor HAL/FreeRTOS/FatFs), 11
FreeRTOS tasks, and a mature host-test suite (30+ `make` targets, 14 CI gate
scripts, ASan/UBSan/Clang-analyzer qualification).

**The ECU is in materially better shape for this port than the AMS was.** The
safety-critical decision logic is already HAL-free and FreeRTOS-free because it
is exercised by host SIL tests. Scanning every application translation unit for
HAL / FreeRTOS-CMSIS / direct-register references:

### 2.1 Already platform-free — moves to `lib/ecu_core` essentially as-is

| File | LOC | Content |
| --- | --- | --- |
| `power/ecu_torque_clamp.c/.h` | 1703 | Deterministic torque→pack-current clamp |
| `power/ecu_pack_current_model.c/.h` | 1409 | Calibrated pack-current prediction |
| `power/ecu_current_residual_monitor.c/.h` | 206 | Measured-vs-predicted residual |
| `power/ecu_pack_current_calibration.c/.h` | 67+ | Calibration qualification/CRC |
| `ext_drivers/ams.c/.h` | 1476 | AMS CAN decode, freshness, authority state |
| `ext_drivers/ams_power_consumer.c/.h` | 924 | AMS power protocol v2 consumer |
| `ext_drivers/cm200.c/.h` | 724 | CM200 state/capability supervision |
| `ext_drivers/ecu_safety.c/.h` | 556 | RTD state machine, torque permission, discrete filters, CM200 packet build |
| `ext_drivers/cooling_control.c/.h` | 310 | Coolant fault policy and pump command |
| `ext_drivers/map.c/.h`, `elapsed_fault_timer.h` | ~80 | Small pure helpers |
| `ecu_config.h` | 288 | Build profile / evidence gate contract |

**≈ 7,700 lines — including the entire torque authority chain — needs no
rewrite, only relocation and a `CMakeLists.txt`.** This is the single largest
difference from the AMS port, where the portable core had to be extracted
stage by stage (Z-006 through Z-010).

### 2.2 Thin HAL shims — split conversion math from the handle

`poten.c`, `pressure_sensor.c`, `ntc.c` and `pwm.c` have HAL-free bodies; only
their headers carry a HAL handle (`ADC_HandleTypeDef *`, `TIM_HandleTypeDef *`)
in the struct. Conversion math → `lib/ecu_core`; the handle → `drivers/ecu`
behind `include/ecu_platform/*.h`.

`flow_sensor.c`, `dashboard.c`, `cli.c` and `mpu6050.c` are more entangled
(5–12 HAL references each) and are closer to a rewrite than a split, but all
four are small and none owns torque authority.

### 2.3 Requires real Zephyr rewrite

| File | LOC | Reason |
| --- | --- | --- |
| `main.c` | 1096 | CubeMX `MX_*_Init()`, clock config, MSP — replaced by Devicetree |
| `tasks/cli_task.c` | 1078 | → Zephyr shell |
| `ext_drivers/ecu_data_logger.c` | 1033 | FatFs + FreeRTOS static task/ISR ring |
| `tasks/canbus_task.c` | 612 | bxCAN mailbox ownership, critical sections |
| `ext_drivers/canbus.c` | 485 | `HAL_CAN_*`, mailbox identity tracking |
| `tasks/*.c` (9 others) | 1445 | `osDelay`/`vTaskDelayUntil`/static TCBs |
| `app.c` | 334 | Direct-register safe outputs + task startup |
| `ext_drivers/stm32f767.c` | 205 | Handle aggregation — disappears entirely |
| `ext_drivers/sdcard_service.c` | 247 | FatFs mount + FreeRTOS recursive mutex |

## 3. Target repository layout

Mirrors `drg27-fortissax-ams` one-for-one:

```text
west.yml                     Zephyr v4.4.0 pinned, self path DRG27-ECU-zephyr
zephyr/module.yml            board_root/dts_root = repo; cmake: zephyr
zephyr/CMakeLists.txt        creates ecu_platform library in KERNEL CMake mode
app/
  CMakeLists.txt             app sources + add_subdirectory(lib/ecu_core)
  Kconfig                    authority locks, evidence gates, hidden ECU_CAP_*
  prj.conf                   base profile
  ecu_vehicle.conf           overlay: authority profile (locked)
  ecu_iwdg_validation.conf   overlay: watchdog validation
  src/main.c
  src/ecu_threads.c/.h       static thread topology + heartbeat accounting
  src/ecu_supervisor.c/.h    fault aggregation (error_task successor)
boards/drexel/der26_ecu/
  board.yml, Kconfig.der26_ecu, der26_ecu_defconfig
  der26_ecu.dts
  CMakeLists.txt             zephyr_library_named(ecu_board_safety)
  ecu_fail_low_stm32.c       THE only direct-register file
dts/bindings/ecu/            drexel,ecu-* typed bindings
include/ecu_platform/        narrow platform interfaces
drivers/ecu/                 Zephyr adapters (zephyr_library_named(ecu_platform))
lib/ecu_core/                portable C, plain CMake, no Zephyr helpers
tests/unit/                  host-native differential tests (ported from host_tests/)
tests/system/                host SIL
tests/null_platform/         mechanical portability proof
scripts/check_*.py           contract checkers
docs/migration/              staged closeout docs + evidence
```

## 4. Hardware contract → Devicetree

From `DER26-ECU/docs/PIN_MAP.md` and the `.ioc`. Peripherals in use: ADC1,
ADC2, ADC3, CAN1, I2C2, RTC, SPI6, TIM3, TIM4, TIM5, UART7, USART3, IWDG.

| Function | Pins | Zephyr representation | Ownership decision |
| --- | --- | --- | --- |
| Safety outputs: Firmware_Ok PA7, Cascadia_ON PA5, MTR_EN PF10, Buzzer PF13, CoolPump gate PB8 | — | `drexel,ecu-safety-io` typed node | **Board-owned direct register** for the fail-low primitive; normal writes via `gpio_dt_spec` |
| Protected discretes: IMD_Fail PF14, BMS_Fail PF15, BSPD_OK PE13, TSAL PC5, RTD_Go PE4, MTR_Fault PB2, MTR_Ok PF12 | — | `drexel,ecu-discrete-in` with per-signal active-level flags | Generic `gpio` |
| APPS1 PA3 (ADC1_IN3), APPS2 PC0 (ADC2_IN10) | analog | `drexel,ecu-apps-sense` | **Decision point — see §6.2** |
| BSE1 PC3 (ADC3_IN13), BSE2 PF3 (ADC3_IN9), CoolPress PF9 (IN7), CoolTemp1 PF5 (IN15), CoolTemp2 PF4 (IN14) | analog | `drexel,ecu-analog-bank` | ADC3, 5 channels, per-read channel reconfiguration |
| CoolFlow PA0 | TIM5 CH1/CH2 | `drexel,ecu-flow-sense`, `pwms` + capture | Zephyr `PWM_CAPTURE` — **direct reuse of the AMS Z-013 IMD capture adapter** |
| CoolPump PB8 | TIM4 CH3 | `drexel,ecu-pump` | `pwm_stm32`, inverted low-side gate semantics preserved |
| SSA LED PB1 | TIM3 CH4 | in `drexel,ecu-indicators` | `pwm_stm32` |
| CAN1 | PD0/PD1 | `&can1` | Zephyr `can_stm32_bxcan` — **see §6.1** |
| SD card | SPI6 PG12/13/14 + NSS PG8 | `&spi6` + `zephyr,sdhc-spi-slot` + `sdmmc-disk` | Generic `spi_stm32` (non-safety path) |
| CLI | USART3 PD8/PD9 | `zephyr,console` + `zephyr,shell-uart` | Generic |
| Dashboard | UART7 PF6/PF7 | `&uart7` | Generic |
| MPU6050 | I2C2 PF0/PF1 | `&i2c2` + in-tree `invensense,mpu6050` | Replace custom driver |
| RTC | LSE | `&rtc` + `&clk_lse` | Zephyr RTC API |
| Watchdog | IWDG | `aliases { ecu-watchdog = &iwdg; }` | **Direct reuse of AMS `watchdog_zephyr.c`** |

**Interrupt priorities must be preserved explicitly in DTS** (the AMS port hit
this with TIM2): CubeMX uses `NVIC_PRIORITYGROUP_4` with CAN1_RX0 = CAN1_TX =
CAN1_SCE = TIM5 = UART7 = **5**, USART3 = 15. Zephyr's SoC DTS defaults differ;
override with `interrupts = <n 5>;` per node and assert with `BUILD_ASSERT` in
the adapter, exactly as `fan_pwm_zephyr.c` does.

## 5. Staged migration

Each stage is one reviewable commit package with a `docs/migration/E0NN_*.md`
closeout and a host validation log. A stage lands only when its gate is green.

| Stage | Deliverable | Acceptance gate |
| --- | --- | --- |
| **E-000** | This plan; freeze v2.10.7 oracle; worktree SHA256SUMS; import `host_tests/` as the differential oracle | Plan reviewed; oracle hashes recorded |
| **E-001** | West workspace bootstrap: `west.yml` (Zephyr v4.4.0), `zephyr/module.yml`, `zephyr/CMakeLists.txt`, app skeleton, `.gitignore` | `west build` succeeds for a stub `main()` |
| **E-002** | Board `der26_ecu`: clock tree, USART3 console, pinctrl, `board.yml`/Kconfig/defconfig | Boots on target, prints banner; `check_board_contract.py` |
| **E-003** | **Fail-low safe-output foundation.** Board-owned `ecu_fail_low_stm32.c` at `PRE_KERNEL_1`, `k_sys_fatal_error_handler` override, authority Kconfig locks (`ECU_TORQUE_AUTHORITY=n`, `ECU_CASCADIA_AUTHORITY=n`) with `BUILD_ASSERT` backstops | Scope-verified: PA5/PA7/PF10/PF13 low and PB8 gate released before kernel init, and after a forced fault |
| **E-004** | Static thread topology: 11 threads at the exact v2.10.7 priorities and periods, heartbeat accounting, WCET instrumentation, `CONFIG_HEAP_MEM_POOL_SIZE=0` | `check_runtime_contract.py`; thread manifest printed matches `app.h` table |
| **E-005** | `lib/ecu_core` foundation — relocate §2.1 files; add `ecu_core_contract.c` | `check_null_platform_core.py` green; ported host tests pass bit-identically against the oracle |
| **E-006** | Sensor conversion cores (`poten`/`bse`/`ntc`/`flow`/`cooling` math) + `include/ecu_platform/analog_in.h` seam | Host differential vs oracle conversions |
| **E-007** | Discrete input + safety output adapters; debounce policy (`ECU_DISCRETE_CLEAR_SAMPLES`) stays in core | `check_safety_io_contract.py`; bench discrete sweep |
| **E-008** | ADC acquisition (APPS ×2, BSE ×2, cooling ×3) — **see §6.2** | Timeout-recovery test; 100 Hz jitter measurement |
| **E-009** | Coolant pump PWM (TIM4 CH3 inverted gate) + SSA indicator (TIM3 CH4) | Cycle-exact duty verification on scope |
| **E-010** | Coolant flow capture (TIM5 CH1/CH2) — port AMS IMD capture adapter | Frequency injection test |
| **E-011** | **CAN RX only.** Filters, FIFO0, freshness, sequence/coherency, AMS + CM200 decode wired to core | Bus replay vs oracle decode; no TX |
| **E-012** | CAN TX for non-authority frames (AMS feedback, telemetry) + bus-off/error-state escalation | Bus-off injection; `can_recovery_count` parity |
| **E-013** | **CM200 protected commit path** — the hard seam, §6.1 | On-wire completion proof; `tx_outcome_uncertain` latch behavior reproduced |
| **E-014** | RTD/buzzer/brake-light state machine, dashboard UART7 | Full RTD sequence on bench |
| **E-015** | IWDG watchdog — port AMS `watchdog_zephyr.c` + policy | Starvation, supervisor-death and reset-cause tests (AMS Z-014 procedure) |
| **E-016** | SD logging: SPI6 SDHC + FAT + logger rewritten on Zephyr FS API | Log schema `LOGGER3` byte-compatible; logger provably non-gating |
| **E-017** | Shell/CLI on USART3 (Zephyr shell) | Command parity matrix vs `cli_task.c` |
| **E-018** | Torque authority integration, complete contract suite, evidence-gate translation | `check_all_contracts.py` green on target build; profile gates enforced |

E-001 through E-005 are largely mechanical and unblock everything else; that is
where the port should start. E-011 through E-013 carry the real risk.

## 6. Hard seams, ranked

### 6.1 CAN transmit commit semantics — highest risk

v2.10.7 does not treat "frame handed to the controller" as the authority event.
`canbus.c` tracks **mailbox identity** (`tx_pending_mailbox_mask`,
`tx_complete_mailbox_mask`, `tx_abort_mailbox_mask`), waits for a terminal
hardware callback within `CANBUS_CM200_TX_COMPLETE_TIMEOUT_MS`, and latches
`tx_outcome_uncertain_latched` — permanently refusing further frames — when a
bounded abort-reconcile window expires without a terminal outcome. The CM200
rolling counter is only advanced on confirmed on-wire completion. The final
commit runs inside `taskENTER_CRITICAL()` with a re-read of AMS/CM200 authority.

Zephyr's `can_send()` takes a completion callback but does **not** expose
mailbox identity, and its bus-off/state-change callback does not sequence
repeated bus-off events. The AMS `ARCHITECTURE.md` already flags this exact
problem as an open seam.

Approach, mirroring the audited AMS Z-015 SPI decision rather than guessing:

1. Write `docs/migration/E013_STM32_BXCAN_DRIVER_AUDIT.md` — read Zephyr
   v4.4.0 `can_stm32_bxcan.c` and answer: when is the TX callback invoked
   relative to RQCP/TXOK/ALST/TERR; what happens on `can_stop()`/abort; is
   mailbox→callback mapping recoverable; is bus-off recovery automatic
   (`CONFIG_CAN_AUTO_BUS_OFF_RECOVERY`) and can that be disabled.
2. Write `docs/migration/E013_CAN_COMMIT_ORACLE.md` — the exact v2.10.7
   commit-path state machine as a testable specification.
3. Only then choose: (a) generic `can_stm32_bxcan` + a per-frame token table if
   the audit proves terminal-outcome fidelity, or (b) a private bounded bxCAN
   TX backend under `drivers/ecu/`, structured exactly like
   `adbms_spi_stm32.c` — one audited exception, Devicetree/pinctrl/clock/reset
   still from Zephyr, with `CONFIG_LTO=n` preserving final-ELF caller proof.

Do not begin E-013 implementation before both documents exist.

Also unresolved: FreeRTOS `taskENTER_CRITICAL()` masks up to
`configMAX_SYSCALL_INTERRUPT_PRIORITY`; the nearest Zephyr equivalents
(`irq_lock()`, `k_sched_lock()`) have different semantics. The commit critical
section is currently measured at 100 Hz with DWT — the parity doc must state
which primitive preserves the atomicity the clamp verification depends on.

### 6.2 ADC ownership — decision required at E-008

The AMS port deliberately disabled `CONFIG_ADC` and wrote a private bounded
STM32 polling backend, because `adc_context` transactions cannot be safely
cancelled after a conversion timeout and the recovery path inherits uncertain
ISR ownership. The ECU has the same requirement on **safety inputs** (APPS
plausibility and BSE at 100 Hz with a 90 ms implausibility limit), across all
three ADC instances.

Complication: STM32F767 has **one common reset bit for ADC1/2/3**. The AMS
avoided this by keeping ADC3 disabled. The ECU uses all three, so a reset-based
recovery on one instance disturbs the others — the recovery policy must be
defined at the bank level, not per channel.

Recommendation: extend the already-audited AMS `current_adc_stm32.c` backend to
a three-instance ECU variant with a shared, explicitly specified common-reset
recovery policy. Record the decision in `E008_ADC_OWNERSHIP_AUDIT.md` before
writing the adapter.

### 6.3 Fail-low primitive is broader than the AMS's

`ecu_force_safe_outputs()` touches three GPIO ports plus `TIM4->CCER`, and must
work before `MX_GPIO_Init()` equivalents have run. It is the ECU's analogue of
the AMS PE0 primitive and gets the same treatment: board-owned, direct
register, `PRE_KERNEL_1`, the **only** file permitted direct register access,
enforced by `check_architecture_contract.py`. Note the PB8 semantics — gate-low
deliberately invokes the pump's full-speed fallback; that inversion must be
preserved and documented, not "fixed".

### 6.4 Cycle-accurate WCET evidence

The torque clamp enforces `ECU_TORQUE_CLAMP_SOFT_BUDGET_US=3000` /
`HARD_BUDGET_US=8000` and trips after 2 consecutive overruns, measured with the
Cortex-M7 DWT cycle counter. Zephyr's `k_cycle_get_32()` may be SysTick-based
depending on configuration. Enable DWT explicitly and assert the counter source
and frequency at build time; a silently different cycle domain would corrupt a
safety deadline.

### 6.5 Static allocation

v2.10.7 is fully static (`StaticTask_t`, `StaticQueue_t`; a
`check_static_allocation` CI gate already exists). Zephyr equivalent:
`K_THREAD_STACK_DEFINE`, `K_MSGQ_DEFINE`, `CONFIG_HEAP_MEM_POOL_SIZE=0`. The SD
card/FAT stack may demand a heap; if so, the gate becomes "no dynamic
allocation reachable from the safety path", proven from the ELF, rather than a
blanket zero-heap claim.

### 6.6 Evidence gates

`ecu_config.h` already implements exactly the discipline the AMS expresses in
Kconfig: profile selection (`bench`/`vehicle`/`testday`), 13 independent
`ECU_*_VALIDATED` gates, `#error` on an under-evidenced vehicle build, and
source-controlled implementation latches. Translate 1:1 into `app/Kconfig` as
user-facing `ECU_*_VALIDATED` symbols plus hidden `ECU_CAP_*` migration facts,
and keep the `#error` semantics as `BUILD_ASSERT`. Preserve the rule that no
single switch can grant vehicle authority.

### 6.7 Smaller items

- `configUSE_NEWLIB_REENTRANT=1` → decide `CONFIG_NEWLIB_LIBC` vs picolibc, and
  confirm float formatting for the CLI and logger.
- CM200 CAN timing (`DER26_CAN_PRESCALER` 6 @ 500 kbps / 12 @ 250 kbps) must be
  re-derived from Zephyr's `can_set_timing()` and asserted equal.
- `ecu_data_logger.c` uses a lock-free ISR→task CAN ring; Zephyr message queues
  are not a drop-in equivalent for its overwrite semantics.
- The MPU6050 path is non-safety — switching to the in-tree Zephyr driver is a
  simplification, not a risk.

## 7. Direct reuse from `drg27-fortissax-ams`

Copy and re-namespace rather than rewrite:

- `zephyr/module.yml`, `zephyr/CMakeLists.txt`, `app/CMakeLists.txt` structure,
  including the kernel-CMake-mode rule and the two `FATAL_ERROR` guards.
- `boards/drexel/der26_ams/*` as the board template (identical SoC and clocks).
- `drivers/ams/watchdog_zephyr.c` + `include/ams_platform/watchdog.h` → IWDG.
- `drivers/ams/imd_capture_zephyr.c` → coolant flow capture (same two-channel
  PWM capture topology with hardware slave reset).
- `drivers/ams/fan_pwm_zephyr.c` → pump/SSA PWM (cycle-exact `pwm_set_cycles`).
- `drivers/ams/current_adc_stm32.c` → basis for the ECU ADC backend (§6.2).
- `drivers/ams/bms_ok_zephyr.c` + `boards/.../ams_fail_low_stm32.c` → the
  safe-output/fail-low pair (§6.3).
- `scripts/check_architecture_contract.py`, `check_null_platform_core.py`,
  `check_capability_contract.py`, `check_runtime_contract.py`,
  `check_all_contracts.py`, `build_manifest.py`, and the
  `run_zNNN_host_validation.py` harness shape.
- `lib/ams_core/CMakeLists.txt` and `tests/null_platform/` verbatim in shape.
- `.github/workflows/portable-core-null-platform.yml`.

## 8. Test, CI and evidence strategy

The ECU's existing `host_tests/` suite is the most valuable asset in this
migration and must be preserved as a **differential oracle**, not rewritten:

1. At E-000, import `host_tests/` and `ci/scripts/` unchanged, pointed at the
   frozen v2.10.7 sources, and record the passing baseline.
2. As each file moves into `lib/ecu_core` at E-005+, re-point its test at the
   new location. **A moved file whose tests do not produce byte-identical
   results has been changed, not moved** — that is a stage failure.
3. Add the null-platform gate (`check_null_platform_core.py`) as the mechanical
   proof that the boundary is real.
4. Add a `check_freertos_runtime_parity.py` equivalent that pins the Zephyr
   thread table against the v2.10.7 priority/period table in `app.h`.
5. Keep the existing gates that still apply: `check_task_contract.py`,
   `check_fault_path_gate.py`, `check_can_v4_contract.py`,
   `check_can_clock_contract.py`, `check_data_logger_contract.py`,
   `check_static_allocation`, ASan/UBSan/Clang-analyzer.
6. CI jobs: portable-core null-platform (every push), host validation, Zephyr
   target build for `der26_ecu`, and `check_all_contracts.py` against that
   build.

## 9. Non-goals for this migration

- No behavioral redesign. Zephyr replaces platform mechanisms only.
- No new vehicle authority. Every stage builds as a compile-time non-authority
  image until E-018, and vehicle authority still requires the full
  `ECU_FULL_RELEASE_EVIDENCE` set independently.
- No CM200 control-loop work — that boundary is unchanged.
- The AMS side of the CAN contract is not modified here.
- "Use more Zephyr" is not itself a goal. Zephyr is preferred for platform
  mechanisms only where it preserves or strengthens the frozen v2.10.7
  semantics.

## 10. Immediate next actions

1. Review and agree this plan (in particular §6.1 and §6.2, which decide the
   two backend-ownership questions).
2. E-000: import the frozen oracle test suite and record baseline hashes.
3. E-001/E-002: bootstrap the west workspace and the `der26_ecu` board, copied
   from the AMS board with LSE/RTC added — this should reach a booting console
   banner quickly and de-risks everything after it.
4. E-003: land the fail-low safe-output foundation before any actuator adapter
   exists, exactly as the AMS landed BMS_OK fail-low at Z-003.
