#!/usr/bin/env python3
"""Freeze the DER26 ECU board hardware contract.

The Devicetree is the wiring contract for this migration, so it is checked
mechanically rather than by review. Every fact asserted here comes from the
v2.10.7 oracle: docs/PIN_MAP.md, DER26-ECU.ioc, and SystemClock_Config().

Normal use, against a completed target build:

    python3 scripts/check_board_contract.py <build-dir>

The E-002 stage has no target toolchain, so an explicit EDT pickle produced by
Zephyr's own gen_edt.py may be supplied instead:

    python3 scripts/check_board_contract.py --edt-pickle <path>
"""

import argparse
import os
import pickle
import sys
from pathlib import Path


def _add_edtlib_to_path():
    """Unpickling an EDT needs Zephyr's devicetree package importable."""
    candidates = []
    zephyr_base = os.environ.get("ZEPHYR_BASE")
    if zephyr_base:
        candidates.append(Path(zephyr_base))
    # west workspace layout: <topdir>/zephyr alongside this repository
    here = Path(__file__).resolve().parent.parent
    candidates.append(here.parent / "zephyr")
    candidates.append(here.parent / "zephyrproject" / "zephyr")

    for base in candidates:
        src = base / "scripts" / "dts" / "python-devicetree" / "src"
        if src.is_dir():
            sys.path.insert(0, str(src))
            return True
    return False


_add_edtlib_to_path()

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)


def node(edt, label_or_path):
    """Resolve by Devicetree label first, then by full path."""
    found = edt.label2node.get(label_or_path)
    if found is not None:
        return found
    try:
        return edt.get_node(label_or_path)
    except Exception:
        return None


def irq_pairs(n):
    """Return [(irq, priority), ...] as written in the Devicetree."""
    out = []
    for irq in n.interrupts:
        data = irq.data
        out.append((data.get("irq"), data.get("priority")))
    return out


