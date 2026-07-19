# CoreMark

Bare-metal [EEMBC CoreMark](https://github.com/eembc/coremark) for this
simulator. Same hex image the RTL uses.

## Memory map

| Region | Addr |
|--------|------|
| `.text` | `0x00000000` (IMEM) |
| `.rodata` / `.data` / `.bss` | `0x00002000` (DMEM) |
| stack top | `0x00008000` (DMEM) |

Halt word is `0xffffffff`, linked after all code so wide fetch does not treat
mid-program as EOP.

## Build

Needs `riscv-none-elf-gcc` on `PATH` (or under `tools/riscv-toolchain/...`).
Upstream sources are in `upstream/`.

```powershell
cd workloads\coremark
.\build.ps1 -Iterations 1 -OutDir ..\..\tb\workloads_hex
```

`ITERATIONS=1` is ~405k dynamic instructions. Higher values get slow fast.

## Run

From the package root:

```powershell
.\build\pipeline_sim.exe --quiet --csv coremark --max-cycles 5000000 --mem-system cached `
  --imem tb\workloads_hex\coremark.imem.hex `
  --dmem tb\workloads_hex\coremark.dmem.hex
```

Golden check (regs + mem):

```powershell
python tools/run_imported_sim.py --only coremark --max-cycles 5000000 --mem-system cached
```

Port timer is a stub — use sim IPC/commits, not CoreMark Iterations/Sec.
