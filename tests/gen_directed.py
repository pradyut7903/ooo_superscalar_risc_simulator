#!/usr/bin/env python3
"""Generate DIRECTED edge-case verification traces.

Unlike the performance benchmarks, these are hand-crafted to drive the out-of-order
logic into specific worst-case hazard scenarios (RAW/WAW/WAR, branch flush, memory
disambiguation/forwarding, ...). Each is tiny and is checked against the golden model
(golden_sim) by tools/verify.py; the comment on each documents the expected key result
so the intent is human-readable too.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from gen_benchmarks import Asm, write_benchmark  # reuse the assembler + writer

OUT = os.path.join(os.path.dirname(__file__), "directed")


def t_raw_chain():
    """RAW: a strict producer->consumer chain. Each op must wait for the previous."""
    a = Asm()
    a.addi(1, 0, 5)      # R1 = 5
    a.add(2, 1, 1)       # R2 = R1+R1 = 10   (needs R1)
    a.mul(3, 2, 1)       # R3 = R2*R1 = 50   (needs R2,R1)
    a.sub(4, 3, 2)       # R4 = R3-R2 = 40   (needs R3,R2)
    return a, {1: 5, 2: 10, 3: 50, 4: 40}, "RAW dependency chain"


def t_waw_youngest():
    """WAW + RAW: three writers of R1; the reader must see the YOUNGEST (=3),
    and the ROB must commit R1's final value (=3) to the ARF in program order."""
    a = Asm()
    a.addi(1, 0, 1)      # R1 = 1
    a.addi(1, 0, 2)      # R1 = 2
    a.addi(1, 0, 3)      # R1 = 3   (youngest writer wins)
    a.add(2, 1, 0)       # R2 = R1 = 3
    return a, {1: 3, 2: 3}, "WAW: youngest writer wins; in-order commit"


def t_war_hazard():
    """WAR: a reader of R1 followed by a later writer of R1. Renaming must let the
    reader keep the OLD value (10) while R1 architecturally becomes 20."""
    a = Asm()
    a.addi(1, 0, 10)     # R1 = 10
    a.add(2, 1, 0)       # R2 = R1 = 10   (reads OLD R1)
    a.addi(1, 0, 20)     # R1 = 20        (WAR against the read above)
    a.add(3, 1, 0)       # R3 = R1 = 20
    return a, {1: 20, 2: 10, 3: 20}, "WAR: reader keeps old value under renaming"


def t_branch_flush_skip_garbage():
    """Branch flush: an always-taken forward branch skips a 'garbage' write to R5.
    On first encounter the BTB misses, so the core SPECULATIVELY fetches the garbage
    write, then must squash it when the branch resolves taken. R5 must stay 100."""
    a = Asm()
    a.addi(5, 0, 100)            # R5 = 100  (correct value)
    a.beq(0, 0, "skip")          # R0==R0 -> always taken -> skip garbage
    a.addi(5, 0, 999)            # GARBAGE: must be squashed, never committed
    a.label("skip")
    a.addi(6, 0, 7)              # R6 = 7
    return a, {5: 100, 6: 7}, "branch flush squashes wrong-path write to R5"


def t_branch_nottaken_fallthrough():
    """Mispredict the other direction: a never-taken branch whose fall-through path
    is the correct one. Exercises the not-taken resolution and recovery PC."""
    a = Asm()
    a.addi(1, 0, 5)
    a.bne(1, 1, "tgt")           # R1==R1 so BNE is NOT taken -> fall through
    a.addi(2, 0, 42)             # correct path: R2 = 42
    a.beq(0, 0, "end")           # skip the target block
    a.label("tgt")
    a.addi(2, 0, 777)            # wrong path: must never run
    a.label("end")
    a.addi(3, 0, 1)
    return a, {1: 5, 2: 42, 3: 1}, "not-taken branch keeps fall-through path"


def t_store_load_forward():
    """Memory disambiguation / forwarding: STORE then LOAD the SAME address. The load
    must observe the stored value (forwarded), not stale memory."""
    a = Asm()
    a.addi(1, 0, 77)             # R1 = 77
    a.addi(4, 0, 100)            # R4 = address 100
    a.store(1, 4, 0)             # MEM[100] = 77
    a.load(2, 4, 0)              # R2 = MEM[100] -> must be 77 (forwarded)
    a.add(3, 2, 2)              # R3 = 154 (use the loaded value)
    return a, {1: 77, 2: 77, 3: 154}, "store->load forwarding (same address)"


def t_store_load_disjoint():
    """Disambiguation: a load from a DIFFERENT address than an older store must NOT
    falsely depend on it, and must read the correct (preloaded) memory."""
    a = Asm()
    a.mem(200, 55)               # preload MEM[200]=55
    a.addi(1, 0, 9)
    a.addi(4, 0, 100)
    a.addi(5, 0, 200)
    a.store(1, 4, 0)             # MEM[100] = 9
    a.load(2, 5, 0)              # R2 = MEM[200] = 55  (independent of the store)
    return a, {1: 9, 2: 55}, "load disjoint from older store reads correct memory"


