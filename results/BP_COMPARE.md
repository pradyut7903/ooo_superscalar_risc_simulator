# Branch predictor comparison

- Date: 2026-07-19 20:06 UTC
- Config: best cached (width 4, ALU 3, mul-lat 2, line 64, …)
- Modes:
  - `gshare` — PHT + BTB; RAS unused at fetch
  - `always-taken` — PC-relative branches/JAL always taken; RAS for call/return
- CSV: `results/bp_compare.csv`

## Per-program

| Program | BP | Result | Commits | Cycles | IPC | Br resolved | Mispredicts | Mispred % |
|---|---|---|---:|---:|---:|---:|---:|---:|
| branch | gshare | PASS | 8200 | 19583 | 0.4187 | 4096 | 1041 | 25.41 |
| branch | always-taken | PASS | 8200 | 22576 | 0.3632 | 4096 | 1027 | 25.07 |
| custom_riscv | gshare | PASS | 12906 | 20839 | 0.6193 | 2574 | 40 | 1.55 |
| custom_riscv | always-taken | PASS | 12906 | 20685 | 0.6239 | 2574 | 5 | 0.19 |
| riscv_mem | gshare | PASS | 8950 | 10921 | 0.8195 | 1274 | 48 | 3.77 |
| riscv_mem | always-taken | PASS | 8950 | 10701 | 0.8364 | 1274 | 5 | 0.39 |
| ilp_loop | gshare | PASS | 40969 | 49237 | 0.8321 | 4096 | 10 | 0.24 |
| ilp_loop | always-taken | PASS | 40969 | 49193 | 0.8328 | 4096 | 1 | 0.02 |
| matmul8 | gshare | PASS | 7780 | 11021 | 0.7059 | 584 | 82 | 14.04 |
| matmul8 | always-taken | PASS | 7780 | 11147 | 0.6979 | 584 | 73 | 12.50 |
| mem_stream | gshare | PASS | 98568 | 132215 | 0.7455 | 16448 | 143 | 0.87 |
| mem_stream | always-taken | PASS | 98568 | 131757 | 0.7481 | 16448 | 65 | 0.40 |
| mix_bench | gshare | PASS | 47269 | 86552 | 0.5461 | 10272 | 4136 | 40.26 |
| mix_bench | always-taken | PASS | 47269 | 78306 | 0.6036 | 10272 | 2081 | 20.26 |
| coremark | gshare | PASS | 405279 | 748781 | 0.5413 | 108915 | 25995 | 23.87 |
| coremark | always-taken | PASS | 405279 | 874296 | 0.4635 | 111440 | 40178 | 36.05 |

## Side-by-side (IPC / mispred %)

| Program | gshare IPC | always-taken IPC | Δ IPC | gshare mis% | always-taken mis% | Δ mis% |
|---|---:|---:|---:|---:|---:|---:|
| branch | 0.4187 | 0.3632 | -0.0555 | 25.41 | 25.07 | -0.34 |
| custom_riscv | 0.6193 | 0.6239 | +0.0046 | 1.55 | 0.19 | -1.36 |
| riscv_mem | 0.8195 | 0.8364 | +0.0168 | 3.77 | 0.39 | -3.38 |
| ilp_loop | 0.8321 | 0.8328 | +0.0007 | 0.24 | 0.02 | -0.22 |
| matmul8 | 0.7059 | 0.6979 | -0.0080 | 14.04 | 12.50 | -1.54 |
| mem_stream | 0.7455 | 0.7481 | +0.0026 | 0.87 | 0.40 | -0.47 |
| mix_bench | 0.5461 | 0.6036 | +0.0575 | 40.26 | 20.26 | -20.01 |
| coremark | 0.5413 | 0.4635 | -0.0777 | 23.87 | 36.05 | +12.19 |

## Summary

- gshare IPC geomean: **0.6382** (8 PASS)
- always-taken IPC geomean: **0.6243** (8 PASS)

Mispred % = `100 * mispredicts / branches_resolved` (includes wrong-path
resolves, so absolute branch counts can differ slightly between modes).

On this suite, gshare wins geomean IPC. always-taken helps taken-heavy loops
(`mix_bench`, `riscv_mem`) and hurts when many branches are not taken
(`branch`, CoreMark). Use `--bp gshare` or `--bp always-taken`.
