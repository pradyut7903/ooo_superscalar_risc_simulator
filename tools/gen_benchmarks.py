#!/usr/bin/env python3
"""Generate realistic benchmark traces for the pipeline simulator.

Each kernel is emitted as a `.trace` file (PC OPCODE DEST SRC1 SRC2 [IMM], with
`.mem ADDR VALUE` data-segment directives) plus a companion `.expect` file listing
the architectural state the simulator must reproduce (used by check_benchmarks.py).

ISA recap (see AGENT_CONTEXT.md / README):
  ADD/SUB/MUL/DIV  DEST = R[SRC1] op R[SRC2]
  ADDI             DEST = R[SRC1] + IMM
  LOAD             DEST = MEM[R[SRC1] + IMM]
  STORE            MEM[R[SRC1] + IMM] = R[DEST]
  BEQ/BNE          if R[SRC1] (==|!=) R[SRC2] goto IMM   (IMM = absolute target PC)
  R0 is hardwired to 0.  Memory is a flat int array (address == word index).
"""

import argparse
import os

BASE_PC = 0x1000
INSTR = 4


class Asm:
    """Tiny assembler: tracks PCs, resolves symbolic branch targets to PCs."""

    def __init__(self, base=BASE_PC):
        self.base = base
        self.insts = []          # [op, dest, src1, src2, imm_or_(LBL,name)]
        self.labels = {}         # name -> instruction index
        self.data = {}           # address -> value  (.mem directives)

    # control / bookkeeping -------------------------------------------------
    def label(self, name):
        self.labels[name] = len(self.insts)

    def mem(self, addr, value):
        self.data[addr] = value

    # instructions ----------------------------------------------------------
    def addi(self, d, s1, imm):   self.insts.append(["ADDI", d, s1, 0, imm])
    def add(self, d, s1, s2):     self.insts.append(["ADD", d, s1, s2, 0])
    def sub(self, d, s1, s2):     self.insts.append(["SUB", d, s1, s2, 0])
    def mul(self, d, s1, s2):     self.insts.append(["MUL", d, s1, s2, 0])
    def div(self, d, s1, s2):     self.insts.append(["DIV", d, s1, s2, 0])
    def load(self, d, base_reg, off):  self.insts.append(["LOAD", d, base_reg, 0, off])
    def store(self, data_reg, base_reg, off): self.insts.append(["STORE", data_reg, base_reg, 0, off])
    def bne(self, s1, s2, lbl):   self.insts.append(["BNE", 0, s1, s2, ("LBL", lbl)])
    def beq(self, s1, s2, lbl):   self.insts.append(["BEQ", 0, s1, s2, ("LBL", lbl)])

    # output ----------------------------------------------------------------
    def render(self):
        lines = []
        for addr in sorted(self.data):
            lines.append(f".mem {addr} {self.data[addr]}")
        for i, (op, d, s1, s2, imm) in enumerate(self.insts):
            if isinstance(imm, tuple) and imm[0] == "LBL":
                imm = self.base + INSTR * self.labels[imm[1]]
            pc = self.base + INSTR * i
            lines.append(f"0x{pc:04X} {op} {d} {s1} {s2} {imm}")
        return "\n".join(lines) + "\n"


def write_benchmark(outdir, name, asm, expect_regs=None, expect_mem=None, note=""):
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, name + ".trace"), "w") as f:
        if note:
            f.write(f"# {name}: {note}\n")
        f.write(asm.render())
    with open(os.path.join(outdir, name + ".expect"), "w") as f:
        f.write(f"# expected architectural state for {name}\n")
        for r in sorted((expect_regs or {})):
            f.write(f"R{r} {expect_regs[r]}\n")
        for a in sorted((expect_mem or {})):
            f.write(f"M{a} {expect_mem[a]}\n")
    # dynamic instruction estimate via a quick functional model is overkill;
    # the simulator's own --stats reports the real committed count.


