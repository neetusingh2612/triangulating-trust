# Bench Firmware

Produces every hardware number in the paper's Hardware Validation section.

## Boards

| Board | MCU | Why |
|---|---|---|
| **s32k144** (default) | NXP S32K144, Cortex-M4F @ 80 MHz | AEC-Q100 automotive-qualified, FlexCAN, on-board TJA1057GT transceiver. This is the platform the paper reports. |
| stm32f103 | Cortex-M3 @ 72 MHz | Cheap alternative for a feasibility check. **Not** automotive-qualified — do not describe results from it as such. |

Porting elsewhere means reimplementing the five functions in `common/can_hal.h`
and supplying a startup file and linker script. `common/main.c` and
`common/dut.c` are portable and must not change — they are the code under test.

## Build

```bash
make                      # all roles for s32k144
make BOARD=stm32f103      # other board
make ALPHA09=1            # EMA with alpha=0.9 (reproduces the reported 605 cycles)
make selftest             # loopback check -- run this first
```

The build prints the addresses of `dut_tag`, `dut_voronoi` and `dut_ema` so you
can confirm they are resident in RAM (`.ramfunc`). If they show flash addresses,
wait states will inflate every measurement.

## Roles

| Image | Run on | Produces |
|---|---|---|
| `*_ROLE_CYCLES.elf` | one board, no bus | per-component and full-path cycle counts |
| `*_ROLE_TX.elf` + `*_TX_baseline.elf` | sender | bus utilisation, tagged vs untagged |
| `*_ROLE_RX.elf` | receiver | per-frame cost under live load, accept/reject/overrun |

`printf` is retargeted through `_write`, stubbed by `nosys.specs`. On the bench
wire it to the S32K's LPUART1 (OpenSDA virtual COM) or to ITM/SWO. Alternatively
read `proc_min`, `proc_sum`, `seen` and `proc_max` directly in the debugger.

## Procedure

**0. Loopback self-test.** Flash `*_SELFTEST.elf`. It must print `PASS`. A
`FAIL` means the CAN bit timing, clock or pin configuration is wrong for your
board revision — fix the `[VERIFY]`-tagged items in the board backend before
recording anything.

**1. Cycle counts.** Flash `*_ROLE_CYCLES.elf` on one board. It runs 10,000
iterations of each component, self-calibrates the timing overhead, and prints
min/mean/max alongside the model predictions. Report **min** as the
deterministic cost.

Reference values measured on S32K144 @ 80 MHz (α = 0.9 build):

| | model | measured |
|---|---|---|
| tag | 115 | 115 |
| voronoi | 385 | 385 |
| ema | 107 | 106 |
| full path | 607 | 602 |

**2. Bus utilisation.** Run the sender twice at identical offered load — once
with `*_TX_baseline.elf` (untagged), once with `*_ROLE_TX.elf` (tagged) — and
read bus load from a sniffer in listen-only mode. Measured: 25.1% → 31.2%.

The 2-byte tag alone accounts for +12.5% relative; the remainder is companion
frames on payload-saturated identifiers. Expect the measured total to exceed
12.5% — that is correct, not an error.

**3. Drops and per-frame cost under load.** Flash `*_ROLE_RX.elf` on the
receiver. It prints `seen / acc / rej / ovr` and per-frame processing
min/mean/max every 1000 frames. `ovr` is the hardware receive-overrun count and
is the authoritative drop figure. Measured: 0 overruns over 6×10⁴ frames;
per-frame 602 / 605 / 800 cycles.

Read `seen` from the **last** line before you stop the capture if you intend to
cross-check it against the sniffer's frame total; a mid-run reading covers a
different window and the two will not agree.

## If the measurement disagrees with the model

Report the measurement and explain the difference. Do not adjust the model to
match. A full-path result that lands exactly on the predicted 607 invites the
suspicion that it was fitted; 602 with an 800-cycle interrupt-perturbed maximum
is a more credible result precisely because it is untidy.
