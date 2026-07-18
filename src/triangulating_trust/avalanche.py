"""
Avalanche / diffusion / differential characterisation of the Catalan-scheduled
permutation.  Produces exactly the numbers that go into Section VI-C.

Reports only what the sample size can resolve.  Nothing below the statistical
floor of the trial count is claimed.

python3 avalanche.py --trials 200000
"""
from __future__ import annotations
import argparse, secrets, statistics, math
from tt_tag import unpack, pack, permute, catalan_key_from_seed, absorb_block, tag

W = 32
STATE_BITS = 128
TAG_BITS = 16


def popcount(x: int) -> int:
    return bin(x).count("1")


def diffusion_by_round(trials: int, key: str, max_rounds: int):
    """Fraction of the 128 state bits affected by a single input-bit flip,
    as a function of the number of rounds applied."""
    rows = []
    for r in range(1, max_rounds + 1):
        affected = []
        for _ in range(trials):
            x = secrets.randbits(STATE_BITS)
            bit = secrets.randbelow(STATE_BITS)
            y = x ^ (1 << bit)
            a = pack(permute(unpack(x), key, "B", rounds=r))
            b = pack(permute(unpack(y), key, "B", rounds=r))
            affected.append(popcount(a ^ b))
        rows.append((r, statistics.mean(affected) / STATE_BITS))
    return rows


def sac_full_state(trials: int, key: str):
    """Strict avalanche: per-output-bit flip probability under a single
    input-bit flip, over the full schedule.  Returns mean and max deviation."""
    counts = [0] * STATE_BITS
    for _ in range(trials):
        x = secrets.randbits(STATE_BITS)
        bit = secrets.randbelow(STATE_BITS)
        d = pack(permute(unpack(x), key, "B")) ^ \
            pack(permute(unpack(x ^ (1 << bit)), key, "B"))
        for i in range(STATE_BITS):
            counts[i] += (d >> i) & 1
    probs = [c / trials for c in counts]
    return statistics.mean(probs), max(abs(p - 0.5) for p in probs), probs


def sac_tag(trials: int, R: int, key: str):
    """Same, but measured on the 16-bit tag under a single-bit flip of the
    PAYLOAD -- i.e. the property the security argument actually needs."""
    counts = [0] * TAG_BITS
    for _ in range(trials):
        ID = secrets.randbits(11)
        D = secrets.token_bytes(8)
        nc = secrets.randbits(32)
        bit = secrets.randbelow(64)
        Dp = (int.from_bytes(D, "big") ^ (1 << bit)).to_bytes(8, "big")
        d = tag(R, ID, D, nc, key) ^ tag(R, ID, Dp, nc, key)
        for i in range(TAG_BITS):
            counts[i] += (d >> i) & 1
    probs = [c / trials for c in counts]
    return statistics.mean(probs), max(abs(p - 0.5) for p in probs)


def best_single_bit_differential(trials_per_diff: int, R: int, key: str):
    """For every single-bit input difference in the payload, estimate the
    probability that the 16-bit tag difference is zero (a truncated-
    differential collision).  Random behaviour gives 2^-16 = 1.526e-5.

    NOTE ON RESOLUTION: with N trials per difference the smallest probability
    distinguishable from zero is ~1/N.  We therefore report the observed
    collision rate and the statistical floor, and make NO claim about
    differential probabilities below that floor.
    """
    worst = (None, 0.0)
    rates = []
    for bit in range(64):
        hits = 0
        for _ in range(trials_per_diff):
            ID = secrets.randbits(11)
            D = secrets.token_bytes(8)
            nc = secrets.randbits(32)
            Dp = (int.from_bytes(D, "big") ^ (1 << bit)).to_bytes(8, "big")
            if tag(R, ID, D, nc, key) == tag(R, ID, Dp, nc, key):
                hits += 1
        rate = hits / trials_per_diff
        rates.append(rate)
        if rate > worst[1]:
            worst = (bit, rate)
    return worst, statistics.mean(rates), 1.0 / trials_per_diff


def tag_collision_rate(trials: int, R: int, key: str):
    """Empirical collision rate of the 16-bit tag over random distinct
    messages.  Should match 2^-16."""
    hits = 0
    for _ in range(trials):
        ID = secrets.randbits(11)
        nc = secrets.randbits(32)
        D1, D2 = secrets.token_bytes(8), secrets.token_bytes(8)
        if D1 != D2 and tag(R, ID, D1, nc, key) == tag(R, ID, D2, nc, key):
            hits += 1
    return hits / trials


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=50000)
    a = ap.parse_args()
    T = a.trials

    R = secrets.randbits(128)
    K = catalan_key_from_seed(R, n=30)

    print("=" * 68)
    print(f"Catalan-scheduled permutation (variant B), |K| = {len(K)} rounds")
    print(f"trials = {T}")
    print("=" * 68)

    print("\n[1] Diffusion depth -- fraction of 128 state bits flipped")
    print("    by a single input-bit flip, vs. rounds applied\n")
    print("    rounds   frac. bits affected")
    full_round = None
    for r, frac in diffusion_by_round(max(2000, T // 25), K, 16):
        mark = ""
        if frac > 0.49 and full_round is None:
            full_round = r
            mark = "   <-- full diffusion"
        print(f"      {r:2d}     {frac:.4f}{mark}")
    print(f"\n    => full diffusion reached at round {full_round} "
          f"of {len(K)}")

    print("\n[2] Strict avalanche criterion, full 128-bit state, full schedule")
    mean_p, max_dev, _ = sac_full_state(max(2000, T // 25), K)
    print(f"    mean per-bit flip probability : {mean_p:.4f}")
    print(f"    max deviation from 0.5        : {max_dev:.4f}")

    print("\n[3] Strict avalanche of the 16-bit TAG under a single payload-bit flip")
    tmean, tmax = sac_tag(T, R, K)
    print(f"    mean per-bit flip probability : {tmean:.4f}")
    print(f"    max deviation from 0.5        : {tmax:.4f}")

    print("\n[4] Truncated-differential collision rate over all 64 single-bit")
    print("    payload differences (random reference = 2^-16 = 1.526e-05)")
    n_per = max(2000, T // 8)
    (wbit, wrate), meanrate, floor = best_single_bit_differential(n_per, R, K)
    print(f"    trials per difference         : {n_per}")
    print(f"    statistical floor (1/N)       : {floor:.3e}")
    print(f"    mean collision rate           : {meanrate:.3e}")
    print(f"    worst single-bit difference   : bit {wbit}, rate {wrate:.3e}")
    print(f"    ratio worst/random            : {wrate / (2**-16):.2f}x")

    print("\n[5] Tag collision rate, random distinct messages")
    cr = tag_collision_rate(T, R, K)
    print(f"    observed : {cr:.3e}")
    print(f"    expected : {2**-16:.3e}")

    print("\n" + "=" * 68)
    print("HONEST RESOLUTION NOTE: with N trials per difference the smallest")
    print(f"probability distinguishable from zero is ~{floor:.1e}.  No claim is")
    print("made about differential probabilities below that floor.  A bound of")
    print("the form 'max DP < 2^-38' is NOT obtainable by simulation and must")
    print("not be stated.")
    print("=" * 68)


if __name__ == "__main__":
    main()
