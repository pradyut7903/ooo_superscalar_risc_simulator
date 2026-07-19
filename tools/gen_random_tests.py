#!/usr/bin/env python3
"""Constrained-random test generator (co-simulation fuzzing).

Emits seeded, randomly-generated but always-legal-and-terminating programs. Each is
run through both the golden model and the out-of-order simulator by tools/verify.py;
any architectural-state mismatch is a bug. This is the industry "constrained-random +
ISS co-simulation" technique (cf. RISC-V riscv-dv): the constraints keep every program
valid and finite while the randomness explores hazard/forwarding/branch interleavings
that hand-written directed tests miss.

Constraints that keep programs valid & terminating:
  * Only FORWARD branches (targets strictly ahead) -> the control-flow graph is a DAG,
    so every program halts.
  * Memory ops address a fixed safe window via dedicated base registers (R14/R15),
    which ALU ops never overwrite -> no wild addresses.
  * Immediates and initial values are small -> arithmetic stays well within int range.
"""

import argparse
import os
import random

OUT = os.path.join(os.path.dirname(__file__), "..", "tests", "random")

BASE_PC = 0x1000
MEM_BASE_A = 1000      # R14 points here
MEM_BASE_B = 2000      # R15 points here
ALU_DEST = list(range(1, 14))          # R1..R13 are general (R14/R15 reserved bases)
ALU_OPS = ["ADD", "SUB", "MUL", "DIV", "ADDI", "ADDI", "ADD", "SUB"]  # bias away from MUL/DIV


def gen_program(rng, n):
    """Return a list of instruction tuples (op, dest, src1, src2, imm_or_targetidx).
    Branch targets are stored as ('T', idx) and resolved to PCs at render time."""
    prog = []

    # Prologue: initialize R1..R13 to small values, R14/R15 to memory bases.
    for r in ALU_DEST:
        prog.append(("ADDI", r, 0, 0, rng.randint(0, 9)))
    prog.append(("ADDI", 14, 0, 0, MEM_BASE_A))
    prog.append(("ADDI", 15, 0, 0, MEM_BASE_B))

    body_start = len(prog)
    total = body_start + n
    for _ in range(n):
        roll = rng.random()
        if roll < 0.18:                      # LOAD
            dest = rng.choice(ALU_DEST)
            base = rng.choice([14, 15])
            prog.append(("LOAD", dest, base, 0, rng.randint(0, 31)))
        elif roll < 0.34:                    # STORE (data reg in DEST field)
            data = rng.choice(ALU_DEST)
            base = rng.choice([14, 15])
            prog.append(("STORE", data, base, 0, rng.randint(0, 31)))
        elif roll < 0.50:                    # forward branch (BEQ/BNE)
            op = rng.choice(["BEQ", "BNE"])
            s1 = rng.randint(0, 13)
            s2 = rng.randint(0, 13)
            here = len(prog)
            tgt = min(total, here + 1 + rng.randint(1, 6))   # strictly forward
            prog.append((op, 0, s1, s2, ("T", tgt)))
        else:                                # ALU
            op = rng.choice(ALU_OPS)
            dest = rng.choice(ALU_DEST)
            s1 = rng.randint(0, 13)
            s2 = rng.randint(0, 13)
            imm = rng.randint(-15, 15)
            prog.append((op, dest, s1, s2, imm))
    return prog


def render(prog):
    lines = []
    for i, (op, d, s1, s2, imm) in enumerate(prog):
        pc = BASE_PC + 4 * i
        if isinstance(imm, tuple) and imm[0] == "T":
            imm = BASE_PC + 4 * imm[1]
        lines.append(f"0x{pc:04X} {op} {d} {s1} {s2} {imm}")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=200, help="number of random programs")
    ap.add_argument("--seed", type=int, default=1, help="base seed")
    ap.add_argument("--min", type=int, default=20, help="min body instructions")
    ap.add_argument("--max", type=int, default=80, help="max body instructions")
    ap.add_argument("--outdir", default=OUT)
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    # clear stale traces
    for f in os.listdir(args.outdir):
        if f.startswith("rand_") and f.endswith(".trace"):
            os.remove(os.path.join(args.outdir, f))

    for k in range(args.count):
        seed = args.seed + k
        rng = random.Random(seed)
        n = rng.randint(args.min, args.max)
        prog = gen_program(rng, n)
        with open(os.path.join(args.outdir, f"rand_{seed:05d}.trace"), "w") as f:
            f.write(f"# random seed={seed} body={n}\n")
            f.write(render(prog))
    print(f"Wrote {args.count} random programs (seeds {args.seed}..{args.seed + args.count - 1}) "
          f"to {args.outdir}/")


if __name__ == "__main__":
    main()