def pinctrl_names(n):
    names = []
    for pc in n.pinctrls:
        for conf in pc.conf_nodes:
            names.append(conf.name)
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", nargs="?", type=Path)
    parser.add_argument("--edt-pickle", type=Path)
    args = parser.parse_args()

    if args.edt_pickle is not None:
        pickle_path = args.edt_pickle
    elif args.build_dir is not None:
        pickle_path = args.build_dir / "zephyr" / "edt.pickle"
    else:
        parser.error("give a build directory or --edt-pickle")

    if not pickle_path.is_file():
        print(f"FAIL: missing EDT pickle: {pickle_path}")
        return 1

    with open(pickle_path, "rb") as f:
        edt = pickle.load(f)

    # -- Clock tree ---------------------------------------------------------
    # v2.10.7 SystemClock_Config(): HSE 8 MHz crystal, PLL M=4 N=216 P=2,
    # 216 MHz SYSCLK, AHB /1, APB1 /4, APB2 /2. Identical to the DER26 AMS.
    hse = node(edt, "clk_hse")
    check(hse is not None and hse.props["clock-frequency"].val == 8000000,
          "HSE must be an 8 MHz crystal")
    # edtlib materializes declared booleans, so test the value, not the key.
    check(hse is not None and hse.props["hse-bypass"].val is False,
          "DER26 uses a real crystal: hse-bypass must not be set")

    lse = node(edt, "clk_lse")
    check(lse is not None and lse.status == "okay",
          "LSE must be enabled: the ECU RTC depends on it")

    pll = node(edt, "pll")
    if pll is None:
        check(False, "missing PLL node")
    else:
        check(pll.props["div-m"].val == 4, "PLL div-m must be 4")
        check(pll.props["mul-n"].val == 216, "PLL mul-n must be 216")
        check(pll.props["div-p"].val == 2, "PLL div-p must be 2")

    rcc = node(edt, "rcc")
    if rcc is None:
        check(False, "missing rcc node")
    else:
        check(rcc.props["clock-frequency"].val == 216000000,
              "SYSCLK must be 216 MHz")
        check(rcc.props["ahb-prescaler"].val == 1, "AHB prescaler must be 1")
        check(rcc.props["apb1-prescaler"].val == 4, "APB1 prescaler must be 4")
        check(rcc.props["apb2-prescaler"].val == 2, "APB2 prescaler must be 2")

    # -- Console ------------------------------------------------------------
    usart3 = node(edt, "usart3")
    if usart3 is None:
        check(False, "missing usart3")
    else:
        check(usart3.status == "okay", "usart3 console must be enabled")
        check(irq_pairs(usart3) == [(39, 15)],
              "USART3 must stay at IRQ 39 priority 15 (v2.10.7 CubeMX)")
        check(sorted(pinctrl_names(usart3)) == ["usart3_rx_pd9", "usart3_tx_pd8"],
              "USART3 must remain on PD8/PD9")

    # -- Frozen NVIC priorities --------------------------------------------
    # v2.10.7 uses NVIC_PRIORITYGROUP_4. These priorities are part of the
    # safety timing contract, not a Zephyr default to inherit.
    can1 = node(edt, "can1")
    if can1 is None:
        check(False, "missing can1")
    else:
        check(irq_pairs(can1) == [(19, 5), (20, 5), (21, 5), (22, 5)],
              "CAN1 TX/RX0/RX1/SCE must all be priority 5")
        check(sorted(pinctrl_names(can1)) == ["can1_rx_pd0", "can1_tx_pd1"],
              "CAN1 must remain on PD0/PD1")

    t5 = node(edt, "timers5")
    check(t5 is not None and irq_pairs(t5) == [(50, 5)],
          "TIM5 must stay at IRQ 50 priority 5")

    u7 = node(edt, "uart7")
    check(u7 is not None and irq_pairs(u7) == [(82, 5)],
          "UART7 must stay at IRQ 82 priority 5")

    # -- Authority-bearing peripherals stay unowned -------------------------
    # Presence of a hardware contract never implies the migration owns it yet.
    for label, stage in (
        ("can1", "E-011"),
        ("adc1", "E-008"),
        ("adc2", "E-008"),
        ("adc3", "E-008"),
        ("timers3", "E-009"),
        ("timers4", "E-009"),
        ("timers5", "E-010"),
        ("uart7", "E-014"),
        ("spi6", "E-016"),
        ("i2c2", "E-014"),
        ("rtc", "E-016"),
        ("iwdg", "E-015"),
    ):
        n = node(edt, label)
        check(n is not None and n.status == "disabled",
              f"{label} must remain disabled until {stage}")

    # -- E-003 safety outputs -----------------------------------------------
    # These five pins are the vehicle's physical safety surface. Their
    # controller, pin number and active level are all frozen.
    safety = node(edt, "ecu_safety_io")
    if safety is None:
        check(False, "missing ecu_safety_io node")
    else:
        check(safety.status == "okay", "ecu_safety_io must be enabled")
        expected = {
            "firmware-ok-gpios": ("gpioa", 7),
            "cascadia-on-gpios": ("gpioa", 5),
            "inverter-enable-gpios": ("gpiof", 10),
            "buzzer-gpios": ("gpiof", 13),
            "pump-gate-gpios": ("gpiob", 8),
        }
        for prop, (want_ctlr, want_pin) in expected.items():
            if prop not in safety.props:
                check(False, f"ecu_safety_io missing {prop}")
                continue
            entry = safety.props[prop].val[0]
            got_ctlr = entry.controller.labels[0]
            got_pin = entry.data["pin"]
            check(got_ctlr == want_ctlr and got_pin == want_pin,
                  f"{prop} must remain {want_ctlr} pin {want_pin}, "
                  f"got {got_ctlr} pin {got_pin}")
            # flags 0 == GPIO_ACTIVE_HIGH. The pump gate's inversion is
            # downstream of the pin and must NOT be encoded here: doing so
            # would invert the safe level.
            check(entry.data["flags"] == 0,
                  f"{prop} must remain active-high at the pin")

    # -- Pump inversion note is load-bearing --------------------------------
    t4 = node(edt, "timers4")
    check(t4 is not None and pinctrl_names(t4.children["pwm"]) == ["tim4_ch3_pb8"],
          "coolant pump must remain on PB8 = TIM4_CH3")

    if FAILURES:
        print(f"FAIL: {len(FAILURES)} board contract violation(s)")
        for f in FAILURES:
            print(f"  - {f}")
        return 1

    print("PASS: DER26 ECU board contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
