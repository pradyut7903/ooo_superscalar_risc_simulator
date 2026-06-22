#!/usr/bin/env python3
"""Validate that each benchmark computes the correct result.

Runs the simulator with --dump on every benchmark that has a .expect file and
compares the final architectural state (registers + output memory) against the
values the generator predicted. This is the "does it make sense" check: it proves
the out-of-order machine produces architecturally correct results, not just
plausible cycle counts.
"""

import argparse
import os
import re
import subprocess
import sys

DUMP_REG = re.compile(r"R(\d+)\s*=\s*(-?\d+)")
DUMP_MEM = re.compile(r"MEM\[(\d+)\]\s*=\s*(-?\d+)")


def run_dump(sim, trace, max_cycles):
    sim = os.path.abspath(sim)
    trace = os.path.abspath(trace)
    out = subprocess.run(
        [sim, "--quiet", "--dump", "--max-cycles", str(max_cycles), trace],
        capture_output=True, text=True, timeout=120,
    )
    if out.returncode != 0:
        raise RuntimeError(f"simulator exited {out.returncode}: {out.stderr.strip()}")
    regs, mem = {}, {}
    for line in out.stdout.splitlines():
        m = DUMP_REG.search(line)
        if m:
            regs[int(m.group(1))] = int(m.group(2))
        m = DUMP_MEM.search(line)
        if m:
            mem[int(m.group(1))] = int(m.group(2))
    hit_cap = "HIT max_cycles" in out.stdout
    return regs, mem, hit_cap


def load_expect(path):
    regs, mem = {}, {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, val = line.split()
            if key.startswith("R"):
                regs[int(key[1:])] = int(val)
            elif key.startswith("M"):
                mem[int(key[1:])] = int(val)
    return regs, mem


def check_one(sim, trace, expect, max_cycles):
    exp_regs, exp_mem = load_expect(expect)
    regs, mem, hit_cap = run_dump(sim, trace, max_cycles)
    if hit_cap:
        return False, "hit max_cycles cap (did not finish / possible deadlock)"
    errors = []
    for r, v in sorted(exp_regs.items()):
        actual = regs.get(r, 0)
        if actual != v:
            errors.append(f"R{r}: expected {v}, got {actual}")
    for a, v in sorted(exp_mem.items()):
        actual = mem.get(a, 0)
        if actual != v:
            errors.append(f"MEM[{a}]: expected {v}, got {actual}")
    if errors:
        return False, "; ".join(errors[:6]) + (" ..." if len(errors) > 6 else "")
    return True, f"{len(exp_regs)} regs + {len(exp_mem)} mem words OK"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default="build/pipeline_sim.exe")
    ap.add_argument("--dir", default="benchmarks")
    ap.add_argument("--max-cycles", type=int, default=5_000_000)
    args = ap.parse_args()

    traces = sorted(f for f in os.listdir(args.dir) if f.endswith(".trace"))
    npass = 0
    nfail = 0
    for t in traces:
        name = t[:-6]
        expect = os.path.join(args.dir, name + ".expect")
        if not os.path.exists(expect):
            print(f"  {name:16s} SKIP (no .expect)")
            continue
        try:
            ok, msg = check_one(args.sim, os.path.join(args.dir, t), expect, args.max_cycles)
        except Exception as e:  # noqa
            ok, msg = False, f"ERROR: {e}"
        status = "PASS" if ok else "FAIL"
        print(f"  {name:16s} {status}  {msg}")
        npass += ok
        nfail += not ok
    print(f"\n{npass} passed, {nfail} failed")
    sys.exit(1 if nfail else 0)


if __name__ == "__main__":
    main()
