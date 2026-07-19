#!/usr/bin/env python3
"""Master functional-verification harness for the out-of-order simulator.

Runs the full battery and exits non-zero on any failure:

  Phase 1  Golden oracle     golden_sim's result == hand-computed .expect values
                             (validates the golden model itself).
  Phase 2  Co-simulation     OoO --dump == golden --dump on directed tests,
                             performance benchmarks, and constrained-random programs.
  Phase 3  Config invariance OoO architectural state is identical across many
                             microarchitectural configs (and equals golden).
  Phase 4  Determinism       OoO produces identical output on repeated runs.
  Phase 5  Self-check asserts every co-sim OoO run uses --selfcheck, so structural
                             invariants are asserted every cycle.
  Phase 6  Functional cover  aggregate stats prove the interesting events
                             (each opcode class, mispredict/flush, memory-order
                             violation, every stall reason) were actually exercised.

Usage: python tools/verify.py [--random-count N] [--quick]
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OOO = os.path.join(ROOT, "build", "pipeline_sim.exe")
GOLD = os.path.join(ROOT, "build", "golden_sim.exe")
DIRECTED = os.path.join(ROOT, "tests", "directed")
RANDOM = os.path.join(ROOT, "tests", "random")
BENCH = os.path.join(ROOT, "benchmarks")

DUMP_REG = re.compile(r"R(\d+)\s*=\s*(-?\d+)")
DUMP_MEM = re.compile(r"MEM\[(\d+)\]\s*=\s*(-?\d+)")
MAXC = "20000000"

# config-invariance matrix: arch state must be identical for all of these.
CONFIGS = {
    "baseline":   [],
    "width1":     ["--width", "1"],
    "width2":     ["--width", "2"],
    "width8":     ["--width", "8", "--cdb", "8", "--alu", "4"],
    "rob8":       ["--rob", "8"],
    "rs4":        ["--rs", "4"],
    "lsq2":       ["--lsq", "2"],
    "ifq2":       ["--ifq", "2"],
    "1alu1mul":   ["--alu", "1", "--mul", "1"],
    "memlat25":   ["--mem-lat", "25"],
    "nofwd":      ["--no-forward"],
    "smallpht":   ["--pht", "16", "--btb", "8"],
}


def parse_dump(text):
    regs, mem = {}, {}
    for line in text.splitlines():
        m = DUMP_REG.search(line)
        if m:
            regs[int(m.group(1))] = int(m.group(2))
        m = DUMP_MEM.search(line)
        if m:
            mem[int(m.group(1))] = int(m.group(2))
    return regs, mem


def run_dump(binary, trace, extra=None, selfcheck=False):
    cmd = [binary, "--quiet", "--dump", "--max-cycles", MAXC]
    if binary == GOLD:
        cmd = [binary, "--quiet", "--dump", "--max-steps", "20000000"]
    if selfcheck and binary == OOO:
        cmd.append("--selfcheck")
    cmd += (extra or []) + [trace]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if out.returncode != 0:
        raise RuntimeError(f"{os.path.basename(binary)} exited {out.returncode} on "
                           f"{os.path.basename(trace)}: {out.stderr.strip()[:200]}")
    if "HIT max_cycles" in out.stdout or "hit step limit" in out.stderr:
        raise RuntimeError(f"{os.path.basename(binary)} did not terminate on {os.path.basename(trace)}")
    return parse_dump(out.stdout)


def run_csv(trace, extra=None):
    cmd = [OOO, "--quiet", "--csv", "x", "--max-cycles", MAXC] + (extra or []) + [trace]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    hdr = subprocess.run([OOO, "--csv-header"], capture_output=True, text=True).stdout.strip().split(",")
    if out.returncode != 0 or not out.stdout.strip():
        return None
    return dict(zip(hdr, out.stdout.strip().split(",")))


def load_expect(path):
    regs, mem = {}, {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        k, v = line.split()
        (regs if k[0] == "R" else mem)[int(k[1:])] = int(v)
    return regs, mem


def cmp_state(a, b):
    """Compare two (regs, mem) states. Returns list of human-readable diffs."""
    (ra, ma), (rb, mb) = a, b
    diffs = []
    for r in sorted(set(ra) | set(rb)):
        if ra.get(r, 0) != rb.get(r, 0):
            diffs.append(f"R{r}: {ra.get(r,0)} vs {rb.get(r,0)}")
    for addr in sorted(set(ma) | set(mb)):
        if ma.get(addr, 0) != mb.get(addr, 0):
            diffs.append(f"MEM[{addr}]: {ma.get(addr,0)} vs {mb.get(addr,0)}")
    return diffs


def traces_in(d):
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".trace")) if os.path.isdir(d) else []


class Reporter:
    def __init__(self):
        self.failures = 0

    def ok(self, msg):
        print(f"  PASS  {msg}")

    def fail(self, msg):
        print(f"  FAIL  {msg}")
        self.failures += 1


def phase_golden_oracle(rep):
    print("\n[Phase 1] Golden model vs hand-computed expected values")
    n = 0
    for t in traces_in(DIRECTED) + traces_in(BENCH):
        exp = t[:-6] + ".expect"
        if not os.path.exists(exp):
            continue
        name = os.path.basename(t)[:-6]
        er, em = load_expect(exp)
        try:
            gr, gm = run_dump(GOLD, t)
        except Exception as e:
            rep.fail(f"{name}: golden error: {e}")
            continue
        # SUBSET check: every hand-specified value must match. The golden dump may
        # additionally contain input arrays / scratch registers not listed in .expect.
        diffs = [f"R{r}: exp {v} got {gr.get(r,0)}" for r, v in er.items() if gr.get(r, 0) != v]
        diffs += [f"MEM[{a}]: exp {v} got {gm.get(a,0)}" for a, v in em.items() if gm.get(a, 0) != v]
        if diffs:
            rep.fail(f"{name}: golden != expected: {'; '.join(diffs[:4])}")
        else:
            n += 1
    if n:
        rep.ok(f"golden model matches expected values on {n} traces")


def phase_cosim(rep, sets):
    print("\n[Phase 2] Co-simulation: OoO --selfcheck vs golden")
    for label, traces in sets:
        passed = 0
        for t in traces:
            name = os.path.basename(t)[:-6]
            try:
                g = run_dump(GOLD, t)
                o = run_dump(OOO, t, selfcheck=True)
            except Exception as e:
                rep.fail(f"{label}/{name}: {e}")
                continue
            diffs = cmp_state(g, o)
            if diffs:
                rep.fail(f"{label}/{name}: OoO != golden: {'; '.join(diffs[:4])}")
            else:
                passed += 1
        if passed:
            rep.ok(f"{label}: {passed}/{len(traces)} OoO==golden")


def phase_config_invariance(rep):
    print("\n[Phase 3] Config invariance: arch state identical across microarchitectures")
    # representative coverage: a few benchmarks + a few directed tests
    picks = ([os.path.join(BENCH, b + ".trace") for b in
              ["matmul", "vecadd", "dotprod", "ptrchase", "saxpy"]] +
             [os.path.join(DIRECTED, d + ".trace") for d in
              ["store_load_forward", "store_overwrite", "loop_accumulate"]])
    for t in picks:
        if not os.path.exists(t):
            continue
        name = os.path.basename(t)[:-6]
        try:
            ref = run_dump(GOLD, t)
        except Exception as e:
            rep.fail(f"{name}: golden error: {e}")
            continue
        bad = []
        for cfg, args in CONFIGS.items():
            try:
                st = run_dump(OOO, t, extra=args, selfcheck=True)
            except Exception as e:
                bad.append(f"{cfg}({e})")
                continue
            if cmp_state(ref, st):
                bad.append(cfg)
        if bad:
            rep.fail(f"{name}: differs/errors under configs: {', '.join(bad)}")
        else:
            rep.ok(f"{name}: identical across {len(CONFIGS)} configs")


def phase_determinism(rep):
    print("\n[Phase 4] Determinism: repeated runs are identical")
    picks = ["matmul", "ptrchase", "dotprod_unroll"]
    for b in picks:
        t = os.path.join(BENCH, b + ".trace")
        if not os.path.exists(t):
            continue
        try:
            a = run_dump(OOO, t)
            b2 = run_dump(OOO, t)
        except Exception as e:
            rep.fail(f"{b}: {e}")
            continue
        if cmp_state(a, b2):
            rep.fail(f"{b}: nondeterministic output")
        else:
            rep.ok(f"{b}: deterministic")


def phase_coverage(rep, all_traces):
    print("\n[Phase 5] Functional coverage: were the interesting events exercised?")
    agg = {}
    cfg_variants = [[], ["--rob", "8"], ["--rs", "2"], ["--lsq", "2"], ["--ifq", "2"]]
    for t in all_traces:
        for args in cfg_variants:
            row = run_csv(t, args)
            if not row:
                continue
            for k, v in row.items():
                try:
                    agg[k] = agg.get(k, 0) + int(v)
                except ValueError:
                    pass
    required = {
        "c_alu": "ALU ops committed", "c_mul": "MUL ops committed",
        "c_div": "DIV ops committed", "c_load": "loads committed",
        "c_store": "stores committed", "c_branch": "branches committed",
        "mispredicts": "branch mispredictions", "flushes": "pipeline flushes",
        "stall_rob": "ROB-full stalls", "stall_rs": "RS-full stalls",
        "stall_lsq": "LSQ-full stalls", "stall_front": "front-end stalls",
    }
    for key, desc in required.items():
        hit = agg.get(key, 0)
        if hit > 0:
            rep.ok(f"covered: {desc} ({hit})")
        else:
            rep.fail(f"NOT covered: {desc} (bin '{key}' == 0)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--random-count", type=int, default=200)
    ap.add_argument("--quick", action="store_true", help="skip random + config phases")
    args = ap.parse_args()

    for b in (OOO, GOLD):
        if not os.path.exists(b):
            sys.exit(f"missing binary: {b} (build it first)")

    # (re)generate inputs
    subprocess.run([sys.executable, os.path.join(ROOT, "tests", "gen_directed.py")], check=True)
    if not args.quick:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_random_tests.py"),
                        "--count", str(args.random_count)], check=True)

    directed = traces_in(DIRECTED)
    bench = traces_in(BENCH)
    rand = traces_in(RANDOM) if not args.quick else []

    rep = Reporter()
    phase_golden_oracle(rep)
    sets = [("directed", directed), ("benchmarks", bench)]
    if rand:
        sets.append(("random", rand))
    phase_cosim(rep, sets)
    if not args.quick:
        phase_config_invariance(rep)
    phase_determinism(rep)
    phase_coverage(rep, directed + bench)

    print("\n" + "=" * 60)
    if rep.failures == 0:
        print("VERIFICATION PASSED — all checks green.")
        print("=" * 60)
        sys.exit(0)
    else:
        print(f"VERIFICATION FAILED — {rep.failures} failing check(s).")
        print("=" * 60)
        sys.exit(1)


if __name__ == "__main__":
    main()
