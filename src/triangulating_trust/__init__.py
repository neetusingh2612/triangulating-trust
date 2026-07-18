"""Triangulating Trust: geometric sender authentication for the CAN bus.

Reference implementation and evaluation harnesses accompanying the paper
"Triangulating Trust: Delaunay-Based Geometric Keys for Sender Authentication
in CAN Protocol".

The tag primitive lives in `tt_tag`; everything else is measurement code.
"""
__version__ = "1.0.0"
from .tt_tag import tag, catalan_key_from_seed  # noqa: F401
