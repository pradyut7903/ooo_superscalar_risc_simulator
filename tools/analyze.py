#!/usr/bin/env python3
"""Summarize sweep results as readable pivot tables (benchmark x swept-param -> IPC).

Reads results/<study>.csv produced by run_sweep.py and prints, for each study, a
table of IPC against the parameter that the study varied. Also flags the saturation
"knee" (smallest parameter value reaching >=99% of that benchmark's best IPC).

Pure standard library (no pandas) so it runs anywhere Python 3 does.
"""

import argparse
import csv
import os

RES_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "results"))

# which CSV column each study varies (column name in the CSV header)
STUDY_PARAM = {
    "rob": "rob", "rs": "rs", "lsq": "lsq", "ifq": "ifq",
    "width": "width", "width_fixedfu": "width",
    "alu": "alu", "mul": "mul", "div": "div",
    "memlat": "mem_lat", "fwd": "fwd",
}


def load(study):
    path = os.path.join(RES_DIR, study + ".csv")
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return list(csv.DictReader(f))


def pivot(study):
    rows = load(study)
    if not rows:
        print(f"[{study}] no results (run: python tools/run_sweep.py --study {study})")
        return
    param = STUDY_PARAM[study]
    # "knee" only makes sense where a larger parameter is better (more resources).
    # For memlat (larger = worse) and fwd (a toggle) it is not meaningful.
    show_knee = study not in ("memlat", "fwd")
    bmks = sorted({r["benchmark"] for r in rows})
    vals = sorted({int(r[param]) for r in rows})

    table = {}  # (bmk, val) -> ipc
    capped = set()
    for r in rows:
        b, v = r["benchmark"], int(r[param])
        table[(b, v)] = float(r["ipc"])
        if r["hit_cap"] == "1":
            capped.add((b, v))

    width = max(11, max(len(b) for b in bmks) + 1)
    hdr = f"{param:>{width}} | " + " ".join(f"{v:>6}" for v in vals)
    print(f"\n=== {study}: IPC vs {param} ===")
    print(hdr)
    print("-" * len(hdr))
    for b in bmks:
        cells = []
        best = max((table.get((b, v), 0.0) for v in vals), default=0.0)
        knee = None
        for v in vals:
            ipc = table.get((b, v))
            if ipc is None:
                cells.append(f"{'-':>6}")
                continue
            mark = "*" if (b, v) in capped else ""
            cells.append(f"{ipc:6.2f}{mark}")
            if knee is None and best > 0 and ipc >= 0.99 * best:
                knee = v
        knee_s = f"  knee@{param}={knee}" if (show_knee and knee is not None) else ""
        print(f"{b:>{width}} | " + " ".join(cells) + knee_s)
    if capped:
        print("  (* = hit max_cycles cap; raise --max-cycles)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--study", default="all")
    args = ap.parse_args()
    studies = list(STUDY_PARAM) if args.study == "all" else [args.study]
    for s in studies:
        pivot(s)


if __name__ == "__main__":
    main()
