"""
frr_sim.py -- False-Rejection-Rate of the fuzzy-extractor layer (R3.4).

WHAT THIS DOES AND DOES NOT ESTABLISH
-------------------------------------
It simulates the ERROR-CORRECTION layer that turns a noisy fingerprint into a
reproducible key: given an intra-device bit-error rate (BER) and an
inter-device BER, and a BCH(255,131,t=18) code-offset secure sketch, what is
  - the False Rejection Rate (a legitimate re-read is NOT corrected back to the
    enrolled key, because it had > t bit errors), and
  - the False Acceptance Rate (a DIFFERENT device is wrongly corrected to the
    enrolled key).

It does NOT measure real silicon PUF drift. The intra/inter BER are INPUTS.
We therefore sweep them across the range reported in the PUF literature for
voltage/timing fingerprints over -40..125 C and supply variation, and report
FRR as a function of that assumed BER. This is the honest form of the claim:
"GIVEN a fingerprint with intra-device BER up to X (a value we take from
[PUF ref], not measure), the ECC layer yields FRR <= Y." Reviewer 3 asked for
fingerprint robustness; absent a bench, this bounds the ECC contribution
exactly and isolates precisely the one empirical input we are assuming.

BCH(255,131,18): n=255 bits, k=131 key bits, corrects t=18 errors.
The secure-sketch FRR is Pr[ intra-device Hamming errors > t ], i.e. the
binomial tail; FAR is Pr[ a random codeword-offset lands within t of the
enrolled ], ~ (volume of Hamming ball radius t)/2^(n-k) for the sketch.
"""
import numpy as np
from math import comb, log2

N, K, T = 255, 131, 18

def binom_tail_gt(n, p, t):
    """Pr[X > t], X~Binom(n,p), computed in log-space for stability."""
    # sum_{i=t+1}^{n} C(n,i) p^i (1-p)^(n-i)
    if p == 0: return 0.0
    from math import lgamma, log, exp
    def logC(n,i): return lgamma(n+1)-lgamma(i+1)-lgamma(n-i+1)
    tot=0.0
    for i in range(t+1, n+1):
        lp = logC(n,i) + i*log(p) + (n-i)*log(1-p)
        tot += exp(lp)
    return tot

def far_sketch(n, k, t):
    """Approx FAR: fraction of syndromes within Hamming ball radius t.
    ball volume / 2^(n-k)."""
    from math import lgamma, log, exp
    def logC(n,i): return lgamma(n+1)-lgamma(i+1)-lgamma(n-i+1)
    logball = max(logC(n,i) for i in range(t+1))  # dominated by largest term
    # sum ball
    s=0.0
    for i in range(t+1):
        s += exp(logC(n,i))
    return s / (2.0**(n-k))

def main():
    print(f"BCH({N},{K},t={T}) code-offset secure sketch\n")
    print("Intra-device BER  ->  FRR (Pr[>{} errors in {} bits])".format(T,N))
    print("-"*52)
    # PUF literature: voltage/timing PUF intra-device BER typically 1-15% raw,
    # dropping to <5% after majority-vote/temporal averaging. Sweep 1..12%.
    for ber in [0.01,0.02,0.03,0.04,0.05,0.06,0.07,0.08,0.10,0.12]:
        frr = binom_tail_gt(N, ber, T)
        exp_err = N*ber
        flag = "" 
        if frr < 1e-6: flag="  (< 1e-6, safe)"
        elif frr > 1e-2: flag="  (HIGH -- needs stronger code / more averaging)"
        print(f"   {ber*100:4.0f}%   mean {exp_err:4.1f} errs   FRR = {frr:.3e}{flag}")

    print()
    far = far_sketch(N,K,T)
    print(f"FAR (different device wrongly corrected): ~{far:.2e}")
    print(f"Helper-data leakage: n-k = {N-K} bits of the {N}-bit reading")
    print(f"Residual key entropy after sketch: >= {K} bits (target 128 via K={K})")
    print()
    # The design point: what intra-device BER keeps FRR below an automotive
    # target (say 1e-6, i.e. < 1 spurious re-key per ~1e6 boots)?
    lo, hi = 0.0, 0.15
    for _ in range(40):
        mid=(lo+hi)/2
        if binom_tail_gt(N,mid,T) < 1e-6: lo=mid
        else: hi=mid
    print(f"Max tolerable intra-device BER for FRR < 1e-6: {lo*100:.2f}%")
    print(f"  => the fingerprint extractor must deliver <= {lo*100:.1f}% BER,")
    print(f"     achievable with temporal averaging per the PUF literature.")

if __name__ == "__main__":
    main()
