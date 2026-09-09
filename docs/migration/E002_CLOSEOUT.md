# E-001 / E-002 closeout — workspace bootstrap and DER26 ECU board

Stages: **E-001 west workspace bootstrap** and **E-002 board definition**,
per `docs/migration/E000_MIGRATION_PLAN.md`.

Oracle: `DER26-ECU-v2.10.7-SAFETY2-20260827`.
Pattern reference: `drg27-fortissax-ams` at Z-016.

## What landed

### E-001 — workspace

- `west.yml` pinning Zephyr **v4.4.0**, self path `DRG27-ECU-zephyr`.
- `zephyr/module.yml` and `zephyr/CMakeLists.txt`, so `drivers/ecu` is created
  by `zephyr_library_named(ecu_platform)` while Zephyr is still in **kernel**
  CMake mode. `app/CMakeLists.txt` must never add that directory itself.
- `app/CMakeLists.txt` with the two structural `FATAL_ERROR` guards inherited
  from the AMS pattern, and `add_subdirectory()` of the portable core.
- `app/Kconfig`: three authority locks, the thirteen `ECU_*_VALIDATED` evidence
  gates translated one-for-one from the oracle's `ecu_config.h`, plus hidden
  `ECU_CAP_*` migration facts.
- `lib/ecu_core` foundation with `ecu_core_contract.c` and plain CMake that
  contains no Zephyr helpers.
- `drivers/ecu/ecu_platform_anchor.c`, present only so the `ecu_platform`
  target exists from stage one and the app's guard is a real invariant.

### E-002 — board

- `boards/drexel/der26_ecu/` with `board.yml`, `Kconfig.der26_ecu`,
  `der26_ecu_defconfig` and `der26_ecu.dts`.
- The complete DER26 pin contract from `docs/PIN_MAP.md` and `DER26-ECU.ioc`,
  with every peripheral except the USART3 console left `disabled` and annotated
  with the stage that owns it.
- Frozen NVIC priorities recorded now rather than inherited from Zephyr's SoC
  defaults: CAN1 TX/RX0/RX1/SCE = 5, TIM5 = 5, UART7 = 5, USART3 = 15.

## Evidence

### Devicetree validated by Zephyr's own tooling

The board devicetree was preprocessed and run through Zephyr v4.4.0's
`scripts/dts/gen_edt.py` with `--edtlib-Werror`, against the real Zephyr and
`hal_stm32` binding sets. **Result: clean, no errors and no warnings.**

All 22 pinctrl labels required by the DER26 pin map were confirmed to exist in
`modules/hal/stm32/dts/st/f7/stm32f767zitx-pinctrl.dtsi`.

The clock tree was confirmed identical to the DER26 AMS board, as the E-000
plan predicted: HSE 8 MHz crystal, PLL M=4 N=216 P=2, 216 MHz SYSCLK,
AHB /1, APB1 /4, APB2 /2.

### `scripts/check_board_contract.py`

New. Asserts the frozen clock tree, console wiring, NVIC priorities, and that
every not-yet-owned peripheral is still `disabled`. Passes on the board, and
**catches all five** deliberate mutations:

| Mutation | Result |
| --- | --- |
| PLL `mul-n` 216 &rarr; 200 | caught |
| USART3 IRQ priority 15 &rarr; 0 | caught |
| CAN1 enabled before E-011 | caught |
| APB1 prescaler 4 &rarr; 2 | caught |
| Coolant pump PB8 &rarr; PD14 | caught |

### `scripts/check_null_platform_core.py`

New. Builds the real `lib/ecu_core/CMakeLists.txt` in a host CMake project with
no Zephyr, FreeRTOS, CMSIS, HAL, board or `ecu_platform` include path, runs
CTest, then audits `compile_commands.json` for leaked platform references.
Passes, and **catches all three** mutations:

| Mutation | Result |
| --- | --- |
| Core includes `<zephyr/kernel.h>` | caught |
| Core includes `"FreeRTOS.h"` | caught |
| Core contract check inverted | caught |

Wired into CI as `.github/workflows/portable-core-null-platform.yml`.

## What is explicitly NOT claimed

This package proves configuration-time and host-native properties only.

- **No target build.** No Zephyr SDK or ARM toolchain was available, so no
  `west build` was run and no ELF exists. The CMake and Kconfig wiring is
  structurally faithful to the AMS pattern but **unbuilt**.
- **No hardware.** Nothing has been flashed or scoped. The E-002 gate in the
  plan calls for a booting console banner on the real board; that remains open.
- **No safe outputs.** `ecu_force_safe_outputs()` has no Zephyr equivalent yet.
  Until E-003 lands, this image must not be flashed to a vehicle-connected ECU.
- **No authority.** All three authority locks are disabled and backstopped by
  `BUILD_ASSERT` in `app/src/main.c`.
- **No ADC decision.** Seam 4.2 remains open; ADC1/2/3 stay disabled.
- **No CAN decision.** Seam 4.1 remains open; CAN1 stays disabled and no bit
  timing has been transcribed.

## Next

**E-003, the fail-low safe-output foundation.** It should land before any
actuator adapter exists, exactly as the AMS landed BMS_OK fail-low at Z-003.

## Reproducing the evidence

Devicetree validation needs a Zephyr v4.4.0 tree and the `hal_stm32` module
(`dts/` alone is sufficient):

```text
cpp -undef -x assembler-with-cpp -nostdinc \
    -I zephyr/include -I zephyr/dts/arm -I zephyr/dts/common -I zephyr/dts \
    -I modules/hal/stm32/dts -D__DTS__ -P -E \
    boards/drexel/der26_ecu/der26_ecu.dts -o pp.dts

python3 zephyr/scripts/dts/gen_edt.py --dts pp.dts \
    --dtc-flags=-Wno-simple_bus_reg \
    --bindings-dirs zephyr/dts/bindings modules/hal/stm32/dts/bindings dts/bindings \
    --dts-out merged.dts --edt-pickle-out edt.pickle --edtlib-Werror

python3 scripts/check_board_contract.py --edt-pickle edt.pickle
```

The portability gate needs only CMake and a host compiler:

```text
python3 scripts/check_null_platform_core.py .
```
