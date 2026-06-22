#!/usr/bin/env python3
"""Run microarchitecture sweep experiments on the pipeline simulator.

Each "study" varies one (or a coordinated set) of microarchitectural parameters
across a set of benchmarks and collects the simulator's CSV statistics into a single
results file. Because the simulator's --csv output already records every config knob,
each row is self-describing and ready for analysis/plotting.

Studies (all map to the three resume themes: OoO structure sizing, superscalar
width & functional-unit mix, and the memory system):

  rob      ROB capacity sweep              -> finds the reorder-window knee
  rs       reservation-station sweep       -> when does the RS bottleneck?
  lsq      load/store-queue sweep          -> memory-bound benchmarks
  ifq      fetch-queue sweep               -> front-end sizing
  width    issue width (resources scaled)  -> ILP / Amdahl scaling
  width_fixedfu issue width, FUs fixed     -> isolate FU vs window effects
  alu      number of ALUs                  -> integer-FU contention
  mul      number of MUL units             -> MUL-bound contention
  div      number of DIV units             -> DIV-bound contention
  memlat   load latency                    -> latency tolerance (MLP)
  fwd      store-to-load forwarding on/off -> forwarding value
  all      run every study
"""

import argparse
import os
import subprocess
import sys

SIM = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "pipeline_sim.exe"))
BMK_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "benchmarks"))
RES_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "results"))

ALL_BMKS = ["vecadd", "dotprod", "dotprod_unroll", "matmul", "ptrchase",
            "saxpy", "ilp_serial", "ilp_parallel", "mul_heavy", "div_heavy"]
MEM_BMKS = ["vecadd", "saxpy", "dotprod", "ptrchase", "matmul"]
ILP_BMKS = ["ilp_serial", "ilp_parallel", "dotprod", "dotprod_unroll", "matmul"]

MAXC = "20000000"


def points_single(flag, values, base=None):
    """Sweep one CLI flag over values; `base` are fixed extra args for every point."""
    base = base or []
    return [base + [flag, str(v)] for v in values]


def width_points(values, scale_fu=True):
    """Width sweep with generous buffers; optionally scale ALUs+CDB with width so the
    study measures issue-width / ILP limits rather than a fixed FU bottleneck."""
    pts = []
    for w in values:
        args = ["--width", str(w), "--rob", "128", "--rs", "128",
                "--lsq", "64", "--ifq", "64", "--cdb", str(w)]
        if scale_fu:
            args += ["--alu", str(max(1, w)), "--mul", str(max(1, w // 2)),
                     "--div", "1"]
        pts.append(args)
    return pts


STUDIES = {
    "rob":    (ALL_BMKS, points_single("--rob", [8, 12, 16, 24, 32, 48, 64, 96, 128],
                                       base=["--rs", "128", "--lsq", "64"])),
    "rs":     (ALL_BMKS, points_single("--rs", [4, 8, 12, 16, 24, 32, 48, 64],
                                       base=["--rob", "128", "--lsq", "64"])),
    "lsq":    (MEM_BMKS, points_single("--lsq", [2, 4, 8, 12, 16, 24, 32],
                                       base=["--rob", "128", "--rs", "128"])),
    "ifq":    (ALL_BMKS, points_single("--ifq", [2, 4, 8, 16, 32, 64],
                                       base=["--rob", "128", "--rs", "128"])),
    "width":  (ILP_BMKS, width_points([1, 2, 3, 4, 6, 8], scale_fu=True)),
    "width_fixedfu": (ILP_BMKS, width_points([1, 2, 3, 4, 6, 8], scale_fu=False)),
    "alu":    (["ilp_parallel", "matmul", "vecadd", "saxpy"],
               points_single("--alu", [1, 2, 3, 4, 6],
                             base=["--width", "6", "--rob", "128", "--rs", "128", "--cdb", "6"])),
    "mul":    (["mul_heavy", "matmul", "dotprod"],
               points_single("--mul", [1, 2, 3, 4],
                             base=["--width", "4", "--rob", "128", "--rs", "128"])),
    "div":    (["div_heavy"],
               points_single("--div", [1, 2, 3, 4],
                             base=["--width", "4", "--rob", "128", "--rs", "128"])),
    "memlat": (["ptrchase", "vecadd", "saxpy", "dotprod", "matmul"],
               points_single("--mem-lat", [1, 2, 5, 10, 20, 50, 100],
                             base=["--rob", "128", "--rs", "128", "--lsq", "32"])),
    "fwd":    (MEM_BMKS,
               [["--rob", "128", "--rs", "128"],
                ["--rob", "128", "--rs", "128", "--no-forward"]]),
}


def get_header():
    out = subprocess.run([SIM, "--csv-header"], capture_output=True, text=True)
    return out.stdout


def run_point(bmk, args):
    trace = os.path.join(BMK_DIR, bmk + ".trace")
    cmd = [SIM, "--quiet", "--csv", bmk, "--max-cycles", MAXC] + args + [trace]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if out.returncode != 0:
        sys.stderr.write(f"  WARN {bmk} {args} exited {out.returncode}\n")
        return None
    return out.stdout.strip()


def run_study(name):
    bmks, points = STUDIES[name]
    os.makedirs(RES_DIR, exist_ok=True)
    path = os.path.join(RES_DIR, name + ".csv")
    rows = 0
    with open(path, "w") as f:
        f.write(get_header())
        for bmk in bmks:
            for args in points:
                line = run_point(bmk, args)
                if line:
                    f.write(line + "\n")
                    rows += 1
    print(f"  {name:14s} {rows:4d} runs -> results/{name}.csv")
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--study", default="all", help="study name or 'all'")
    args = ap.parse_args()

    if not os.path.exists(SIM):
        sys.exit(f"simulator not found at {SIM} (build it first)")

    studies = list(STUDIES) if args.study == "all" else [args.study]
    if args.study != "all" and args.study not in STUDIES:
        sys.exit(f"unknown study '{args.study}'. choose from: {', '.join(STUDIES)} or 'all'")

    print(f"Running {len(studies)} study/studies with simulator {SIM}")
    total = 0
    for s in studies:
        total += run_study(s)
    print(f"Done: {total} total simulation runs across {len(studies)} studies.")


if __name__ == "__main__":
    main()
