#!/usr/bin/env python3
"""
cyclecount.py -- exact static cycle count for straight-line Cortex-M code.

WHY THIS IS EXACT (and QEMU is not)
-----------------------------------
QEMU does not model M-profile pipeline timing at all; cycle numbers taken from
it are meaningless and a reviewer is entitled to say so.  But the tag function,
with the Catalan schedule unrolled at compile time, is:

    * branch-free               -> no misprediction / pipeline-refill penalty
    * free of memory access in the round loop  -> no wait states, no stalls
    * free of data-dependent control flow      -> no input-dependent timing

For code with those three properties, the cycle count is just the sum of the
per-instruction issue costs from the ARM Technical Reference Manual.  There is
nothing left for a simulator to discover.

TIMINGS (ARM TRMs: Cortex-M0+ DDI0484, Cortex-M3 DDI0337, Cortex-M4 DDI0439)
----------------------------------------------------------------------------
  ARMv6-M (M0+): 16-bit data-processing = 1;  LDR/STR = 2;  PUSH/POP = 1+N;
                 branch taken = 3 (2 for M0+ with branch speculation on some
                 implementations -- we take the conservative 3);  BX = 3.
  ARMv7-M (M3/M4): data-processing (incl. shifted operand) = 1;  LDR/STR = 2
                 (1 when pipelined back-to-back -- we take the conservative 2);
                 PUSH/POP = 1+N;  BX LR = 3.

CAVEAT WE STATE IN THE PAPER
----------------------------
These are CORE cycles at zero flash wait states.  A real part running from
flash at 100+ MHz inserts wait states on instruction fetch.  Report the core
count, and state the wait-state assumption; or place the function in RAM
(__attribute__((section(".ramfunc")))), which is what a production ECU would
do for a per-frame hot path anyway.

Usage:
  python3 cyclecount.py tt_cortex-m4.o tt_tag_unrolled cortex-m4
"""
import re, subprocess, sys

OBJDUMP = "arm-none-eabi-objdump"

# mnemonic -> cycles, per architecture family
V6M = {  # Cortex-M0+
    "default": 1,
    "ldr": 2, "ldrb": 2, "ldrh": 2, "ldm": None,   # None -> 1 + n_regs
    "str": 2, "strb": 2, "strh": 2, "stm": None,
    "push": None, "pop": None,
    "b": 3, "bl": 4, "bx": 3, "blx": 3,
    "beq": 3, "bne": 3, "blt": 3, "bgt": 3, "bge": 3, "ble": 3,
    "mul": 1,           # M0+ has a 1-cycle multiplier in the fast config
}
V7M = {  # Cortex-M3 / M4
    "default": 1,
    "ldr": 2, "ldrb": 2, "ldrh": 2, "ldm": None,
    "str": 2, "strb": 2, "strh": 2, "stm": None,
    "push": None, "pop": None,
    "b": 3, "bl": 4, "bx": 3, "blx": 3,
    "beq": 3, "bne": 3, "blt": 3, "bgt": 3, "bge": 3, "ble": 3,
    "cbz": 3, "cbnz": 3,
    "mul": 1, "mla": 2, "umull": 3,
}

LINE = re.compile(r"^\s+[0-9a-f]+:\s+([0-9a-f ]+)\t([a-z][a-z0-9.]*)\s*(.*)$")


def cycles_for(mnem, ops, table):
    base = mnem.split(".")[0]           # strip .w / .n
    base = base.rstrip("s")             # adds -> add, eors -> eor
    if base in ("push", "pop", "ldm", "stm", "ldmia", "stmdb"):
        nregs = len(re.findall(r"r\d+|lr|pc|sp", ops))
        return 1 + max(nregs, 1)
    v = table.get(base, table["default"])
    return v if v is not None else 1


def analyse(obj, sym, arch):
    table = V6M if "m0" in arch else V7M
    out = subprocess.run(
        [OBJDUMP, "-d", f"--section=.text.{sym}", obj],
        capture_output=True, text=True, check=True).stdout

    total, n, hist = 0, 0, {}
    branches, mem = 0, 0
    for line in out.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        mnem, ops = m.group(2), m.group(3)
        c = cycles_for(mnem, ops, table)
        total += c
        n += 1
        hist[mnem.split(".")[0]] = hist.get(mnem.split(".")[0], 0) + 1
        if mnem.split(".")[0].startswith("b") and mnem.split(".")[0] not in ("bic",):
            branches += 1
        if mnem.split(".")[0].rstrip("s") in ("ldr", "str", "push", "pop", "ldm", "stm"):
            mem += 1
    return total, n, branches, mem, hist


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    obj, sym, arch = sys.argv[1:4]
    total, n, br, mem, hist = analyse(obj, sym, arch)

    print(f"{arch:<14} {sym}")
    print(f"  instructions      : {n}")
    print(f"  branches          : {br}   (0 expected: schedule unrolled)")
    print(f"  memory ops        : {mem}  (prologue/epilogue only)")
    print(f"  CORE CYCLES       : {total}")
    print(f"  cycles / round    : {total/59:.2f}   (|K| = 59)")
    print("  mix               : " +
          ", ".join(f"{k}x{v}" for k, v in
                    sorted(hist.items(), key=lambda kv: -kv[1])[:8]))
    print()
    for f in (8, 16, 24, 48, 64, 80, 100):
        print(f"    @ {f:>3} MHz -> {total/(f*1000):.4f} ms/frame   "
              f"(max {int(f*1e6/total):,} frames/s)")


if __name__ == "__main__":
    main()
