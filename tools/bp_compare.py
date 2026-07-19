#!/usr/bin/env python3
"""Compare gshare (no RAS) vs always-taken (+RAS) on hex workloads.

Writes results/bp_compare.csv and results/BP_COMPARE.md.
"""

from __future__ import annotations

import argparse
import csv
import math
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMPORTED = ROOT / "tb" / "imported"
WORKLOADS = ROOT / "tb" / "workloads_hex"

# Same "best" cached config used for IPC studies.
BASE_ARGS = [
    "--mem-system", "cached",
    "--width", "4", "--cdb", "4",
    "--rob", "32", "--rs", "32", "--lsq", "32", "--ifq", "32",
    "--alu", "3", "--mul", "1", "--div", "1", "--br", "1", "--lsq-cdb", "2",
    "--alu-lat", "1", "--mul-lat", "2", "--div-lat", "10", "--br-lat", "1",
    "--pht", "256", "--btb", "256", "--ras", "16",
    "--sb", "8",
    "--dram-lat", "10", "--dram-out", "4",
    "--cache-line", "64",
    "--dcache-sets", "16", "--dcache-ways", "4",
    "--icache-sets", "16", "--icache-ways", "4",
    "--dcache-mshr", "4", "--icache-mshr", "2",
    "--dcache-ufp", "2", "--mshr-waiters", "4",
    "--load-hit-lat", "1", "--fetch-hit-lat", "1",
]

# Study suite + CoreMark (+ a few useful imported kernels if present).
DEFAULT_PROGS = [
    "branch", "custom_riscv", "riscv_mem",
    "ilp_loop", "matmul8", "mem_stream", "mix_bench",
    "coremark",
]


def find_sim() -> Path:
    for name in ("pipeline_sim.exe", "pipeline_sim"):
        p = ROOT / "build" / name
        if p.exists():
            return p
    raise SystemExit(f"missing sim under {ROOT / 'build'}")


def find_program(name: str) -> tuple[Path, Path | None]:
    for folder in (WORKLOADS, IMPORTED):
        imem = folder / f"{name}.imem.hex"
        if imem.exists():
            dmem = folder / f"{name}.dmem.hex"
            return imem, dmem if dmem.exists() else None
    raise FileNotFoundError(name)


def parse_csv_row(text: str) -> dict[str, str]:
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    # Prefer a header+data pair from --csv-header style; our sim prints data only.
    data = None
    for ln in reversed(lines):
        if ln.startswith("benchmark,"):
            continue
        if "," in ln:
            data = ln
            break
    if not data:
        raise ValueError("no csv data row")
    # Reconstruct with known header from printStatsCSVHeader (must stay in sync).
    header = (
        "benchmark,width,rob,rs,lsq,ifq,cdb,alu,mul,div,"
        "alu_lat,mul_lat,div_lat,mem_lat,fwd,pht,btb,ras,bp,"
        "cycles,committed,fetched,issued,ipc,"
        "c_alu,c_mul,c_div,c_load,c_store,c_branch,"
        "branches,mispredicts,mispred_rate,flushes,squashed,"
        "stall_rob,stall_rs,stall_lsq,stall_front,hit_cap"
    ).split(",")
    cols = data.split(",")
    if len(cols) < len(header):
        raise ValueError(f"short csv row ({len(cols)} < {len(header)}): {data[:120]}")
    return dict(zip(header, cols))


def run_one(sim: Path, name: str, imem: Path, dmem: Path | None,
            bp: str, max_cycles: int) -> dict:
    cmd = [
        str(sim), "--quiet", "--csv", name,
        "--bp", bp,
        "--max-cycles", str(max_cycles),
        "--imem", str(imem),
        *BASE_ARGS,
    ]
    if dmem is not None:
        cmd += ["--dmem", str(dmem)]
    timeout = 1200 if name == "coremark" else 600
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT), timeout=timeout)
    if proc.returncode != 0:
        return {
            "program": name, "bp": bp, "result": "FAIL",
            "commits": 0, "cycles": 0, "ipc": 0.0,
            "branches": 0, "mispredicts": 0, "mispred_pct": 0.0, "hit_cap": 1,
            "stderr": (proc.stderr or proc.stdout)[-300:],
        }
    row = parse_csv_row(proc.stdout)
    branches = int(row["branches"])
    mispredicts = int(row["mispredicts"])
    mispred_pct = 100.0 * float(row["mispred_rate"])
    return {
        "program": name,
        "bp": row["bp"],
        "result": "CAP" if row["hit_cap"] == "1" else "PASS",
        "commits": int(row["committed"]),
        "cycles": int(row["cycles"]),
        "ipc": float(row["ipc"]),
        "branches": branches,
        "mispredicts": mispredicts,
        "mispred_pct": mispred_pct,
        "hit_cap": int(row["hit_cap"]),
    }


