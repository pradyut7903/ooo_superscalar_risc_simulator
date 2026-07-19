# ooo_simulator

Cycle-accurate out-of-order RV32IM simulator. Same microarchitecture as
[ARCHITECTURE.md](ARCHITECTURE.md): Tomasulo with ROB-tag rename, early
mispredict recovery, CDB, LSQ + store buffer, optional caches.

## Build

```powershell
cmake -S . -B build
cmake --build build -j
```

Output: `build/pipeline_sim` (`.exe` on Windows).

## Run checks

```powershell
python tools/run_imported_sim.py --smoke
python tools/run_imported_sim.py
python tools/run_imported_sim.py --mem-system cached

.\build\pipeline_sim.exe --quiet --csv coremark --max-cycles 5000000 --mem-system cached `
  --imem tb\workloads_hex\coremark.imem.hex `
  --dmem tb\workloads_hex\coremark.dmem.hex
```

## Branch prediction

`--bp gshare` (default) — PHT + BTB, no RAS at fetch  
`--bp always-taken` — PC-relative always taken; RAS for call/return

```powershell
python tools/bp_compare.py    # -> results/BP_COMPARE.md
```

## Layout

| Path | What |
|------|------|
| `src/`, `include/` | simulator |
| `golden/` | in-order model for `.trace` files |
| `tools/` | verify, sweeps, golden ISS, BP compare |
| `tb/imported/` | small RV32IM hex tests |
| `tb/workloads_hex/` | larger hex + CoreMark |
| `workloads/coremark/` | CoreMark port + upstream |
| `results/` | sizing + BP compare reports |

See `./build/pipeline_sim --help` for knobs.
