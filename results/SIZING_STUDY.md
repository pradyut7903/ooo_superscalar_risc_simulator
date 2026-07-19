# IPC sizing study

- Date: 2026-07-19
- Mem: cached, simple DRAM
- Suite: branch, custom_riscv, riscv_mem, ilp_loop, matmul8, mem_stream, mix_bench
- Method: one knob at a time from baseline, then combine the winners
- CSV: `results/sizing_study.csv`

Re-run: `python tools/ipc_sizing_study.py`

## Baseline

Study geomean: **0.634**

| Program | IPC | Cycles | Commits |
|---|---:|---:|---:|
| branch | 0.418 | 19608 | 8200 |
| custom_riscv | 0.616 | 20939 | 12906 |
| riscv_mem | 0.672 | 13309 | 8950 |
| ilp_loop | 0.832 | 49271 | 40969 |
| matmul8 | 0.704 | 11054 | 7780 |
| mem_stream | 0.745 | 132328 | 98568 |
| mix_bench | 0.546 | 86642 | 47269 |

```
--mem-system cached
--width 4 --cdb 4
--rob 32 --rs 32 --lsq 32 --ifq 32
--alu 2 --mul 1 --div 1 --br 1 --lsq-cdb 2
--alu-lat 1 --mul-lat 3 --div-lat 10 --br-lat 1
--pht 1024 --btb 256 --ras 16 --sb 8
--dram-lat 10 --dram-out 4 --cache-line 32
--dcache-sets 16 --dcache-ways 4
--icache-sets 16 --icache-ways 4
--dcache-mshr 4 --icache-mshr 2 --dcache-ufp 2
--load-hit-lat 1 --fetch-hit-lat 1
```

## Best value per axis

| Axis | Baseline | Best | Geo | Δ |
|---|---:|---:|---:|---:|
| `--width` | 4 | 8 | 0.754 | +0.120 |
| `--cache-line` | 32 | 64 | 0.652 | +0.018 |
| `--pht` | 1024 | 256 | 0.635 | +0.001 |
| `--dram-lat` | 10 | 5 | 0.635 | +0.001 |
| `--mul-lat` | 3 | 5 | 0.634 | ~0 |
| `--alu` | 2 | 3 | 0.634 | ~0 |
| `--cdb` / `--rob` / `--rs` / `--lsq` / `--dcache-ufp` | (baseline) | same | 0.634 | 0 |

Width dominates. Cache line helps a bit. Most queue sizes are already past the
knee at the baseline.

## Per-axis detail

### `--width`

| Value | Geo |
|---:|---:|
| 1 | 0.249 |
| 2 | 0.434 |
| 4 | 0.634 |
| 8 | 0.754 ← best |

### `--cdb`

| Value | Geo |
|---:|---:|
| 1 | 0.634 |
| 2–8 | 0.634 |

### `--alu`

| Value | Geo |
|---:|---:|
| 1 | 0.634 |
| 2–4 | 0.634 |

### `--mul-lat`

| Value | Geo |
|---:|---:|
| 1–3 | 0.634 |
| 5 | 0.634 ← slight best |

### `--rob` / `--rs` / `--lsq`

16 / 32 / 64 all at 0.634 on this suite.

### `--pht`

| Value | Geo |
|---:|---:|
| 64 | 0.632 |
| 256 | 0.635 ← best |
| 1024 | 0.634 |

### `--cache-line`

| Value | Geo |
|---:|---:|
| 32 | 0.634 |
| 64 | 0.652 ← best |

### `--dram-lat`

| Value | Geo |
|---:|---:|
| 5 | 0.635 ← best |
| 10 | 0.634 |
| 20 | 0.633 |

### `--dcache-ufp`

| Value | Geo |
|---:|---:|
| 1 | 0.631 |
| 2 | 0.634 ← best |

## Combined winners

Geomean **0.830** (+0.196 vs baseline).

Changes: width 8, alu 3, mul-lat 5, pht 256, dram-lat 5, cache-line 64.

| Program | Baseline | Combined |
|---|---:|---:|
| branch | 0.418 | 0.419 |
| custom_riscv | 0.616 | 0.748 |
| riscv_mem | 0.672 | 0.994 |
| ilp_loop | 0.832 | 1.247 |
| matmul8 | 0.704 | 1.089 |
| mem_stream | 0.745 | 1.001 |
| mix_bench | 0.546 | 0.640 |

## Notes

- IPC = commits / cycles
- Width sweep clamps `--cdb` to `min(cdb, width)`
- One-at-a-time only; not a full factorial
