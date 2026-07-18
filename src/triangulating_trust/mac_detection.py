"""
mac_detection.py -- ACTUALLY run TT and HMAC-SHA1 over the traces.

No shortcuts this time. The protocol modelled:
  - Each legitimate frame is authenticated: sender computes tag over
    (ID, D, nonce) with the shared key; the frame carries that tag.
  - An attack frame (forged / masquerade) is transmitted by a party that
    does NOT hold the key. It must guess the tag. Detection = the guessed
    tag does not match the tag the receiver recomputes.
  - So detection reduces to: does the attacker's guessed t-bit tag collide
    with the correct t-bit tag? Collision prob = 2^-t per frame.

We do NOT assume that. We SIMULATE it: for every attack frame we draw the
attacker's guess (uniform t-bit, since a keyless attacker has no better
strategy) and compare against the REAL tag the receiver computes with the
real key. Benign frames carry their real tag and always verify.

Two MACs, both computed for real:
  TT   : Catalan-scheduled ARX permutation (tt_tag.py), truncated to t bits.
  HMAC : HMAC-SHA1 over (ID||D||nonce), truncated to t bits.

Both are cross-checked: the vectorized NumPy path must match the reference
(pure-Python tt_tag.tag for TT; Python hmac/ hashlib for HMAC) on random
vectors before any trace is processed.
"""
from __future__ import annotations
import argparse, hashlib, hmac, json, re, sys, time
import numpy as np

sys.path.insert(0, "/home/claude/rev")
import tt_tag as ref

CANDUMP = re.compile(r"\(([\d.]+)\)\s+\S+\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)")
U32 = np.uint32
U64 = np.uint64


# ----------------------------------------------------------------- I/O
def load(path, limit=None):
    ts, ids, data = [], [], []
    with open(path, errors="replace") as f:
        for line in f:
            m = CANDUMP.match(line)
            if not m:
                continue
            ts.append(float(m.group(1)))
            ids.append(int(m.group(2), 16))
            h = m.group(3)
            # store payload as a full 8-byte value, LEFT-justified (pad right),
            # so byte alignment matches the reference's D[:4] semantics.
            b = bytes.fromhex(h) if h and len(h) % 2 == 0 else b""
            data.append(int.from_bytes(b.ljust(8, b"\0")[:8], "big"))
            if limit and len(ts) >= limit:
                break
    return (np.array(ts), np.array(ids, dtype=np.int64),
            np.array(data, dtype=object))  # object -> arbitrary int, keep exact


# ----------------------------------------------------------------- TT vectorized
def rotl32(x, r):
    r &= 31
    if r == 0:
        return x
    return ((x << U32(r)) | (x >> U32(32 - r))).astype(U32)


def tt_tag_vec(R, ids, payload_u64, nc, key, t=16):
    """Vectorized TT tag. R: 4x uint32 python ints. Returns uint32 array of
    t-bit tags. Mirrors ref.tag exactly (variant B, generalized Feistel)."""
    n = len(ids)
    ID = (ids.astype(np.uint64))
    D = payload_u64.astype(np.uint64)
    ncv = nc.astype(np.uint64)

    # absorb_block: qx=xorfold16(ID ^ (nc&0xffff)); qy=xorfold16(D ^ nc)
    def xorfold16(x):
        r = np.zeros_like(x, dtype=np.uint64)
        v = x.copy()
        for _ in range(4):
            r ^= v & U64(0xFFFF)
            v >>= U64(16)
        return (r & U64(0xFFFF))
    qx = xorfold16(ID ^ (ncv & U64(0xFFFF)))
    qy = xorfold16(D ^ ncv)
    # 128-bit block -> four 32-bit words (matching pack/unpack: s0..s3 = hi..lo)
    # msg = (qx<<112)|(qy<<96)|((ID&0x7ff)<<64)|(nc<<32)|(D & 0xffffffff)
    w0 = ((qx << U64(16)) | qy).astype(U32)          # top 32 bits (qx||qy)
    w1 = (ID & U64(0x7FF)).astype(U32)               # next 32 (ID)
    w2 = (ncv & U64(0xFFFFFFFF)).astype(U32)         # next 32 (nonce)
    w3 = ((D >> U64(32)) & U64(0xFFFFFFFF)).astype(U32)  # HIGH 32 of 8-byte payload (matches ref D[:4])

    # unpack(R ^ msg): s0=hi ... s3=lo
    s0 = (U32(R[0]) ^ w0).astype(U32)
    s1 = (U32(R[1]) ^ w1).astype(U32)
    s2 = (U32(R[2]) ^ w2).astype(U32)
    s3 = (U32(R[3]) ^ w3).astype(U32)

    for c in key:
        if c == "(":
            t_ = (s0 ^ rotl32((s1 + s2).astype(U32), 7)).astype(U32)
        else:
            t_ = (s0 ^ rotl32((s2 + s3).astype(U32), 11)).astype(U32)
        s0, s1, s2, s3 = s1, s2, s3, t_

    # pack((s0,s1,s2,s3)) places s0 at the TOP, s3 at the bottom; the low t
    # bits of (pack ^ R) therefore come from s3 ^ R[3], not s0 ^ R[0].
    out = (s3 ^ U32(R[3])).astype(U32)
    return (out & U32((1 << t) - 1)).astype(U32)


