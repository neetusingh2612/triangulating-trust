"""Tests for the fuzzy-extractor false-rejection analysis."""
from triangulating_trust.frr_sim import binom_tail_gt, N, T


def test_frr_monotonic_in_ber():
    """Higher bit-error rate must give higher false-rejection rate."""
    prev = -1.0
    for ber in (0.005, 0.01, 0.02, 0.03, 0.05, 0.08):
        f = binom_tail_gt(N, ber, T)
        assert f > prev
        prev = f


def test_frr_design_point():
    """The paper's design constraint: BER <= ~2% for FRR < 1e-6."""
    assert binom_tail_gt(N, 0.0196, T) < 1e-6
    assert binom_tail_gt(N, 0.0198, T) > 1e-6   # threshold is 1.9664%
    assert binom_tail_gt(N, 0.05, T) > 1e-2   # 5% is unusable, as reported


def test_zero_ber_gives_zero_frr():
    assert binom_tail_gt(N, 0.0, T) == 0.0
