#!/usr/bin/env python3
"""Lightweight one-at-a-time IPC sizing study for ooo_simulator.

Sweeps important knobs from a fixed baseline (RTL best-ish cached config).
Writes results/SIZING_STUDY.md and results/sizing_study.csv.
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

STUDY = ["branch", "custom_riscv", "riscv_mem", "ilp_loop", "matmul8", "mem_stream", "mix_bench"]

BASELINE = {
    "--mem-system": "cached",
    "--width": "4",
    "--cdb": "4",
    "--rob": "32",
    "--rs": "32",
    "--lsq": "32",
    "--ifq": "32",
    "--alu": "2",
    "--mul": "1",
    "--div": "1",
    "--br": "1",
    "--lsq-cdb": "2",
    "--alu-lat": "1",
    "--mul-lat": "3",
    "--div-lat": "10",
    "--br-lat": "1",
    "--pht": "1024",
    "--btb": "256",
    "--ras": "16",
    "--sb": "8",
    "--mem-lat": "1",
    "--fwd-lat": "1",
    "--dram-lat": "10",
    "--dram-out": "4",
    "--cache-line": "32",
    "--dcache-sets": "16",
    "--dcache-ways": "4",
    "--icache-sets": "16",
    "--icache-ways": "4",
    "--dcache-mshr": "4",
    "--icache-mshr": "2",
    "--dcache-ufp": "2",
    "--mshr-waiters": "4",
    "--load-hit-lat": "1",
    "--fetch-hit-lat": "1",
}

# Important axes only (not exhaustive).
SWEEPS: dict[str, list[str]] = {
    "--width": ["1", "2", "4", "8"],
    "--cdb": ["1", "2", "4", "8"],
    "--alu": ["1", "2", "3", "4"],
    "--mul-lat": ["1", "2", "3", "5"],
    "--rob": ["16", "32", "64"],
    "--rs": ["16", "32", "64"],
    "--lsq": ["16", "32", "64"],
    "--pht": ["64", "256", "1024"],
    "--cache-line": ["32", "64"],
    "--dram-lat": ["5", "10", "20"],
    "--dcache-ufp": ["1", "2"],
}


def find_sim() -> Path:
    for name in ("pipeline_sim.exe", "pipeline_sim"):
        p = ROOT / "build" / name
        if p.exists():
            return p
    raise SystemExit(f"missing sim under {ROOT / 'build'}")


def find_program(name: str) -> tuple[Path, Path | None]:
    for folder in (IMPORTED, WORKLOADS):
        imem = folder / f"{name}.imem.hex"
        if imem.exists():
            dmem = folder / f"{name}.dmem.hex"
            return imem, dmem if dmem.exists() else None
    raise FileNotFoundError(name)


def cfg_args(cfg: dict[str, str]) -> list[str]:
    out: list[str] = []
    for k, v in cfg.items():
        out += [k, v]
    return out


def geomean(values: list[float]) -> float:
    vals = [v for v in values if v > 0]
    if not vals:
        return 0.0
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def run_one(sim: Path, name: str, cfg: dict[str, str], max_cycles: int) -> dict:
    imem, dmem = find_program(name)
    cmd = [
        str(sim), "--quiet", "--csv", name,
        "--max-cycles", str(max_cycles),
        "--imem", str(imem),
        *cfg_args(cfg),
    ]
    if dmem is not None:
        cmd += ["--dmem", str(dmem)]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT), timeout=600)
    if proc.returncode != 0:
        return {"program": name, "ipc": 0.0, "cycles": 0, "commits": 0, "hit_cap": 1,
                "ok": False, "err": (proc.stderr or proc.stdout or "")[-200:]}
    line = ""
    for ln in proc.stdout.splitlines():
        if ln.strip() and not ln.startswith("benchmark,"):
            line = ln.strip()
    cols = line.split(",")
    try:
        cycles = int(cols[18])
        commits = int(cols[19])
        ipc = float(cols[22])
        hit_cap = int(cols[-1])
    except (IndexError, ValueError) as ex:
        return {"program": name, "ipc": 0.0, "cycles": 0, "commits": 0, "hit_cap": 1,
                "ok": False, "err": f"parse {ex}: {line}"}
    return {"program": name, "ipc": ipc, "cycles": cycles, "commits": commits,
            "hit_cap": hit_cap, "ok": hit_cap == 0}


def run_suite(sim: Path, cfg: dict[str, str], max_cycles: int) -> tuple[float, list[dict]]:
    rows = [run_one(sim, n, cfg, max_cycles) for n in STUDY]
    geo = geomean([r["ipc"] for r in rows if r["ok"]])
    return geo, rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--max-cycles", type=int, default=2_000_000)
    ap.add_argument("--out-md", type=Path, default=ROOT / "results" / "SIZING_STUDY.md")
    ap.add_argument("--out-csv", type=Path, default=ROOT / "results" / "sizing_study.csv")
    args = ap.parse_args()

    sim = find_sim()
    args.out_md.parent.mkdir(parents=True, exist_ok=True)

    print("Baseline...", flush=True)
    base_geo, base_rows = run_suite(sim, BASELINE, args.max_cycles)
    print(f"  study geomean = {base_geo:.6f}", flush=True)

    csv_rows: list[dict] = []
    for r in base_rows:
        csv_rows.append({"axis": "baseline", "value": "-", **{k: r[k] for k in
                        ("program", "ipc", "cycles", "commits", "hit_cap")}})

    oat: list[tuple[str, str, float, float]] = []  # axis, best_val, best_geo, delta

    for axis, values in SWEEPS.items():
        print(f"Sweep {axis} {values}...", flush=True)
        best_v, best_g = BASELINE[axis], base_geo
        detail: list[tuple[str, float]] = []
        for v in values:
            cfg = dict(BASELINE)
            cfg[axis] = v
            # Keep CDB <= width when widening width; when narrowing width, clamp cdb.
            if axis == "--width":
                cfg["--cdb"] = str(min(int(cfg["--cdb"]), int(v)))
            if axis == "--cdb":
                cfg["--cdb"] = str(min(int(v), int(cfg["--width"])))
            geo, rows = run_suite(sim, cfg, args.max_cycles)
            detail.append((v, geo))
            print(f"  {axis}={v}: geo={geo:.6f}", flush=True)
            for r in rows:
                csv_rows.append({"axis": axis, "value": v, **{k: r[k] for k in
                                ("program", "ipc", "cycles", "commits", "hit_cap")}})
            if geo > best_g + 1e-9:
                best_g, best_v = geo, v
        oat.append((axis, best_v, best_g, best_g - base_geo))

    oat.sort(key=lambda t: -t[3])

    # Combined: take improving OAT winners that beat baseline.
    combined = dict(BASELINE)
    for axis, best_v, best_g, delta in oat:
        if delta > 1e-6:
            combined[axis] = best_v
    if "--width" in combined:
        combined["--cdb"] = str(min(int(combined["--cdb"]), int(combined["--width"])))

    print("Combined improving knobs...", flush=True)
    comb_geo, comb_rows = run_suite(sim, combined, args.max_cycles)
    print(f"  study geomean = {comb_geo:.6f}", flush=True)
    for r in comb_rows:
        csv_rows.append({"axis": "combined", "value": "-", **{k: r[k] for k in
                        ("program", "ipc", "cycles", "commits", "hit_cap")}})

    with args.out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["axis", "value", "program", "ipc", "cycles",
                                          "commits", "hit_cap"])
        w.writeheader()
        w.writerows(csv_rows)

    # Markdown report
    lines: list[str] = []
    lines.append("# ooo_simulator IPC Sizing Study")
    lines.append("")
    lines.append(f"- Date: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}")
    lines.append("- Memory: cached, simple DRAM")
    lines.append(f"- Suite: {', '.join(STUDY)}")
    lines.append("- Method: one-at-a-time sweeps from baseline, then combine improving winners")
    lines.append(f"- Raw CSV: `{args.out_csv.relative_to(ROOT)}`")
    lines.append("")
    lines.append("## Baseline")
    lines.append("")
    lines.append(f"- Study geomean: **{base_geo:.6f}**")
    lines.append("")
    lines.append("| Program | IPC | Cycles | Commits |")
    lines.append("|---|---:|---:|---:|")
    for r in base_rows:
        lines.append(f"| {r['program']} | {r['ipc']:.6f} | {r['cycles']} | {r['commits']} |")
    lines.append("")
    lines.append("### Baseline knobs")
    lines.append("")
    lines.append("```")
    for k, v in BASELINE.items():
        lines.append(f"{k} {v}")
    lines.append("```")
    lines.append("")
    lines.append("## OAT summary (best value per axis)")
    lines.append("")
    lines.append("| Axis | Baseline | Best | Best study geo | Δ vs baseline |")
    lines.append("|---|---:|---:|---:|---:|")
    for axis, best_v, best_g, delta in oat:
        lines.append(
            f"| `{axis}` | {BASELINE[axis]} | {best_v} | {best_g:.6f} | {delta:+.6f} |"
        )
    lines.append("")
    lines.append("## OAT detail")
    lines.append("")

    # Re-read detail from csv for clean tables
    by_axis: dict[str, dict[str, float]] = {}
    for row in csv_rows:
        if row["axis"] in ("baseline", "combined"):
            continue
        by_axis.setdefault(row["axis"], {})
    # Recompute per (axis,value) geomean from csv_rows
    groups: dict[tuple[str, str], list[float]] = {}
    for row in csv_rows:
        if row["axis"] in ("baseline", "combined"):
            continue
        key = (row["axis"], str(row["value"]))
        if int(row["hit_cap"]) == 0 and float(row["ipc"]) > 0:
            groups.setdefault(key, []).append(float(row["ipc"]))

    for axis in SWEEPS:
        lines.append(f"### `{axis}`")
        lines.append("")
        lines.append("| Value | Study geomean |")
        lines.append("|---:|---:|")
        for v in SWEEPS[axis]:
            g = geomean(groups.get((axis, v), []))
            mark = " ← best" if v == next(x[1] for x in oat if x[0] == axis) else ""
            lines.append(f"| {v} | {g:.6f}{mark} |")
        lines.append("")

    lines.append("## Combined improving knobs")
    lines.append("")
    lines.append(f"- Study geomean: **{comb_geo:.6f}** (Δ {comb_geo - base_geo:+.6f})")
    lines.append("")
    changed = {k: v for k, v in combined.items() if BASELINE.get(k) != v}
    if changed:
        lines.append("Changes vs baseline:")
        lines.append("")
        for k, v in changed.items():
            lines.append(f"- `{k}`: {BASELINE[k]} → **{v}**")
    else:
        lines.append("No OAT winner beat baseline enough to combine.")
    lines.append("")
    lines.append("| Program | Baseline IPC | Combined IPC |")
    lines.append("|---|---:|---:|")
    base_by = {r["program"]: r["ipc"] for r in base_rows}
    for r in comb_rows:
        lines.append(
            f"| {r['program']} | {base_by.get(r['program'], 0):.6f} | {r['ipc']:.6f} |"
        )
    lines.append("")
    lines.append("## Notes")
    lines.append("")
    lines.append("- IPC = committed / cycles.")
    lines.append("- Width sweep clamps `--cdb` to `min(cdb, width)`.")
    lines.append("- This is a coarse OAT study, not a full factorial search.")
    lines.append("")

    args.out_md.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {args.out_md}")
    print(f"Wrote {args.out_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
