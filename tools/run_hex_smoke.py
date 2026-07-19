#!/usr/bin/env python3
"""Thin wrapper: short hex smoke via run_imported_sim.py --smoke."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path

# Preserve `python tools/run_hex_smoke.py --only ...` by forwarding args.
if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    sys.argv = [str(here / "run_imported_sim.py"), "--smoke", *sys.argv[1:]]
    runpy.run_path(str(here / "run_imported_sim.py"), run_name="__main__")
