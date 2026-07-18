"""
Triangulating Trust -- reference implementation of the tag function.

Defines the two Catalan-scheduled ARX round functions, the keyed permutation,
and the t-bit tag.  Also contains bijectivity tests for candidate round pairs.

python3 tt_tag.py --selftest
"""
from __future__ import annotations
import argparse, itertools, secrets

M32 = 0xFFFFFFFF


def rotl(x: int, r: int, w: int = 32) -> int:
    r %= w
    m = (1 << w) - 1
    return ((x << r) | (x >> (w - r))) & m


# --------------------------------------------------------------------------
# Candidate A -- the round pair as originally drafted in the manuscript.
#   rho_open  : (s0,s1,s2,s3) -> (s1, s2 ^ rotl(s0,7), s3, s0 + s2)
#   rho_close : (s0,s1,s2,s3) -> (s3 + s1, s0, s1 ^ rotl(s3,11), s2)
# Bijectivity is NOT obvious; tested below on a reduced word size.
# --------------------------------------------------------------------------
def rho_open_A(s, w=32):
    m = (1 << w) - 1
    s0, s1, s2, s3 = s
    return (s1, (s2 ^ rotl(s0, 7, w)) & m, s3, (s0 + s2) & m)


def rho_close_A(s, w=32):
    m = (1 << w) - 1
    s0, s1, s2, s3 = s
    return ((s3 + s1) & m, s0, (s1 ^ rotl(s3, 11, w)) & m, s2)


# --------------------------------------------------------------------------
# Candidate B -- Type-1 generalised Feistel.  Invertible by construction:
# three words pass through unchanged, the fourth is XORed with a function of
# two others, so the preimage is recovered by re-applying the same F.
#   rho_open  : (s0,s1,s2,s3) -> (s1, s2, s3, s0 ^ rotl((s1 + s2) & M, 7))
#   rho_close : (s0,s1,s2,s3) -> (s1, s2, s3, s0 ^ rotl((s2 + s3) & M, 11))
# The two differ in their tap words and rotation, hence do not commute.
# --------------------------------------------------------------------------
def rho_open_B(s, w=32):
    m = (1 << w) - 1
    s0, s1, s2, s3 = s
    return (s1, s2, s3, s0 ^ rotl((s1 + s2) & m, 7, w))


def rho_close_B(s, w=32):
    m = (1 << w) - 1
    s0, s1, s2, s3 = s
    return (s1, s2, s3, s0 ^ rotl((s2 + s3) & m, 11, w))


ROUNDS = {"A": (rho_open_A, rho_close_A), "B": (rho_open_B, rho_close_B)}


# --------------------------------------------------------------------------
# State <-> integer packing
# --------------------------------------------------------------------------
def pack(s, w=32) -> int:
    v = 0
    for x in s:
        v = (v << w) | x
    return v


def unpack(v: int, w=32):
    m = (1 << w) - 1
    return tuple((v >> (w * i)) & m for i in (3, 2, 1, 0))


# --------------------------------------------------------------------------
# Catalan-key: a Dyck word.  Derived deterministically from the seed for the
# reference implementation; in deployment it comes from the Lukasiewicz
# encoding of DT(P).  Length 2n-1 with n=30 -> 59 symbols.
# --------------------------------------------------------------------------
def catalan_key_from_seed(seed: int, n: int = 30) -> str:
    """Deterministic Dyck-like schedule of length 2n-1 (n-1 opens, n closes)."""
    length = 2 * n - 1
    bits = [(seed >> i) & 1 for i in range(length)]
    opens, closes = n - 1, n
    out, o, c = [], 0, 0
    for b in bits:
        # keep the prefix property: never more closes than opens so far
        if o < opens and (b == 1 or c >= o):
            out.append("(")
            o += 1
        elif c < closes:
            out.append(")")
            c += 1
        else:
            out.append("(")
            o += 1
    return "".join(out)


# --------------------------------------------------------------------------
# Permutation and tag
# --------------------------------------------------------------------------
def permute(state, key: str, variant: str = "B", rounds: int | None = None, w=32):
    ro, rc = ROUNDS[variant]
    sched = key if rounds is None else key[:rounds]
    for sym in sched:
        state = ro(state, w) if sym == "(" else rc(state, w)
    return state


def tag(R: int, ID: int, D: bytes, nc: int, key: str, t: int = 16,
        variant: str = "B", rounds: int | None = None) -> int:
    """
    R  : 128-bit seed from the fuzzy extractor
    ID : 11-bit arbitration id
    D  : payload bytes
    nc : 32-bit nonce (16-bit counter || 16-bit epoch)
    """
    msg = absorb_block(ID, D, nc)
    st = unpack(R ^ msg)                       # Even-Mansour style: key XOR msg
    st = permute(st, key, variant, rounds)
    out = pack(st) ^ R                         # output whitening with the key
    return out & ((1 << t) - 1)


def absorb_block(ID: int, D: bytes, nc: int) -> int:
    """Inject (q_x, q_y, ID, nc) into a 128-bit block.  q_x/q_y are the
    geometric coordinates; here the lightweight XOR-fold hash of the paper."""
    qx = xorfold16(ID ^ (nc & 0xFFFF))
    qy = xorfold16(int.from_bytes(D, "big") ^ nc)
    return (qx << 112) | (qy << 96) | ((ID & 0x7FF) << 64) | \
           (nc << 32) | int.from_bytes(D[:4].ljust(4, b"\0"), "big")


def xorfold16(x: int) -> int:
    r = 0
    while x:
        r ^= x & 0xFFFF
        x >>= 16
    return r & 0xFFFF


def verify(R, ID, D, nc, sigma, key, t=16, variant="B") -> bool:
    return tag(R, ID, D, nc, key, t, variant) == sigma


# --------------------------------------------------------------------------
# Bijectivity test.  A round function on 4 w-bit words is a permutation iff it
# is injective on the full (2^w)^4 domain -- infeasible at w=32, so we test
# exhaustively at small w, which is decisive: the algebraic structure does not
# depend on the word size, only the rotation amounts do (taken mod w).
# --------------------------------------------------------------------------
def is_bijective(fn, w: int) -> bool:
    seen = set()
    dom = 1 << w
    for s in itertools.product(range(dom), repeat=4):
        y = fn(s, w)
        if y in seen:
            return False
        seen.add(y)
    return True


def selftest():
    print("Bijectivity of candidate round functions (exhaustive):")
    for w in (3, 4):
        for name, (ro, rc) in ROUNDS.items():
            b1 = is_bijective(ro, w)
            b2 = is_bijective(rc, w)
            print(f"  w={w}  variant {name}:  rho_open bijective={b1}   "
                  f"rho_close bijective={b2}")

    print("\nNon-commutativity (variant B, w=32):")
    ro, rc = ROUNDS["B"]
    s = tuple(secrets.randbits(32) for _ in range(4))
    print("  rho_open(rho_close(s)) == rho_close(rho_open(s)) ?",
          ro(rc(s)) == rc(ro(s)))

    print("\nTag sanity (variant B):")
    R = secrets.randbits(128)
    K = catalan_key_from_seed(R)
    print(f"  |K| = {len(K)}  key = {K}")
    sig = tag(R, 0x123, bytes([0x10, 0x20]), 5, K)
    print(f"  tag(ID=0x123, D=1020, nc=5) = 0x{sig:04x}")
    print("  verify (correct)  :", verify(R, 0x123, bytes([0x10, 0x20]), 5, sig, K))
    print("  verify (tampered) :", verify(R, 0x123, bytes([0x10, 0x21]), 5, sig, K))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest()
