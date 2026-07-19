# ooo_simulator

Cycle-accurate out-of-order RV32IM simulator. Same microarchitecture as the RTL
core described in [ARCHITECTURE.md](ARCHITECTURE.md): Tomasulo with ROB-tag
rename, early mispredict recovery, CDB, LSQ + store buffer, optional caches.

## Build

```powershell
cmake -S . -B build
cmake --build build -j
```

Output: `build/pipeline_sim` (`.exe` on Windows). Build stays at `-O0` on
purpose — see `CMakeLists.txt`.

## Run checks

```powershell
python tools/run_imported_sim.py --smoke
python tools/run_imported_sim.py
python tools/run_imported_sim.py --mem-system cached

# CoreMark (~405k commits; give it a few million cycles)
.\build\pipeline_sim.exe --quiet --csv coremark --max-cycles 5000000 --mem-system cached `
  --imem tb\workloads_hex\coremark.imem.hex `
  --dmem tb\workloads_hex\coremark.dmem.hex
```

Toy-trace co-sim / fuzz:

```powershell
python tools/verify.py
```

## Layout

| Path | What |
|------|------|
| `src/`, `include/` | simulator |
| `golden/` | in-order model for `.trace` files |
| `tools/` | verify, sweeps, `golden_rv32im.py`, `elf_to_hex.py` |
| `tb/imported/` | small RV32IM hex tests |
| `tb/workloads_hex/` | larger hex + CoreMark |
| `workloads/coremark/` | CoreMark port + upstream |
| `results/` | sizing study output |

CLI knobs (width, ROB/RS/LSQ, FU counts/latencies, caches, DRAM):  
`./build/pipeline_sim --help`.

## CoreMark rebuild

Prebuilt hex is already in `tb/workloads_hex/` (`ITERATIONS=1`). Rebuild needs
`riscv-none-elf-gcc`:

```powershell
cd workloads\coremark
.\build.ps1 -Iterations 1 -OutDir ..\..\tb\workloads_hex
```

The timer in the port is a stub — report IPC/commits from the sim, not
CoreMark Iterations/Sec.

## Other docs

- [ARCHITECTURE.md](ARCHITECTURE.md)
- [VERIFICATION.md](VERIFICATION.md)
- [EXPERIMENTS.md](EXPERIMENTS.md) — older toy-trace sweeps
- [results/SIZING_STUDY.md](results/SIZING_STUDY.md) — hex/cached sizing
- [workloads/coremark/README.md](workloads/coremark/README.md)