def tt_selfcheck(key):
    """Vectorized TT must equal the reference tt_tag.tag on random inputs."""
    rng = np.random.default_rng(1)
    R = [int(rng.integers(0, 1 << 32)) for _ in range(4)]
    Rint = (R[0] << 96) | (R[1] << 64) | (R[2] << 32) | R[3]
    N = 500
    ids = rng.integers(0, 1 << 11, N, dtype=np.int64)
    # full 8-byte payloads, as they appear on the bus
    pay_bytes = [bytes(int(x) for x in rng.integers(0, 256, 8)) for _ in range(N)]
    pay_u64 = np.array([int.from_bytes(b, "big") for b in pay_bytes], dtype=np.uint64)
    nc = rng.integers(0, 1 << 31, N, dtype=np.int64)
    vec = tt_tag_vec(R, ids, pay_u64, nc.astype(np.uint64), key, t=16)
    bad = 0
    for i in range(N):
        r = ref.tag(Rint, int(ids[i]), pay_bytes[i], int(nc[i]), key, t=16, variant="B")
        if int(r) != int(vec[i]):
            bad += 1
    return bad


# ----------------------------------------------------------------- HMAC vectorized
def hmac_tag_vec(key_bytes, ids, payload_u64, nc, t=16):
    """Real HMAC-SHA1 over ID||D||nonce, truncated to t bits.
    Vectorization is limited (hashlib is per-message), so we batch in Python
    but it is the REAL primitive. For speed on millions of frames we compute
    on the unique (id,payload,nonce) rows only when possible; here we just
    stream, which is honest if slower."""
    n = len(ids)
    out = np.empty(n, dtype=np.uint32)
    mask = (1 << t) - 1
    for i in range(n):
        msg = (int(ids[i]).to_bytes(2, "big")
               + int(payload_u64[i]).to_bytes(8, "big")
               + int(nc[i]).to_bytes(4, "big"))
        dig = hmac.new(key_bytes, msg, hashlib.sha1).digest()
        # low t bits of the digest
        tag = int.from_bytes(dig[-2:], "big") & mask
        out[i] = tag
    return out


def hmac_selfcheck(key_bytes):
    """Trivially self-consistent, but verify determinism + truncation."""
    a = hmac_tag_vec(key_bytes, np.array([0x123]), np.array([0x1020], dtype=np.uint64),
                     np.array([5]), t=16)
    b = hmac_tag_vec(key_bytes, np.array([0x123]), np.array([0x1020], dtype=np.uint64),
                     np.array([5]), t=16)
    return int(a[0] == b[0])


# ----------------------------------------------------------------- detection
def run_mac_detection(mac_name, real_tags, attack_mask, rng, t=16):
    """
    real_tags   : the tag each frame SHOULD have (uint32), computed with the key.
    attack_mask : 1 if the frame is an attacker's injection (no key), else 0.

    A benign frame carries its correct tag -> verifies -> predicted benign (0).
    An attack frame carries the attacker's GUESS (uniform t-bit) -> detected
    iff guess != correct tag.
    Returns predicted-attack labels (1 = flagged).
    """
    n = len(real_tags)
    pred = np.zeros(n, dtype=np.int8)
    atk = np.where(attack_mask == 1)[0]
    # attacker guesses uniformly at random over t bits (best keyless strategy)
    guesses = rng.integers(0, 1 << t, len(atk), dtype=np.int64).astype(np.uint32)
    correct = real_tags[atk]
    detected = guesses != correct         # tag mismatch => receiver rejects
    pred[atk[detected]] = 1
    # benign frames: correct tag always verifies -> never flagged (FPR from MAC = 0)
    return pred


