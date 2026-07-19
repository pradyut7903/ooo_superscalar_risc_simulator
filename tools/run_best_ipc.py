#!/usr/bin/env python3
"""Run RTL-aligned 'best IPC' config on imported + workload hex suite.

Config sources (ooo_rtl/rtl_v2):
  - pkg_cpu.sv defaults (WIDTH=4, cached, simple DRAM, ...)
  - VERIF_IPC_SIZING.md best: MUL_STAGES=2, NUM_ALU=3, PHT_SIZE=256
  - VERIF_IPC_BEST.md: CACHE_LINE_BYTES=64

Emits CSV: program,commits,cycles,ipc,hit_cap
"""

from __future__ import annotations

import argparse
import csv
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMPORTED = ROOT / "tb" / "imported"
WORKLOADS = ROOT / "tb" / "workloads_hex"

# RTL best-perf knobs (sizing study + BEST cache line).
BEST_ARGS = [
    "--mem-system", "cached",
    "--width", "4", "--cdb", "4",
    "--rob", "32", "--rs", "32", "--lsq", "32", "--ifq", "32",
    "--alu", "3", "--mul", "1", "--div", "1", "--br", "1", "--lsq-cdb", "2",
    "--alu-lat", "1", "--mul-lat", "2", "--div-lat", "10", "--br-lat", "1",
    "--pht", "256", "--btb", "256", "--ras", "16",
    "--sb", "8",
    "--mem-lat", "1", "--fwd-lat", "1",
    "--dram-lat", "10", "--dram-out", "4",
    "--cache-line", "64",
    "--dcache-sets", "16", "--dcache-ways", "4",
    "--icache-sets", "16", "--icache-ways", "4",
    "--dcache-mshr", "4", "--icache-mshr", "2",
    "--dcache-ufp", "2", "--mshr-waiters", "4",
    "--load-hit-lat", "1", "--fetch-hit-lat", "1",
]

STUDY = ["branch", "custom_riscv", "riscv_mem", "ilp_loop", "matmul8", "mem_stream", "mix_bench"]
BIG6 = ["custom_riscv", "riscv_mem", "ilp_loop", "matmul8", "mem_stream", "mix_bench"]


def find_sim() -> Path:
    for name in ("pipeline_sim.exe", "pipeline_sim"):
        p = ROOT / "build" / name
        if p.exists():
            return p
    raise SystemExit(f"missing sim binary under {ROOT / 'build'}")


def discover_programs() -> list[tuple[str, Path, Path | None]]:
    out: list[tuple[str, Path, Path | None]] = []
    seen: set[str] = set()
    for folder in (IMPORTED, WORKLOADS):
        if not folder.exists():
            continue
        for imem in sorted(folder.glob("*.imem.hex")):
            name = imem.name.replace(".imem.hex", "")
            if name in seen:
                continue
            seen.add(name)
            dmem = folder / f"{name}.dmem.hex"
            out.append((name, imem, dmem if dmem.exists() else None))
    return out


def run_one(sim: Path, name: str, imem: Path, dmem: Path | None, max_cycles: int) -> dict:
    cmd = [
        str(sim), "--quiet", "--csv", name,
        "--max-cycles", str(max_cycles),
        "--imem", str(imem),
        *BEST_ARGS,
    ]
    if dmem is not None:
        cmd += ["--dmem", str(dmem)]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT), timeout=600)
    if proc.returncode != 0:
        return {
            "program": name, "result": "FAIL", "commits": 0, "cycles": 0,
            "ipc": 0.0, "hit_cap": 1, "stderr": (proc.stderr or proc.stdout)[-400:],
        }
    # Last non-empty CSV data line
    line = ""
    for ln in proc.stdout.splitlines():
        if ln.strip() and not ln.startswith("benchmark,"):
            line = ln.strip()
    if not line:
        return {
            "program": name, "result": "FAIL", "commits": 0, "cycles": 0,
            "ipc": 0.0, "hit_cap": 1, "stderr": "no csv row",
        }
    cols = line.split(",")
    # csv: ...,cycles,committed,fetched,issued,ipc,... hit_cap last
    try:
        # Header: ...,pht,btb,ras,cycles,committed,fetched,issued,ipc,...
        cycles = int(cols[18])
        commits = int(cols[19])
        ipc = float(cols[22])
        hit_cap = int(cols[-1])
    except (IndexError, ValueError) as ex:
        return {
            "program": name, "result": "FAIL", "commits": 0, "cycles": 0,
            "ipc": 0.0, "hit_cap": 1, "stderr": f"parse: {ex} | {line}",
        }
    return {
        "program": name, "result": "PASS" if hit_cap == 0 else "CAP",
        "commits": commits, "cycles": cycles, "ipc": ipc, "hit_cap": hit_cap,
    }


def geomean(values: list[float]) -> float:
    vals = [v for v in values if v > 0]
    if not vals:
        return 0.0
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--max-cycles", type=int, default=2_000_000)
    ap.add_argument("--only", nargs="*", help="subset of program names")
    ap.add_argument("--out", type=Path, default=ROOT / "results" / "best_ipc.csv")
    args = ap.parse_args()

    sim = find_sim()
    programs = discover_programs()
    if args.only:
        want = set(args.only)
        programs = [p for p in programs if p[0] in want]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    print("Best-IPC config (RTL sizing + CACHE_LINE=64, cached, simple DRAM)")
    print(" ".join(BEST_ARGS))
    print()
    print(f"{'Program':22} {'Result':6} {'Commits':>10} {'Cycles':>10} {'IPC':>10}")
    print("-" * 64)

    for name, imem, dmem in programs:
        row = run_one(sim, name, imem, dmem, args.max_cycles)
        rows.append(row)
        print(f"{row['program']:22} {row['result']:6} {row['commits']:10d} "
              f"{row['cycles']:10d} {row['ipc']:10.6f}")
        if row.get("stderr"):
            print(f"  ! {row['stderr'][:200]}")

    with args.out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["program", "result", "commits", "cycles", "ipc", "hit_cap"])
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in w.fieldnames})

    by_name = {r["program"]: r for r in rows if r["result"] in ("PASS", "CAP")}
    study_ipc = [by_name[n]["ipc"] for n in STUDY if n in by_name]
    big6_ipc = [by_name[n]["ipc"] for n in BIG6 if n in by_name]
    all_ipc = [r["ipc"] for r in rows if r["result"] == "PASS"]

    print()
    print(f"Wrote {args.out}")
    print(f"Study-suite geomean ({len(study_ipc)}): {geomean(study_ipc):.6f}")
    print(f"Big-6 geomean ({len(big6_ipc)}):        {geomean(big6_ipc):.6f}")
    print(f"All PASS geomean ({len(all_ipc)}):      {geomean(all_ipc):.6f}")
    # CAP (max-cycle hit) is a failure for CI / soak.
    return 0 if all(r["result"] == "PASS" for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
