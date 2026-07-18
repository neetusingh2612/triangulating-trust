"""
beta_containment.py -- measures the residual guarantee under ECU capture
(Section VII-D, Theorem 2).

THE QUESTION
------------
An adversary has captured one ECU in broadcast domain G and therefore holds the
group key K_G.  All its tags verify.  Does the Voronoi layer still detect it
when it impersonates a DIFFERENT ECU in the same domain?

beta = Pr[ c_i in V(C_i) | frames actually sent by ECU j, relabelled as ECU i ]

beta is the MISS rate.  Low beta = the Voronoi layer is load-bearing.
High beta = the Voronoi layer is decorative and you should say so.

ECU IDENTITY
------------
On a CAN bus each arbitration ID has exactly one legitimate transmitter, so an
arbitration ID is a sound proxy for its sender ECU in the absence of a DBC.
If you have the ROAD DBC, pass --dbc to group IDs into real ECUs; the result is
strictly more faithful and you should report that version.

USAGE
-----
  # real data (ROAD ambient / candump .log, or CSV)
  python3 beta_containment.py --trace ambient_dyno_drive_basic_long.log

  # synthetic smoke test -- verifies the pipeline runs.  DO NOT PUT THESE
  # NUMBERS IN THE PAPER.
  python3 beta_containment.py --synthetic
"""
from __future__ import annotations
import argparse, csv, json, math, random, re, sys
from collections import defaultdict
import numpy as np

ALPHA = 0.9      # EMA smoothing, as in the paper
THETA = 0.1      # normalised-distance threshold, as in the paper


# ---------------------------------------------------------------- mapping
def xorfold16(x: int) -> int:
    r = 0
    while x:
        r ^= x & 0xFFFF
        x >>= 16
    return r & 0xFFFF


def map_frame(can_id: int, payload: bytes) -> tuple[float, float]:
    """q = (h(ID), h(D)) -- the same mapping used by the authentication layer,
    normalised to [0,1]^2 as stated in the Feature table."""
    qx = xorfold16(can_id) / 65535.0
    qy = xorfold16(int.from_bytes(payload, "big")) / 65535.0
    return qx, qy


# ---------------------------------------------------------------- parsing
CANDUMP = re.compile(r"\(([\d.]+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)")


def load_trace(path: str):
    """Yields (can_id:int, payload:bytes).  Supports candump .log and CSV."""
    frames = []
    with open(path, "r", errors="replace") as f:
        head = f.readline()
        f.seek(0)
        if "#" in head and "(" in head:                      # candump
            for line in f:
                m = CANDUMP.match(line.strip())
                if not m:
                    continue
                cid = int(m.group(3), 16)
                data = bytes.fromhex(m.group(4)) if m.group(4) else b""
                frames.append((cid, data.ljust(8, b"\0")[:8]))
        else:                                                # CSV
            rd = csv.DictReader(f)
            for row in rd:
                idk = next((k for k in row if k.lower() in
                            ("id", "can_id", "arbitration_id", "pid")), None)
                dk = next((k for k in row if k.lower() in
                           ("data", "payload", "d")), None)
                if idk is None:
                    sys.exit(f"cannot find an ID column in {path}; "
                             f"columns are {list(row)}")
                cid = int(str(row[idk]), 16 if not str(row[idk]).isdigit() else 10)
                raw = (row[dk] or "") if dk else ""
                raw = re.sub(r"[^0-9A-Fa-f]", "", raw)
                data = bytes.fromhex(raw) if len(raw) % 2 == 0 and raw else b""
                frames.append((cid, data.ljust(8, b"\0")[:8]))
    return frames


def synthetic_trace(n_ecu=10, per_ecu=4000, seed=7):
    """Smoke test only.  Each 'ECU' emits payloads from its own distribution,
    which is the OPTIMISTIC case for the detector -- real CAN payloads are far
    less separable.  Numbers from this are meaningless for the paper."""
    rng = random.Random(seed)
    frames = []
    for e in range(n_ecu):
        cid = 0x100 + e * 0x11
        base = rng.randrange(0, 1 << 40)
        for _ in range(per_ecu):
            jitter = rng.randrange(0, 1 << 12)
            frames.append((cid, ((base ^ jitter) & ((1 << 64) - 1))
                           .to_bytes(8, "big")))
    rng.shuffle(frames)
    return frames


