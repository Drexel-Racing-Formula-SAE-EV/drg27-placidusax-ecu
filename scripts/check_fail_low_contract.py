#!/usr/bin/env python3
"""E-003 safe-output architecture gate.

Direct MCU register access is the one approved escape hatch in this migration,
and it is confined to a single board-owned file. This checker proves that
containment mechanically, because it is exactly the property that erodes
silently as a codebase grows.

    python3 scripts/check_fail_low_contract.py <repo-root>
"""

import argparse
import re
import sys
from pathlib import Path

# The ONLY file permitted direct register access.
FAIL_LOW_FILE = Path("boards/drexel/der26_ecu/ecu_fail_low_stm32.c")

PRODUCTION_DIRS = ("app", "drivers", "boards", "lib")

# Peripheral register-block dereferences, e.g. GPIOA->MODER, RCC->AHB1ENR.
REGISTER_RE = re.compile(
    r"\b(RCC|PWR|FLASH|GPIO[A-K]|TIM\d+|IWDG|WWDG|CAN\d|ADC\d|ADC_Common|"
    r"SPI\d|I2C\d|USART\d|UART\d|DMA\d|EXTI|SYSCFG|NVIC|SCB|DWT)\s*->"
)

# Safety pins the primitive must drive, as (port, pin, signal).
# Named by schematic net. Function for MISC_IO4 and GP_OUT6 is assigned
# downstream and is deliberately not asserted here.
REQUIRED_PINS = (
    ("GPIOA", 7, "Firmware_Ok"),
    ("GPIOA", 5, "MISC_IO4"),
    ("GPIOF", 10, "MTR_EN"),
    ("GPIOF", 13, "Buzzer"),
    ("GPIOB", 8, "GP_OUT6"),
)

FAILURES = []


def fail(msg):
    FAILURES.append(msg)


def strip_c_comments(text):
    """Remove comments, preserving newlines and string literals.

    Contract scans must inspect executable source. Provenance prose in this
    repository names registers and pins deliberately, and must not be mistaken
    for a dependency.
    """
    out = []
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "*":
                out.append("  ")
                i += 2
                state = "block"
                continue
            if ch == "/" and nxt == "/":
                out.append("  ")
                i += 2
                state = "line"
                continue
            if ch in ('"', "'"):
                out.append(ch)
                i += 1
                state = "string" if ch == '"' else "char"
                continue
            out.append(ch)
            i += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/":
                out.append("  ")
                i += 2
                state = "code"
            else:
                out.append("\n" if ch == "\n" else " ")
                i += 1
            continue
        if state == "line":
            if ch == "\n":
                out.append("\n")
                state = "code"
            else:
                out.append(" ")
            i += 1
            continue
        out.append(ch)
        if ch == "\\" and i + 1 < len(text):
            out.append(text[i + 1])
            i += 2
            continue
        if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
            state = "code"
        i += 1
    return "".join(out)


