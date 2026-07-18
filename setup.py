#!/usr/bin/env python3
"""Shim for `pip install -e .` on toolchains that predate PEP 660.

All real metadata lives in pyproject.toml.
"""
from setuptools import setup

setup()
