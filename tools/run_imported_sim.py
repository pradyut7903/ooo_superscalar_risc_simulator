#!/usr/bin/env python3
"""Run imported RV32IM hex suite on the C++ simulator vs golden_rv32im.

Default suite is tb/imported (manifest.csv).

Examples:
  python tools/run_imported_sim.py              # full imported suite
  python tools/run_imported_sim.py --smoke      # short architectural subset
  python tools/run_imported_sim.py --only alu jump store
  python tools/run_imported_sim.py --only coremark --max-cycles 5000000
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # package root
GOLDEN_PY = ROOT / "tools" / "golden_rv32im.py"
IMPORTED = ROOT / "tb" / "imported"
WORKLOADS = ROOT / "tb" / "workloads_hex"


def resolve_hex(name: str) -> tuple[Path, Path | None]:
    for folder in (IMPORTED, WORKLOADS):
        imem = folder / f"{name}.imem.hex"
        if imem.exists():
            dmem = folder / f"{name}.dmem.hex"
            return imem, dmem if dmem.exists() else None
    raise FileNotFoundError(name)

MANIFEST = IMPORTED / "manifest.csv"

SMOKE = [
    "super_simple",
    "jump",
    "alu",
    "simple_mem",
    "simple_st",
    "store",
    "load",
    "branch",
    "dependency_test",
]


def find_sim() -> Path:
    for name in ("pipeline_sim.exe", "pipeline_sim"):
        p = ROOT / "build" / name
        if p.exists():
            return p
    return ROOT / "build" / "pipeline_sim.exe"


def list_from_manifest() -> list[str]:
    if not MANIFEST.exists():
        # Fall back to every *.imem.hex in the imported dir.
        return sorted(p.stem.replace(".imem", "") for p in IMPORTED.glob("*.imem.hex"))
    names: list[str] = []
    with MANIFEST.open(newline="") as f:
        for row in csv.DictReader(f):
            name = (row.get("name") or "").strip()
            if name:
                names.append(name)
    return names


def run_golden(imem: Path, dmem: Path | None):
    sys.path.insert(0, str(GOLDEN_PY.parent))
    from golden_rv32im import GoldenRV32IM, load_hex_words  # type: ignore

    dwords = load_hex_words(dmem) if dmem and dmem.exists() else []
    model = GoldenRV32IM(load_hex_words(imem), dwords)
    model.run()
    return model


def run_sim(
    sim: Path,
    imem: Path,
    dmem: Path | None,
    max_cycles: int,
    mem_system: str = "ideal",
) -> tuple[list[int], dict[int, int], str]:
    cmd = [
        str(sim),
        "--quiet",
        "--dump",
        "--dump-regs",
        "--max-cycles",
        str(max_cycles),
        "--mem-system",
        mem_system,
        "--imem",
        str(imem),
    ]
    if dmem and dmem.exists():
        cmd += ["--dmem", str(dmem)]
    out = subprocess.check_output(cmd, text=True, cwd=str(ROOT), stderr=subprocess.STDOUT)
    regs = [0] * 32
    mem: dict[int, int] = {}
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("x") and "=" in line:
            name, val = line.split("=", 1)
            idx = int(name[1:])
            regs[idx] = int(val, 16) & 0xFFFFFFFF
        elif line.startswith("MEM[") and "] =" in line:
            # MEM[<byte_addr>] = <decimal>
            left, right = line.split("] =", 1)
            addr = int(left[4:], 0)
            mem[addr] = int(right.strip()) & 0xFFFFFFFF
    return regs, mem, out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--smoke", action="store_true", help="run short SMOKE subset only")
    ap.add_argument("--only", nargs="*", help="run these program names only")
    ap.add_argument("--max-cycles", type=int, default=200_000)
    ap.add_argument(
        "--mem-system",
        choices=("ideal", "cached"),
        default="ideal",
        help="memory model (default ideal; Stage 8 cached = I$/D$/DRAM)",
    )
    ap.add_argument("--list", action="store_true", help="print suite names and exit")
    args = ap.parse_args()

    if args.only:
        names = list(args.only)
    elif args.smoke:
        names = list(SMOKE)
    else:
        names = list_from_manifest()

    if args.list:
        for n in names:
            print(n)
        return 0

    sim = find_sim()
    if not sim.exists():
        print(f"missing sim binary: {sim} (run cmake --build build first)", file=sys.stderr)
        return 2
    if not GOLDEN_PY.exists():
        print(f"missing golden: {GOLDEN_PY}", file=sys.stderr)
        return 2
    if not IMPORTED.exists():
        print(f"missing imported suite: {IMPORTED}", file=sys.stderr)
        return 2

    passed = failed = skipped = 0
    for name in names:
        try:
            imem, dmem = resolve_hex(name)
        except FileNotFoundError:
            print(f"SKIP {name}: no imem hex under tb/")
            skipped += 1
            continue
        try:
            golden = run_golden(imem, dmem)
            sregs, smem, _ = run_sim(
                sim,
                imem,
                dmem,
                args.max_cycles,
                args.mem_system,
            )
        except subprocess.CalledProcessError as e:
            failed += 1
            print(f"FAIL {name}: sim exited {e.returncode}")
            if e.stdout:
                print(e.stdout[-800:])
            continue
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: {e}")
            continue

        mismatches = [i for i in range(32) if golden.x[i] != sregs[i]]
        mem_bad = []
        # Compare every word the sim reports nonzero, plus a prefix of golden dmem.
        addrs = set(smem.keys())
        for a in range(0, min(getattr(golden.mem, "size", 4096), 8192), 4):
            gw = golden.mem.load_word(a) & 0xFFFFFFFF
            if gw != 0:
                addrs.add(a)
        for a in sorted(addrs):
            gw = golden.mem.load_word(a) & 0xFFFFFFFF
            sw = smem.get(a, 0) & 0xFFFFFFFF
            if gw != sw:
                mem_bad.append(a)
        if mismatches or mem_bad:
            failed += 1
            print(f"FAIL {name}: regs {mismatches[:8]} mem {mem_bad[:8]}")
            for i in mismatches[:4]:
                print(f"  x{i}: golden={golden.x[i]:08x} sim={sregs[i]:08x}")
            for a in mem_bad[:4]:
                print(f"  MEM[0x{a:x}]: golden={golden.mem.load_word(a):08x} "
                      f"sim={smem.get(a, 0):08x}")
        else:
            passed += 1
            print(f"PASS {name}")

    total = passed + failed + skipped
    print(f"\nSummary: {passed} pass, {failed} fail, {skipped} skip ({total} listed)")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