# ---------------------------------------------------------------------------
# Kernels
# ---------------------------------------------------------------------------
def gen_vecadd(N=512, A=4096, B=8192, C=12288):
    """C[i] = A[i] + B[i].  Memory-bandwidth / LSQ stress, independent iterations."""
    a = Asm()
    av = [(i % 97) + 1 for i in range(N)]
    bv = [(i % 89) + 3 for i in range(N)]
    for i in range(N):
        a.mem(A + i, av[i])
        a.mem(B + i, bv[i])
    # R1=i, R2=N
    a.addi(2, 0, N)
    a.addi(1, 0, 0)
    a.label("loop")
    a.load(5, 1, A)        # R5 = A[i]
    a.load(6, 1, B)        # R6 = B[i]
    a.add(7, 5, 6)         # R7 = A[i]+B[i]
    a.store(7, 1, C)       # C[i] = R7
    a.addi(1, 1, 1)        # i++
    a.bne(1, 2, "loop")
    mem = {C + i: av[i] + bv[i] for i in range(N)}
    return a, {1: N, 2: N}, mem, f"C[i]=A[i]+B[i], N={N}"


def gen_dotprod(N=512, A=4096, B=8192):
    """sum = sum(A[i]*B[i]).  Serial reduction: accumulator dependency limits ILP."""
    a = Asm()
    av = [(i % 7) + 1 for i in range(N)]
    bv = [(i % 5) + 1 for i in range(N)]
    for i in range(N):
        a.mem(A + i, av[i])
        a.mem(B + i, bv[i])
    a.addi(2, 0, N)        # N
    a.addi(1, 0, 0)        # i
    a.addi(3, 0, 0)        # sum
    a.label("loop")
    a.load(5, 1, A)
    a.load(6, 1, B)
    a.mul(7, 5, 6)
    a.add(3, 3, 7)         # sum += A[i]*B[i]  (serial dep on R3)
    a.addi(1, 1, 1)
    a.bne(1, 2, "loop")
    s = sum(av[i] * bv[i] for i in range(N))
    return a, {1: N, 2: N, 3: s}, {}, f"dot product, N={N} (serial accumulate)"


def gen_dotprod_unroll(N=512, A=4096, B=8192):
    """Dot product with 4 independent accumulators -> exposes ILP (vs serial)."""
    assert N % 4 == 0
    a = Asm()
    av = [(i % 7) + 1 for i in range(N)]
    bv = [(i % 5) + 1 for i in range(N)]
    for i in range(N):
        a.mem(A + i, av[i])
        a.mem(B + i, bv[i])
    a.addi(2, 0, N)        # N
    a.addi(1, 0, 0)        # i
    a.addi(3, 0, 0)        # acc0
    a.addi(4, 0, 0)        # acc1
    a.addi(8, 0, 0)        # acc2  (R5..R7,R9..R12 used as temps)
    a.addi(9, 0, 0)        # acc3
    a.label("loop")
    # lane 0
    a.load(5, 1, A); a.load(6, 1, B); a.mul(7, 5, 6); a.add(3, 3, 7)
    a.addi(1, 1, 1)
    # lane 1
    a.load(5, 1, A); a.load(6, 1, B); a.mul(7, 5, 6); a.add(4, 4, 7)
    a.addi(1, 1, 1)
    # lane 2
    a.load(5, 1, A); a.load(6, 1, B); a.mul(7, 5, 6); a.add(8, 8, 7)
    a.addi(1, 1, 1)
    # lane 3
    a.load(5, 1, A); a.load(6, 1, B); a.mul(7, 5, 6); a.add(9, 9, 7)
    a.addi(1, 1, 1)
    a.bne(1, 2, "loop")
    a.add(3, 3, 4)
    a.add(8, 8, 9)
    a.add(3, 3, 8)         # R3 = total
    s = sum(av[i] * bv[i] for i in range(N))
    return a, {1: N, 2: N, 3: s}, {}, f"dot product 4x unrolled, N={N} (ILP)"


