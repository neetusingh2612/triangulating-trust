# Triangulating Trust

Reference implementation and evaluation code for **"Triangulating Trust:
Delaunay-Based Geometric Keys for Sender Authentication in CAN Protocol"**.

The scheme derives a per-ECU key from a hardware fingerprint via a fuzzy
extractor, uses the resulting Catalan sequence as the *round schedule* of a
lightweight ARX permutation, and emits a 16-bit tag that fits inside the CAN
8-byte payload. The same geometric mapping is reused as a Voronoi-based
behavioural detector, so one pipeline provides both authentication and
intrusion detection.

Everything in this repository was used to produce numbers in the paper. Nothing
here is a mock-up.

---

## What is measured, and how

The paper deliberately separates four kinds of evidence. This repository keeps
them separate too, because they carry different weight:

| Evidence | Produced by | Strength |
|---|---|---|
| Analytic bounds (EUF-CMA, FRR) | `frr_sim.py`, proofs in the paper | Exact, given assumptions |
| Static cycle counts | `native/arm/` | Exact for branch-free code |
| Executed-instruction counts | `native/qemu/` | Exact; catches what static analysis misses |
| Hardware measurement | `firmware/` on an NXP S32K144 | Ground truth |
| Trace-driven detection | `src/triangulating_trust/` on ROAD | Real data, real attacks |

**Hardware-validated.** The per-frame path was measured on a two-node
S32K144 bench (Cortex-M4F, AEC-Q100) at 80 MHz: 602 cycles minimum, 605 mean
and 800 maximum under live bus load, against a 607-cycle static prediction.
Bus utilisation rose 25.1% → 31.2% with zero dropped frames over 6×10⁴
authenticated frames.

**Not validated.** The fingerprint bit-error rate is *not* measured here. The
FRR analysis is exact given a BER, and the BER is taken from the silicon-PUF
literature to size the code. Characterising it on a real CAN transceiver
remains open work. See `docs/REPRODUCING.md`.

---

## Quick start

```bash
git clone <your-fork-url> triangulating-trust
cd triangulating-trust
make install          # editable install + ML extras
make verify           # self-checks, no dataset needed (~30 s)
```

`make verify` prints the reference tag for a known input and the
fuzzy-extractor FRR table. If the tag differs from `0x7bc1` for the worked
example, something is wrong with your environment before anything else is
worth running.

```bash
make help             # all targets
```

---

## Repository layout

```
src/triangulating_trust/   Python: tag reference + evaluation harnesses
  tt_tag.py                the tag primitive (authoritative reference)
  mac_detection.py         real TT and HMAC-SHA1 tag verification on traces
  detection_eval.py        TT / HMAC vs RandomForest / LightGBM
  beta_containment.py      Voronoi containment rate under ECU capture
  frr_sim.py               fuzzy-extractor false-rejection analysis
  avalanche.py             diffusion / differential measurement

native/arm/                static ARM cycle counting (M0+/M3/M4)
native/qemu/               exact executed-instruction counting under QEMU
firmware/                  bench firmware (S32K144 + STM32F103)
formal/                    ProVerif models
results/                   JSON outputs from the runs reported in the paper
docs/REPRODUCING.md        paper claim -> exact command
```

---

## The dataset is not bundled

The ROAD corpus (Oak Ridge National Laboratory) is not redistributed here.
Download it, then point `DATA` at the directory containing the `.log` files:

```bash
make analysis DATA=/path/to/road
```

You need, at minimum:

- `ambient_dyno_drive_basic_short.log` — benign baseline
- `fuzzing_attack_1.log` — real fuzzing capture
- `capture_metadata.json` — injection intervals (ground truth for masquerade)
- `*_masquerade.log` — the 13 masquerade captures

Masquerade labels come from the dataset's own injection metadata (arbitration
ID plus time window), not from a heuristic.

---

## Running the pieces individually

```bash
# Voronoi containment rate under ECU capture (paper Table: beta)
python -m triangulating_trust.beta_containment --trace $DATA/ambient_dyno_drive_basic_short.log

# Real MAC verification: TT and HMAC-SHA1 tags computed and checked per frame
python -m triangulating_trust.mac_detection \
    --ambient $DATA/ambient_dyno_drive_basic_short.log \
    --masq_dir $DATA --meta $DATA/capture_metadata.json

# Detection comparison against learned baselines
python -m triangulating_trust.detection_eval \
    --ambient $DATA/ambient_dyno_drive_basic_short.log \
    --fuzzing $DATA/fuzzing_attack_1.log

# Fuzzy-extractor FRR (no dataset needed)
python -m triangulating_trust.frr_sim
```

`mac_detection.py` cross-checks its vectorised tag against the pure-Python
reference on 500 random inputs before touching any trace, and aborts on
mismatch. That check exists because it caught two real bugs during
development; do not remove it.

---

## Cycle counts

```bash
make cycles     # static, needs arm-none-eabi-gcc
make trace      # executed-instruction, needs qemu-system-arm + gdb-multiarch
```

Static counting is exact here because the Catalan schedule is fixed at
provisioning, so the round loop unrolls to branch-free, memory-free code.
QEMU does **not** model M-profile pipeline timing — `make trace` counts
*executed instructions* and applies ARM TRM costs; it is not a cycle-accurate
simulator, and the code says so.

The two disagree in one instructive place: an operation-count estimate of
HMAC-SHA1 understates it by 2.4×, because the message schedule spills. Only
execution reveals that.

---

## Bench firmware

```bash
make firmware BOARD=s32k144          # default, automotive-grade
make firmware BOARD=stm32f103        # cheaper alternative
make -C firmware selftest            # CAN loopback check -- RUN THIS FIRST
```

See `firmware/README.md` for the full bench procedure. Two things matter:

1. **Run the loopback self-test before trusting any measurement.** The S32K
   FlexCAN backend is written from the reference manual; every board-specific
   value is tagged `[VERIFY]`. If loopback fails, the configuration is wrong
   for your board revision and nothing downstream is valid.
2. **The default build uses α = 7/8** for the EMA (pure shifts). The reported
   605-cycle measurement was taken with α = 0.9, which needs a 64-bit divide;
   rebuild with `ALPHA09=1` to reproduce it exactly.

---

## Formal verification

```bash
make proverif    # needs ProVerif >= 2.05
```

Model A (honest parties) proves seed secrecy, group-key secrecy and agreement.
Injective agreement is **not** provable in ProVerif's Horn-clause abstraction
for one-pass counter freshness; we report that honestly rather than claiming
it. Model B confirms the group-key attribution gap under ECU capture, and that
revocation restores security.

---

## Citing

```bibtex
@article{singh2026triangulating,
  author  = {Singh, Neetu and Agarwal, Ritu},
  title   = {Triangulating Trust: Delaunay-Based Geometric Keys for
             Sender Authentication in {CAN} Protocol},
  journal = {Vehicular Communications},
  year    = {2026},
  note    = {Under review}
}
```

## License

MIT. See `LICENSE`.