def t_store_overwrite():
    """Two stores to the same address; the YOUNGER store must win at memory."""
    a = Asm()
    a.addi(1, 0, 11)
    a.addi(2, 0, 22)
    a.addi(4, 0, 300)
    a.store(1, 4, 0)             # MEM[300] = 11
    a.store(2, 4, 0)             # MEM[300] = 22  (younger wins)
    a.load(3, 4, 0)             # R3 = 22
    return a, {1: 11, 2: 22, 3: 22}, "younger store wins; later load sees it"


def t_div_by_zero():
    """DIV by zero must produce 0 (defined behavior matching the golden model)."""
    a = Asm()
    a.addi(1, 0, 50)
    a.div(2, 1, 0)               # R2 = 50 / R0(0) -> 0
    a.div(3, 1, 1)              # R3 = 50/50 = 1  (R1/R1)
    return a, {1: 50, 2: 0, 3: 1}, "DIV by zero -> 0"


def t_negatives():
    """Negative immediates and subtraction producing negative results."""
    a = Asm()
    a.addi(1, 0, 3)
    a.addi(2, 0, 10)
    a.sub(3, 1, 2)               # R3 = 3 - 10 = -7
    a.addi(4, 1, -5)            # R4 = 3 + (-5) = -2
    return a, {1: 3, 2: 10, 3: -7, 4: -2}, "negative immediates / signed subtraction"


def t_rob_fill_independent():
    """Many independent ALU ops in flight at once: stresses ROB/RS occupancy and the
    CDB arbiter; the in-order commit must still reproduce every value."""
    a = Asm()
    # R1..R14 each set to a distinct constant, fully independent (max ROB pressure).
    expect = {}
    for r in range(1, 15):
        a.addi(r, 0, r * 7)
        expect[r] = r * 7
    return a, expect, "many independent ops fill the ROB"


def t_long_latency_then_dependent():
    """A slow DIV feeding a dependent ADD: the consumer must wait for the DIV's CDB
    broadcast (no early/!stale read)."""
    a = Asm()
    a.addi(1, 0, 100)
    a.addi(2, 0, 4)
    a.div(3, 1, 2)               # R3 = 25 (10-cycle DIV)
    a.add(4, 3, 1)              # R4 = 25 + 100 = 125 (must wait for DIV)
    a.mul(5, 3, 3)              # R5 = 625
    return a, {1: 100, 2: 4, 3: 25, 4: 125, 5: 625}, "dependent op waits for long-latency DIV"


def t_mem_order_violation():
    """Conservative load wait: STORE address depends on a slow DIV; younger LOAD to the
    same address is known early. Load must not issue until the older store is resolved,
    then forward — no MOV flush. Architecturally R2 == stored value (100)."""
    a = Asm()
    a.addi(1, 0, 100)            # R1 = 100 (data to store)
    a.addi(3, 0, 50)
    a.addi(4, 0, 5)
    a.div(5, 3, 4)               # R5 = 10, resolves LATE -> store addr
    a.addi(6, 0, 10)             # R6 = 10 (load address, known immediately)
    a.store(1, 5, 0)             # MEM[R5=10] = 100
    a.load(2, 6, 0)              # R2 = MEM[10]; waits for older store, then forwards
    return a, {1: 100, 3: 50, 4: 5, 5: 10, 6: 10, 2: 100}, "conservative late-store then load"


def t_loop_accumulate():
    """A small counted loop (backward branch) -- the case that originally deadlocked.
    Sum 1..20 = 210."""
    a = Asm()
    a.addi(2, 0, 20)             # N
    a.addi(1, 0, 0)              # i
    a.label("loop")
    a.addi(1, 1, 1)              # i++
    a.add(3, 3, 1)              # sum += i
    a.bne(1, 2, "loop")
    return a, {1: 20, 2: 20, 3: 210}, "counted loop accumulate (backward branch)"


TESTS = [
    t_raw_chain, t_waw_youngest, t_war_hazard,
    t_branch_flush_skip_garbage, t_branch_nottaken_fallthrough,
    t_store_load_forward, t_store_load_disjoint, t_store_overwrite,
    t_div_by_zero, t_negatives, t_rob_fill_independent,
    t_long_latency_then_dependent, t_mem_order_violation, t_loop_accumulate,
]


def main():
    os.makedirs(OUT, exist_ok=True)
    for fn in TESTS:
        name = fn.__name__[2:]   # strip "t_"
        asm, regs, note = fn()
        write_benchmark(OUT, name, asm, regs, {}, note)
        print(f"  {name:30s} {len(asm.insts):2d} insts  -- {note}")
    print(f"Wrote {len(TESTS)} directed tests to {OUT}/")


if __name__ == "__main__":
    main()