def gen_matmul(Nm=12, A=4096, B=8192, C=12288):
    """C = A*B for NmxNm matrices, row-major.  Compute-heavy: MUL + loads + ILP."""
    a = Asm()
    av = [[(r * Nm + c) % 6 + 1 for c in range(Nm)] for r in range(Nm)]
    bv = [[(r * Nm + c) % 4 + 1 for c in range(Nm)] for r in range(Nm)]
    for r in range(Nm):
        for c in range(Nm):
            a.mem(A + r * Nm + c, av[r][c])
            a.mem(B + r * Nm + c, bv[r][c])
    # Registers: R1=i, R2=j, R3=k, R4=Nm, R5=acc, R6=&A row base(i*Nm), R7..R12 temps
    a.addi(4, 0, Nm)
    a.addi(1, 0, 0)            # i
    a.label("i_loop")
    a.mul(6, 1, 4)            # R6 = i*Nm  (A row base offset)
    a.addi(2, 0, 0)            # j
    a.label("j_loop")
    a.addi(5, 0, 0)            # acc = 0
    a.addi(3, 0, 0)            # k
    a.label("k_loop")
    # A[i][k] at A + i*Nm + k  -> base reg R6 (=i*Nm), offset via R3(k): need R6+R3
    a.add(10, 6, 3)           # R10 = i*Nm + k
    a.load(7, 10, A)          # R7 = A[i][k]
    # B[k][j] at B + k*Nm + j
    a.mul(11, 3, 4)           # R11 = k*Nm
    a.add(11, 11, 2)          # R11 = k*Nm + j
    a.load(8, 11, B)          # R8 = B[k][j]
    a.mul(9, 7, 8)            # R9 = A*B
    a.add(5, 5, 9)            # acc += R9
    a.addi(3, 3, 1)           # k++
    a.bne(3, 4, "k_loop")
    # C[i][j] = acc, at C + i*Nm + j
    a.add(12, 6, 2)           # R12 = i*Nm + j
    a.store(5, 12, C)
    a.addi(2, 2, 1)           # j++
    a.bne(2, 4, "j_loop")
    a.addi(1, 1, 1)           # i++
    a.bne(1, 4, "i_loop")
    cv = {}
    for r in range(Nm):
        for c in range(Nm):
            cv[C + r * Nm + c] = sum(av[r][k] * bv[k][c] for k in range(Nm))
    return a, {1: Nm, 4: Nm}, cv, f"{Nm}x{Nm} matrix multiply"


def gen_ptrchase(N=512, HEAD=2000, stride=7):
    """Linked-list traversal: next = MEM[cur].  Pure load-use serial chain (MLP=1),
    memory-latency bound -- the headline benchmark for the load-latency sweep."""
    a = Asm()
    # Build a cycle of N nodes: node address sequence p0 -> p1 -> ... -> p0.
    addrs = [(HEAD + (i * stride) % (N)) for i in range(N)]
    # ensure unique addresses
    seen, uniq = set(), []
    x = HEAD
    for i in range(N):
        while x in seen:
            x += 1
        seen.add(x); uniq.append(x); x += stride
    addrs = uniq
    for i in range(N):
        a.mem(addrs[i], addrs[(i + 1) % N])    # each node points to the next
    # R1 = steps remaining (N), R2 = current pointer = addrs[0]
    a.addi(1, 0, N)
    a.addi(2, 0, addrs[0])
    a.label("loop")
    a.load(2, 2, 0)            # R2 = MEM[R2]   (serial load-use dependency)
    a.addi(1, 1, -1)          # steps--
    a.bne(1, 0, "loop")       # while steps != 0
    # after N steps we return to the start pointer
    return a, {1: 0, 2: addrs[0]}, {}, f"pointer chase, {N} nodes (load-latency bound)"


def gen_saxpy(N=512, X=4096, Y=8192, ascalar=3):
    """y[i] = a*x[i] + y[i].  Mixed MUL + load/store, independent iterations."""
    a = Asm()
    xv = [(i % 11) + 1 for i in range(N)]
    yv = [(i % 13) + 2 for i in range(N)]
    for i in range(N):
        a.mem(X + i, xv[i])
        a.mem(Y + i, yv[i])
    a.addi(2, 0, N)            # N
    a.addi(1, 0, 0)            # i
    a.addi(3, 0, ascalar)      # a
    a.label("loop")
    a.load(5, 1, X)
    a.mul(6, 5, 3)            # a*x[i]
    a.load(7, 1, Y)
    a.add(8, 6, 7)            # a*x[i] + y[i]
    a.store(8, 1, Y)
    a.addi(1, 1, 1)
    a.bne(1, 2, "loop")
    mem = {Y + i: ascalar * xv[i] + yv[i] for i in range(N)}
    return a, {1: N, 2: N, 3: ascalar}, mem, f"saxpy y=a*x+y, N={N}, a={ascalar}"


