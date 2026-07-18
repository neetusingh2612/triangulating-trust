"""Tests for the tag primitive. These are the invariants that, when broken
during development, produced silently-wrong measurements."""
import pytest
from triangulating_trust.tt_tag import tag, catalan_key_from_seed

SEED = 0x0F1E2D3C4B5A69788796A5B4C3D2E1F0


def test_schedule_length():
    """|K| = 2n-1 for n fingerprint points."""
    for n in (10, 20, 30):
        assert len(catalan_key_from_seed(SEED, n)) == 2 * n - 1


def test_schedule_is_balanced_prefix():
    """A Catalan/Dyck word never has more ')' than '(' in any prefix."""
    k = catalan_key_from_seed(SEED, 30)
    depth = 0
    for c in k:
        depth += 1 if c == "(" else -1
        assert depth >= -1, "prefix went unbalanced"


def test_known_vector():
    """Regression vector. If this changes, the tag construction changed and
    every measured number in the paper must be regenerated."""
    k = catalan_key_from_seed(SEED, 30)
    assert tag(SEED, 0x123, bytes(8), 5, k, t=16) == 0x7BC1


def test_determinism():
    k = catalan_key_from_seed(SEED, 30)
    a = tag(SEED, 0x1A0, b"\x01\x02\x03\x04\x05\x06\x07\x08", 42, k, t=16)
    b = tag(SEED, 0x1A0, b"\x01\x02\x03\x04\x05\x06\x07\x08", 42, k, t=16)
    assert a == b


def test_tag_width():
    k = catalan_key_from_seed(SEED, 30)
    for t_bits in (8, 16):
        v = tag(SEED, 0x100, bytes(8), 1, k, t=t_bits)
        assert 0 <= v < (1 << t_bits)


def test_sensitivity_to_inputs():
    """Changing ID, payload or nonce must change the tag. A construction that
    ignores one of its inputs is the failure mode that produced a 1-bit tag in
    an earlier version of this work."""
    k = catalan_key_from_seed(SEED, 30)
    base = tag(SEED, 0x100, bytes(8), 1, k, t=16)
    assert tag(SEED, 0x101, bytes(8), 1, k, t=16) != base
    assert tag(SEED, 0x100, b"\x00" * 7 + b"\x01", 1, k, t=16) != base
    assert tag(SEED, 0x100, bytes(8), 2, k, t=16) != base


def test_key_separation():
    """Different seeds must give different tags on the same message."""
    other = SEED ^ 0xFFFF
    ka = catalan_key_from_seed(SEED, 30)
    kb = catalan_key_from_seed(other, 30)
    assert tag(SEED, 0x100, bytes(8), 1, ka, t=16) != tag(other, 0x100, bytes(8), 1, kb, t=16)


def test_tag_distribution_is_not_degenerate():
    """Over many messages the 16-bit tag should take many distinct values.
    A collapsed construction shows up here immediately."""
    k = catalan_key_from_seed(SEED, 30)
    vals = {tag(SEED, 0x100 + (i % 16), i.to_bytes(8, "big"), i, k, t=16) for i in range(2000)}
    assert len(vals) > 1500, f"only {len(vals)} distinct tags in 2000 messages"