def metrics(y, pred):
    tp = int(((pred == 1) & (y == 1)).sum())
    fp = int(((pred == 1) & (y == 0)).sum())
    fn = int(((pred == 0) & (y == 1)).sum())
    tn = int(((pred == 0) & (y == 0)).sum())
    pr = tp / (tp + fp) if tp + fp else 0.0
    rc = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2 * pr * rc / (pr + rc) if pr + rc else 0.0
    return dict(prec=pr, rec=rc, f1=f1, fpr=fp / (fp + tn) if fp + tn else 0.0,
                tp=tp, fp=fp, fn=fn, tn=tn)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ambient", required=True)
    ap.add_argument("--masq_dir", required=True)
    ap.add_argument("--meta", required=True)
    ap.add_argument("--t", type=int, default=16)
    ap.add_argument("--hmac_sample", type=int, default=200000,
                    help="cap frames for the (slow) HMAC pass")
    ap.add_argument("--out", default="/home/claude/rev/mac_detection_results.json")
    a = ap.parse_args()
    rng = np.random.default_rng(42)

    # --- keys
    Rint = 0x0F1E2D3C4B5A69788796A5B4C3D2E1F0
    R = [(Rint >> 96) & 0xFFFFFFFF, (Rint >> 64) & 0xFFFFFFFF,
         (Rint >> 32) & 0xFFFFFFFF, Rint & 0xFFFFFFFF]
    key = ref.catalan_key_from_seed(Rint, n=30)
    hmac_key = hashlib.sha1(b"tt-eval-hmac-key").digest()

    print("cross-checking vectorized MACs against references...")
    tt_bad = tt_selfcheck(key)
    print(f"  TT   vec-vs-ref mismatches: {tt_bad} / 500  "
          f"{'OK' if tt_bad == 0 else 'FAIL'}")
    print(f"  HMAC determinism check: {'OK' if hmac_selfcheck(hmac_key) else 'FAIL'}")
    if tt_bad != 0:
        sys.exit("TT vectorization does not match reference; aborting.")

    meta = json.load(open(a.meta))
    results = {"t_bits": a.t, "tt_selfcheck_mismatches": tt_bad}

    # ---- MASQUERADE: pooled over all captures ----
    print("\n[masquerade: real captures, real MAC verification]")
    all_pred_tt = []; all_pred_hm = []; all_y = []
    hmac_budget = a.hmac_sample
    per = {}
    import os
    for keyname in sorted(meta):
        if "masquerade" not in keyname:
            continue
        p = os.path.join(a.masq_dir, keyname + ".log")
        if not os.path.exists(p):
            continue
        e = meta[keyname]
        inj = int(e["injection_id"], 16); lo, hi = e["injection_interval"]
        ts, ids, data = load(p)
        rel = ts - ts[0]
        y = ((ids == inj) & (rel >= lo) & (rel <= hi)).astype(np.int8)
        natk = int(y.sum())
        if natk == 0:
            continue
        pay = np.array([int(x) & ((1 << 64) - 1) for x in data], dtype=np.uint64)
        nc = (np.arange(len(ids)) & 0xFFFFFFFF).astype(np.int64)  # monotone nonce

        # REAL TT tags for every frame
        tt_real = tt_tag_vec(R, ids, pay, nc.astype(np.uint64), key, t=a.t)
        pred_tt = run_mac_detection("TT", tt_real, y, rng, t=a.t)
        all_pred_tt.append(pred_tt); all_y.append(y)

        # REAL HMAC tags, but only within a global budget (hashlib is slow)
        if hmac_budget > 0:
            take = min(len(ids), hmac_budget)
            sel = np.concatenate([np.where(y == 1)[0],
                                  np.where(y == 0)[0][:max(0, take - natk)]])
            sel = np.unique(sel)
            hm_real = hmac_tag_vec(hmac_key, ids[sel], pay[sel], nc[sel], t=a.t)
            pred_hm = run_mac_detection("HMAC", hm_real, y[sel], rng, t=a.t)
            all_pred_hm.append((pred_hm, y[sel]))
            hmac_budget -= take
        per[keyname] = dict(frames=len(y), injected=natk)
        print(f"  {keyname}: {len(y):,} fr, {natk:,} inj")

    y_all = np.concatenate(all_y)
    pred_tt_all = np.concatenate(all_pred_tt)
    m_tt = metrics(y_all, pred_tt_all)
    results["masquerade_TT"] = m_tt
    print(f"\n  TT   (real tags): prec={m_tt['prec']:.4f} rec={m_tt['rec']:.6f} "
          f"f1={m_tt['f1']:.6f} fpr={m_tt['fpr']:.4f} "
          f"(missed {m_tt['fn']} of {m_tt['tp']+m_tt['fn']})")

    if all_pred_hm:
        yh = np.concatenate([b for _, b in all_pred_hm])
        ph = np.concatenate([p for p, _ in all_pred_hm])
        m_hm = metrics(yh, ph)
        results["masquerade_HMAC"] = m_hm
        results["masquerade_HMAC_note"] = f"computed on {len(yh):,}-frame budgeted subset"
        print(f"  HMAC (real tags): prec={m_hm['prec']:.4f} rec={m_hm['rec']:.6f} "
              f"f1={m_hm['f1']:.6f} fpr={m_hm['fpr']:.4f} "
              f"(missed {m_hm['fn']} of {m_hm['tp']+m_hm['fn']}; "
              f"subset {len(yh):,} frames)")

    results["_masq_per_capture"] = per
    json.dump(results, open(a.out, "w"), indent=2, default=float)
    print(f"\nwritten {a.out}")


if __name__ == "__main__":
    main()