def gen_ilp_serial(N=2000):
    """One long dependent ADDI chain: R1 += 1, N times.  IPC floor (no ILP)."""
    a = Asm()
    a.addi(2, 0, N)
    a.addi(1, 0, 0)
    a.label("loop")
    a.addi(1, 1, 1)           # nothing else; pure serial counter
    a.add(3, 3, 1)            # R3 depends on R3 and R1 -> serial too
    a.bne(1, 2, "loop")
    s = sum(range(1, N + 1))
    return a, {1: N, 2: N, 3: s}, {}, f"serial dependency chain, N={N}"


def gen_ilp_parallel(N=500):
    """Four independent counters updated per iteration: high ILP -> IPC ceiling."""
    a = Asm()
    a.addi(2, 0, N)
    a.addi(1, 0, 0)
    a.label("loop")
    a.addi(3, 3, 1)           # independent
    a.addi(4, 4, 2)           # independent
    a.addi(5, 5, 3)           # independent
    a.addi(6, 6, 4)           # independent
    a.addi(1, 1, 1)           # loop counter
    a.bne(1, 2, "loop")
    return a, {1: N, 2: N, 3: N, 4: 2 * N, 5: 3 * N, 6: 4 * N}, {}, f"4 independent chains, N={N}"


def gen_mul_heavy(N=400):
    """Back-to-back independent MULs -> saturates the (single) MUL unit."""
    a = Asm()
    a.addi(2, 0, N)
    a.addi(1, 0, 0)
    a.addi(10, 0, 3)
    a.addi(11, 0, 5)
    a.label("loop")
    a.mul(3, 10, 11)         # independent products (overwritten each iter)
    a.mul(4, 10, 11)
    a.mul(5, 10, 11)
    a.mul(6, 10, 11)
    a.addi(1, 1, 1)
    a.bne(1, 2, "loop")
    return a, {1: N, 2: N, 3: 15, 4: 15, 5: 15, 6: 15, 10: 3, 11: 5}, {}, f"MUL-bound, N={N}"


def gen_div_heavy(N=200):
    """Back-to-back independent DIVs -> saturates the (single, slow) DIV unit."""
    a = Asm()
    a.addi(2, 0, N)
    a.addi(1, 0, 0)
    a.addi(10, 0, 100)
    a.addi(11, 0, 7)
    a.label("loop")
    a.div(3, 10, 11)
    a.div(4, 10, 11)
    a.div(5, 10, 11)
    a.addi(1, 1, 1)
    a.bne(1, 2, "loop")
    q = 100 // 7
    return a, {1: N, 2: N, 3: q, 4: q, 5: q, 10: 100, 11: 7}, {}, f"DIV-bound, N={N}"


KERNELS = {
    "vecadd": gen_vecadd,
    "dotprod": gen_dotprod,
    "dotprod_unroll": gen_dotprod_unroll,
    "matmul": gen_matmul,
    "ptrchase": gen_ptrchase,
    "saxpy": gen_saxpy,
    "ilp_serial": gen_ilp_serial,
    "ilp_parallel": gen_ilp_parallel,
    "mul_heavy": gen_mul_heavy,
    "div_heavy": gen_div_heavy,
}


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ap = argparse.ArgumentParser(description="Generate pipeline-simulator benchmarks")
    ap.add_argument("--outdir", default=os.path.join(root, "benchmarks"))
    ap.add_argument("--only", nargs="*", help="generate only these kernels")
    args = ap.parse_args()

    names = args.only if args.only else list(KERNELS)
    for name in names:
        asm, regs, mem, note = KERNELS[name]()
        write_benchmark(args.outdir, name, asm, regs, mem, note)
        ninst = len(asm.insts)
        ndata = len(asm.data)
        print(f"  {name:16s} {ninst:4d} static insts, {ndata:5d} data words  -- {note}")
    print(f"Wrote {len(names)} benchmark(s) to {args.outdir}/")


if __name__ == "__main__":
    main()
