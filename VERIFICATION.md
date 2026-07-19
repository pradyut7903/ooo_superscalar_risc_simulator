# Verification

Checked against a simple in-order model, plus directed and random traces.

```bash
cmake -S . -B build && cmake --build build -j
python tools/verify.py          # ~15 s; fails nonzero on mismatch
```

RV32IM hex:

```bash
python tools/run_imported_sim.py --smoke
python tools/run_imported_sim.py
```

## Golden model

`golden/golden_model.cpp` is in-order, one instr at a time — no rename, no
speculation. Shares `src/trace_loader.cpp` with the OoO sim.

Run the same `.trace` on both, dump regs + mem, compare. `verify.py` Phase 1
also checks golden against hand-written `.expect` files.

```bash
diff <(./build/golden_sim.exe --quiet --dump benchmarks/matmul.trace) \
     <(./build/pipeline_sim.exe --quiet --dump benchmarks/matmul.trace)
```

Hex images use `tools/golden_rv32im.py` the same way.

## Directed tests

`tests/gen_directed.py` — small traces for specific hazards, each vs golden.

| Test | Intent |
|------|--------|
| `waw_youngest` | three writers of R1, then a reader → youngest value |
| `war_hazard` | read then later write → reader keeps old value |
| `branch_flush_skip_garbage` | mispredict taken; speculative R5 write squashed |
| `branch_nottaken_fallthrough` | mispredict the other way |
| `store_load_forward` | store then load same addr → forwarded data |
| `store_load_disjoint` / `store_overwrite` | independent load; younger store wins |
| `mem_order_violation` | load past older store with unknown addr → recover |
| `long_latency_then_dependent` | wait on a 10-cycle DIV |
| others | DIV/0, signed math, ROB fill, deps, loops |

```bash
python tests/gen_directed.py
```

## Random programs

`tools/gen_random_tests.py` — seeded legal programs vs golden.

- Forward branches only (always halt)
- Mem ops use fixed base regs (R14/R15)
- Small immediates

Default 200 seeds; `--random-count` for more.

## Config invariance

Same arch state across width / queue / latency / forwarding configs
(`verify.py` Phase 3). Caught the width-`matmul` bug and a `--no-forward`
stale-load case.

## Pipeline dump

`--pipeline-trace FILE` dumps structure state every cycle (for latency checks).

```bash
printf '0x1000 ADDI 1 0 0 6\n0x1004 ADDI 2 0 0 7\n0x1008 MUL 3 1 2 0\n' > tests/mul_trace_demo.trace
./build/pipeline_sim.exe --quiet --pipeline-trace mul.log tests/mul_trace_demo.trace
```

## Assertions / coverage

C++ `assert`s plus `--selfcheck` invariants. Coverage phase fails if the corpus
never hit mispredicts, mem-order violations, or the various stall reasons.

## Bugs caught here

1. Mem-order recovery used a full flush and killed an older DIV that still
   owned a store address. Now squashes only the load and younger work.
2. With `--no-forward`, loads could read mem before an older same-addr store
   committed. Now they wait.

More OoO bugs from the performance benches: `EXPERIMENTS.md`.

## Files

```
golden/                     toy-trace ISS
tools/golden_rv32im.py      hex ISS
tests/gen_directed.py
tools/gen_random_tests.py
tools/verify.py
tools/run_imported_sim.py
```
