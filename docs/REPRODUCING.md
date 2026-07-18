# Reproducing the Paper

Every quantitative claim, the command that produces it, and what it depends on.

Set `DATA` to the directory holding the ROAD `.log` files:

```bash
export DATA=/path/to/road
```

---

## No dataset required

| Claim | Command | Expected |
|---|---|---|
| Tag reference vector | `make verify` | schedule length 59, tag `0x7bc1` |
| FRR vs BER (Table: frr) | `python -m triangulating_trust.frr_sim` | FRR < 1e-6 requires BER ≤ 1.97% |
| Static cycle counts (Table: lowend) | `make cycles` | tag 223/115/115, voronoi 508/385/385 (M0+/M3/M4) |
| Executed-instruction counts | `make trace` | HMAC 13,226 insn / 18,661 cyc; TT tag 114 / 125 |

The HMAC figure is the one worth attention: an operation-count estimate gives
~6,900, which is 2.4× low. The message schedule spills to RAM and only
execution exposes it.

---

## Dataset required

| Claim | Command |
|---|---|
| Containment rate β (Table: beta) | `python -m triangulating_trust.beta_containment --trace $DATA/ambient_dyno_drive_basic_short.log` |
| Real MAC verification, masquerade | `python -m triangulating_trust.mac_detection --ambient $DATA/ambient_dyno_drive_basic_short.log --masq_dir $DATA --meta $DATA/capture_metadata.json` |
| Detection vs learned baselines | `python -m triangulating_trust.detection_eval --ambient $DATA/ambient_dyno_drive_basic_short.log --fuzzing $DATA/fuzzing_attack_1.log` |
| Diffusion / avalanche | `python -m triangulating_trust.avalanche` |

Or all at once: `make analysis DATA=$DATA`.

Expected headline values:

- β within a domain: 0.529 / 0.173 / 0.050 at burst k = 1 / 5 / 20
- Masquerade, TT: 1 miss in 30,728 injections (recall 0.99997)
- Masquerade, HMAC: 1 miss in 7,974 (recall 0.99988) — **the same guarantee**
- Forgery, learned baselines: LightGBM F1 = 0.51, RandomForest F1 = 0.18

Note the second and third rows. TT does not beat HMAC and cannot: both are
16-bit MACs with the same 2⁻ᵗ bound. The efficiency difference is the claim,
not a detection difference.

---

## Hardware required

See `firmware/README.md`. Two S32K144 boards, a CAN transceiver pair, and a
sniffer in listen-only mode.

| Claim | Source |
|---|---|
| Per-frame 602/605/800 cycles under load | `ROLE_RX` console |
| Per-component 115 / 385 / 106 | `ROLE_CYCLES` console |
| Bus 25.1% → 31.2% | sniffer, paired TX runs |
| Zero drops over 6×10⁴ frames | `ROLE_RX`, `ovr` counter |

---

## What this repository does not reproduce

**Fingerprint bit-error rate.** The FRR analysis is exact *given* a BER, and
`frr_sim.py` derives the design constraint (BER ≤ 1.97% for FRR < 1e-6). The
BER itself is a property of the analogue fingerprint circuit and is taken from
the silicon-PUF literature to size the BCH code. It is not measured here, and
the CAN traces contain no analogue voltage data from which it could be.

This is stated in the paper's Threats to Validity and is genuine open work.
A CAN-transceiver fingerprint is not an engineered CMOS PUF and there is no
reason to assume they share a BER.

**Injective agreement.** Not provable in ProVerif's abstraction for one-pass
counter freshness. See `formal/README.md`.

**Multi-node scaling.** The bench is two nodes. A production bus carries tens
of ECUs with vehicle-length wiring and mixed transceiver vendors.

---

## Determinism

All randomised harnesses fix `seed = 42` (`numpy.random.default_rng(42)` and
scikit-learn `random_state=42`). Reruns on identical input reproduce identical
output. The one measurement that legitimately varies run-to-run is the
`max` per-frame cycle count under bus load, which depends on where interrupts
land; `min` is stable.
