# DRG27 Placidusax ECU

Zephyr RTOS port of the Drexel Electric Racing DER26 vehicle ECU firmware.

**Status: E-002.** Workspace and board definition only. This image has no safe
outputs, no authority, and has never been built for the target or flashed.
Do not connect it to a vehicle.

Oracle: `DER26-ECU-v2.10.7-SAFETY2-20260827` (STM32F767ZI + CubeMX + FreeRTOS).
Zephyr replaces platform mechanisms; it does not redefine ECU safety behavior.

- [Migration plan](docs/migration/E000_MIGRATION_PLAN.md) — staging E-000..E-018,
  portability triage, and the ranked hard seams.
- [E-001/E-002 closeout](docs/migration/E002_CLOSEOUT.md) — what landed, the
  evidence behind it, and what is explicitly not claimed.

## Architecture invariant

`lib/ecu_core` is host-native portable C: no Zephyr, FreeRTOS, CMSIS, STM32
HAL, board headers or `ecu_platform` dependency. Hardware access belongs in
`drivers/ecu` behind narrow interfaces and typed Devicetree bindings. The only
file permitted direct register access is the board-owned fail-low primitive.

## Gates

Portability, needing only CMake and a host compiler:

```text
python3 scripts/check_null_platform_core.py .
```

Board hardware contract, against a target build:

```text
python3 scripts/check_board_contract.py <build-dir>
```

## Authority

All three authority locks — torque, inverter enable, Firmware_Ok — are disabled
in Kconfig and backstopped by `BUILD_ASSERT`. Vehicle authority additionally
requires the complete `ECU_*_VALIDATED` evidence set, which no single switch
can grant.