def sources(repo):
    for d in PRODUCTION_DIRS:
        base = repo / d
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.is_file() and path.suffix in (".c", ".h"):
                yield path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    args = parser.parse_args()
    repo = args.repo_root.resolve()

    primitive = repo / FAIL_LOW_FILE
    if not primitive.is_file():
        print(f"FAIL: missing safe-output primitive: {FAIL_LOW_FILE}")
        return 1

    # -- 1. Direct register access is confined to the one board file --------
    for path in sources(repo):
        rel = path.relative_to(repo)
        if rel == FAIL_LOW_FILE:
            continue
        code = strip_c_comments(path.read_text(encoding="utf-8", errors="replace"))
        for lineno, line in enumerate(code.splitlines(), 1):
            match = REGISTER_RE.search(line)
            if match:
                fail(f"{rel}:{lineno} direct register access outside the "
                     f"approved primitive: {match.group(1)}->")

    body = strip_c_comments(primitive.read_text(encoding="utf-8"))

    # -- 2. Every safety output is actually driven --------------------------
    # Test for a real register DEREFERENCE, not a substring: the clock-enable
    # macro RCC_AHB1ENR_GPIOBEN contains "GPIOB" and would otherwise satisfy
    # this check while the pump gate was never driven at all.
    for port in sorted({p for p, _, _ in REQUIRED_PINS}):
        signals = ", ".join(s for prt, _, s in REQUIRED_PINS if prt == port)
        if not re.search(rf"\b{port}\s*->\s*BSRR", body):
            fail(f"primitive never writes {port}->BSRR ({signals})")
        if not re.search(rf"\b{port}\s*->\s*MODER", body):
            fail(f"primitive never forces {port}->MODER to output ({signals})")

    # Each pin constant must be defined with the frozen pin number and used.
    for port, pin, signal in REQUIRED_PINS:
        defines = re.findall(r"#define\s+(ECU_PIN_\w+)\s+(\d+)U", body)
        if not any(int(v) == pin for _, v in defines):
            fail(f"no ECU_PIN_* constant defines pin {pin} ({signal} on {port}{pin})")

    # -- 3. Latch-low must bracket output mode, PER PORT --------------------
    # Checked per port, not globally: a BSRR write on a different port after
    # this port's MODER proves nothing about this port's glitch window.
    for port in sorted({p for p, _, _ in REQUIRED_PINS}):
        bsrr = [m.start() for m in re.finditer(rf"\b{port}\s*->\s*BSRR", body)]
        moder = [m.start() for m in re.finditer(rf"\b{port}\s*->\s*MODER", body)]
        if not bsrr or not moder:
            continue  # absence already reported by check 2
        if not any(b < min(moder) for b in bsrr):
            fail(f"{port}: must preload outputs low via BSRR BEFORE MODER "
                 f"(open glitch window on a safety output)")
        if not any(b > max(moder) for b in bsrr):
            fail(f"{port}: must re-assert BSRR AFTER MODER "
                 f"(defense in depth against partial peripheral init)")

    # -- 4. The TIM4 CH3 release must survive -------------------------------
    # The oracle releases TIM4 CH3 before reclaiming PB8 so no timer waveform
    # can outlive the call. Preserved even though what GP_OUT6 drives is
    # unverified: dropping it would be an unjustified deviation.
    if "TIM_CCER_CC3E" not in body:
        fail("primitive must disable TIM4 CH3 output compare before "
             "reclaiming PB8, or a timer waveform can survive the call")

    # -- 4b. Oracle write order: MODER before OTYPER, per port --------------
    # Reverted from an earlier reordering. Equivalence with the oracle is the
    # migration's bar; re-introducing the reorder needs a recorded deviation.
    for port in sorted({p for p, _, _ in REQUIRED_PINS}):
        moder = [m.start() for m in re.finditer(rf"\b{port}\s*->\s*MODER", body)]
        otyper = [m.start() for m in re.finditer(rf"\b{port}\s*->\s*OTYPER", body)]
        if moder and otyper and min(otyper) < min(moder):
            fail(f"{port}: OTYPER is written before MODER; the oracle order is "
                 f"MODER then OTYPER (see handoff 1.1)")

    # -- 5. Registered at PRE_KERNEL_1 --------------------------------------
    if not re.search(r"SYS_INIT\s*\([^)]*PRE_KERNEL_1", body):
        fail("primitive must be registered with SYS_INIT at PRE_KERNEL_1")

    # -- 6. No scheduler/logging/heap dependency in the fatal path ----------
    for banned in ("k_malloc", "k_mutex", "k_sem", "k_sleep", "LOG_INF",
                   "LOG_ERR", "printk", "gpio_pin_set"):
        if banned in body:
            fail(f"primitive must not depend on '{banned}': it runs "
                 f"pre-kernel and from fatal context")

    # -- 7. The app owns ordering, not mechanism ---------------------------
    fatal = repo / "app" / "src" / "ecu_fatal.c"
    if not fatal.is_file():
        fail("missing app/src/ecu_fatal.c fatal policy")
    else:
        ftext = strip_c_comments(fatal.read_text(encoding="utf-8"))
        if "k_sys_fatal_error_handler" not in ftext:
            fail("app must override k_sys_fatal_error_handler")
        # Order is checked inside the handler body only: an #include of
        # printk.h ahead of the function is not a diagnostic call.
        handler = ftext.find("k_sys_fatal_error_handler")
        body_start = ftext.find("{", handler) if handler != -1 else -1
        handler_body = ftext[body_start:] if body_start != -1 else ""
        # Stop at the next top-level closing brace.
        depth = 0
        for idx, ch in enumerate(handler_body):
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    handler_body = handler_body[: idx + 1]
                    break

        first_force = handler_body.find("ecu_force_safe_outputs_direct(")
        first_printk = handler_body.find("printk(")
        if first_force == -1:
            fail("fatal handler must force safe outputs")
        elif first_printk != -1 and first_printk < first_force:
            fail("fatal handler must drive outputs safe BEFORE any console I/O")

    if FAILURES:
        print(f"FAIL: {len(FAILURES)} safe-output contract violation(s)")
        for f in FAILURES:
            print(f"  - {f}")
        return 1

    print("PASS: DER26 ECU safe-output contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