# ---------------------------------------------------------------- experiment
def build_centroids(frames, split=0.5):
    """Benign enrolment: per-ECU (=per-ID) centroid C_i from the first `split`
    fraction of that ECU's frames."""
    by_id = defaultdict(list)
    for cid, d in frames:
        by_id[cid].append(map_frame(cid, d))
    centroids, holdout = {}, {}
    for cid, pts in by_id.items():
        if len(pts) < 40:
            continue                       # too few frames to characterise
        k = int(len(pts) * split)
        arr = np.array(pts[:k])
        centroids[cid] = arr.mean(axis=0)
        holdout[cid] = np.array(pts[k:])
    return centroids, holdout


def nearest(C_ids, C_arr, q):
    d = np.linalg.norm(C_arr - q, axis=1)
    return C_ids[int(np.argmin(d))]




def experiment(frames, bursts=(1, 5, 20), trials_per_pair=200, seed=11):
    rng = np.random.default_rng(seed)
    centroids, holdout = build_centroids(frames)
    ids = sorted(centroids)
    if len(ids) < 2:
        sys.exit("need at least 2 IDs with enough frames")
    C_arr = np.array([centroids[i] for i in ids])
    scale = max(np.linalg.norm(C_arr, axis=1).max(), 1e-9)

    results = {}
    for burst in bursts:
        contained = 0
        total = 0
        dists = []
        for a, i in enumerate(ids):                  # impersonated ECU
            C_i = centroids[i]
            for b, j in enumerate(ids):              # capturing ECU
                if i == j:
                    continue
                src = holdout[j]
                if len(src) < burst:
                    continue
                for _ in range(trials_per_pair):
                    # ECU j's payloads, retransmitted under ECU i's ID.
                    # q_x depends on ID (=i), q_y on the payload (=j's).
                    k = int(rng.integers(0, len(src) - burst + 1))
                    payload_qy = src[k:k + burst, 1]
                    qx_i = C_i[0] * 0 + map_frame(i, b"\0" * 8)[0]
                    q = np.stack([np.full(burst, qx_i), payload_qy], axis=1)

                    c = C_i.copy()
                    for qq in q:
                        c = ALPHA * c + (1 - ALPHA) * qq
                    d = np.linalg.norm(c - C_i) / scale
                    nn = ids[int(np.argmin(np.linalg.norm(C_arr - c, axis=1)))]
                    # "contained" = evades BOTH the distance gate and the
                    # Voronoi cell query, i.e. a MISS
                    miss = (d <= THETA) and (nn == i)
                    contained += int(miss)
                    total += 1
                    dists.append(d)
        beta = contained / total if total else float("nan")
        results[burst] = dict(beta=beta, n=total,
                              mean_dist=float(np.mean(dists)),
                              detect=1 - beta)
    return ids, results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace")
    ap.add_argument("--synthetic", action="store_true")
    ap.add_argument("--trials", type=int, default=200)
    ap.add_argument("--out", default="beta_results.json")
    a = ap.parse_args()

    if a.synthetic:
        print("!! SYNTHETIC SMOKE TEST -- these numbers are NOT publishable !!\n")
        frames = synthetic_trace()
    elif a.trace:
        frames = load_trace(a.trace)
    else:
        sys.exit("pass --trace <file> or --synthetic")

    print(f"frames loaded: {len(frames):,}")
    ids, res = experiment(frames, trials_per_pair=a.trials)
    print(f"ECUs (distinct arbitration IDs with >=40 frames): {len(ids)}\n")

    print(f"{'burst k':>8} {'trials':>9} {'beta (miss)':>13} {'detect':>9} "
          f"{'mean norm dist':>15}")
    for k, r in res.items():
        print(f"{k:>8} {r['n']:>9,} {r['beta']:>13.4f} {r['detect']:>9.4f} "
              f"{r['mean_dist']:>15.4f}")

    with open(a.out, "w") as f:
        json.dump({"ids": len(ids), "results": res,
                   "synthetic": bool(a.synthetic)}, f, indent=2)
    print(f"\nwritten to {a.out}")
    if a.synthetic:
        print("\nREMINDER: run this on the real ROAD trace before citing any "
              "beta value.")


if __name__ == "__main__":
    main()