def geomean(vals: list[float]) -> float:
    vals = [v for v in vals if v > 0]
    if not vals:
        return 0.0
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def write_md(path: Path, rows: list[dict], modes: list[str]) -> None:
    by = {(r["program"], r["bp"]): r for r in rows}
    progs = []
    for r in rows:
        if r["program"] not in progs:
            progs.append(r["program"])

    lines = [
        "# Branch predictor comparison",
        "",
        f"- Date: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}",
        "- Config: best cached (width 4, ALU 3, mul-lat 2, line 64, …)",
        "- Modes:",
        "  - `gshare` — PHT + BTB; RAS unused at fetch",
        "  - `always-taken` — PC-relative branches/JAL always taken; RAS for call/return",
        "- CSV: `results/bp_compare.csv`",
        "",
        "## Per-program",
        "",
        "| Program | BP | Result | Commits | Cycles | IPC | Br resolved | Mispredicts | Mispred % |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for prog in progs:
        for mode in modes:
            r = by.get((prog, mode))
            if not r:
                continue
            lines.append(
                f"| {prog} | {mode} | {r['result']} | {r['commits']} | {r['cycles']} | "
                f"{r['ipc']:.4f} | {r['branches']} | {r['mispredicts']} | {r['mispred_pct']:.2f} |"
            )

    lines += [
        "",
        "## Side-by-side (IPC / mispred %)",
        "",
        "| Program | gshare IPC | always-taken IPC | Δ IPC | gshare mis% | always-taken mis% | Δ mis% |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    g_ipcs, a_ipcs = [], []
    for prog in progs:
        g = by.get((prog, "gshare"))
        a = by.get((prog, "always-taken"))
        if not g or not a:
            continue
        if g["result"] == "PASS":
            g_ipcs.append(g["ipc"])
        if a["result"] == "PASS":
            a_ipcs.append(a["ipc"])
        dipc = a["ipc"] - g["ipc"]
        dmis = a["mispred_pct"] - g["mispred_pct"]
        lines.append(
            f"| {prog} | {g['ipc']:.4f} | {a['ipc']:.4f} | {dipc:+.4f} | "
            f"{g['mispred_pct']:.2f} | {a['mispred_pct']:.2f} | {dmis:+.2f} |"
        )

    lines += [
        "",
        "## Summary",
        "",
        f"- gshare IPC geomean: **{geomean(g_ipcs):.4f}** ({len(g_ipcs)} PASS)",
        f"- always-taken IPC geomean: **{geomean(a_ipcs):.4f}** ({len(a_ipcs)} PASS)",
        "",
        "Mispred % = `100 * mispredicts / branches_resolved`.",
        "always-taken is usually worse on not-taken-heavy code; gshare wins when the",
        "BTB/PHT train. Returns still need the RAS in always-taken mode.",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--max-cycles", type=int, default=5_000_000)
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--out-csv", type=Path, default=ROOT / "results" / "bp_compare.csv")
    ap.add_argument("--out-md", type=Path, default=ROOT / "results" / "BP_COMPARE.md")
    args = ap.parse_args()

    sim = find_sim()
    progs = list(args.only) if args.only else list(DEFAULT_PROGS)
    modes = ["gshare", "always-taken"]

    rows: list[dict] = []
    print(f"sim={sim}")
    print(" ".join(BASE_ARGS))
    print()
    hdr = f"{'Program':16} {'BP':14} {'Res':4} {'IPC':>8} {'Mis%':>8} {'Br':>8} {'MP':>8}"
    print(hdr)
    print("-" * len(hdr))

    for name in progs:
        try:
            imem, dmem = find_program(name)
        except FileNotFoundError:
            print(f"{name:16} SKIP (no hex)")
            continue
        for bp in modes:
            r = run_one(sim, name, imem, dmem, bp, args.max_cycles)
            rows.append(r)
            print(f"{r['program']:16} {r['bp']:14} {r['result']:4} "
                  f"{r['ipc']:8.4f} {r['mispred_pct']:8.2f} "
                  f"{r['branches']:8d} {r['mispredicts']:8d}")
            if r.get("stderr"):
                print(f"  ! {r['stderr'][:200]}")

    args.out_csv.parent.mkdir(parents=True, exist_ok=True)
    fields = ["program", "bp", "result", "commits", "cycles", "ipc",
              "branches", "mispredicts", "mispred_pct", "hit_cap"]
    with args.out_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    write_md(args.out_md, rows, modes)
    print()
    print(f"Wrote {args.out_csv}")
    print(f"Wrote {args.out_md}")
    return 0 if all(r["result"] == "PASS" for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
