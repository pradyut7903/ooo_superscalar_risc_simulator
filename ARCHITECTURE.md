# Architecture

Shared microarchitecture for the OoO RV32IM core. RTL lives in a separate
repo (`rtl/`); this one is the C++ cycle sim (`src/`, knobs via CLI /
`include/types.hpp`).

In-order fetch and commit, out-of-order issue/complete. No physical register
file — rename tags are ROB indices. Values sit in the ROB until commit into
the ARF; dependents wake on the CDB.

Defaults (RTL): `WIDTH=4`, `CDB_WIDTH=4` in `pkg_cpu.sv`. Top: `core.sv`.

## Modules

### Frontend

| Module | Role |
|--------|------|
| `branch_predictor` | gshare PHT + BTB; trained at commit |
| `fetch` | up to `WIDTH` instrs/cycle, predictions, redirects |
| `ifq` | fetch queue into decode |
| `decode` | RV32IM → uops; illegal/system/fence → NOP |
| `dispatch_reg` | holds a decoded bundle; prefix-accept into backend |
| `if_id_reg` / `id_rn_reg` | frontend pipe regs |

### Backend

| Module | Role |
|--------|------|
| `backend` | wires rename, ROB/RAT/ARF, RS, LSQ, FUs, CDB, recovery |
| `rename_dispatch` | rename in lane order, alloc ROB, send to RS or LSQ |
| `rat` | arch reg → ARF or ROB tag |
| `rat_checkpoints` | RAT snapshots for control-flow; restore on mispredict |
| `arf` | architectural regs; written at commit; `x0` = 0 |
| `rob` | reorder buffer; in-order commit; store-commit grant |
| `rs` | ALU/MUL/DIV/BR reservation stations; CDB wakeup |
| `cdb_arbiter` | up to `CDB_WIDTH` results/cycle |
| `early_recovery` | on mispredict: redirect, restore RAT, squash younger |
| `commit_recovery` | unused stub (kept for older TBs) |

### Execute

| Module | Role |
|--------|------|
| `alu` | integer / LUI / AUIPC (`ALU_STAGES`) |
| `mul` | multiply (`MUL_STAGES`) |
| `div` | div/rem (`DIV_STAGES`) |
| `branch_unit` | taken/target/mispredict |

### Memory

| Module | Role |
|--------|------|
| `lsq` | OoO LSQ + committed store buffer; forwarding + partial merge |
| `dmem` | ideal multi-port D-mem (ideal mode / unit tests) |
| `instr_mem` | ideal I-mem (ideal mode) |

### Cache / DRAM (`mem/`)

| Module | Role |
|--------|------|
| `icache` | non-blocking I$, MSHRs |
| `dcache` | non-blocking D$, MSHRs, up to 2 UFP ports |
| `mem_arbiter` | one DRAM grant/cycle; D$ preferred |
| `dram_model` | picks simple or banked |
| `dram_model_simple` | fixed line latency (default for studies) |
| `dram_model_banked` | open-row timing |
| `ideal_imem_bridge` | ideal I-mem when caches off |

`pkg_cpu` holds widths, depths, FU counts, cache/DRAM knobs, opcodes, structs.

## Design notes

**Rename.** RAT points at ARF or a ROB entry. Completions write the ROB and
broadcast `{tag, data}` on the CDB. Commit writes ARF and clears the RAT if it
still points at that tag.

**Bundles.** Fetch through commit are `WIDTH`-wide. CDB width is separate.
Backend accepts a prefix of lanes and holds the rest.

**Issue.** Non-mem ops in the RS, mem ops in the LSQ. Both wake from CDB.

**Store buffer.** Once a store has ROB commit permission it enters the SB and
frees its LSQ slot. ROB can retire without waiting on D$. SB drains in order
and is not killed by branch recovery. Loads check older LSQ stores, then SB,
then memory.

**Forwarding.** Full match → forward, no mem read. Partial → mem read + merge.

**Early recovery.** Branch unit mispredict → redirect fetch, restore RAT
checkpoint, squash younger ops. Commit only trains the predictor.

**Caches.** Split I$/D$ with MSHRs; shared DRAM; tagged D$ responses so
completes can return OoO. Ideal memories still available for bring-up.

**ISA.** RV32IM user integer only. No CSRs, traps, atomics, or real fences.
Misaligned accesses are not trapped; byte/half handled via LSQ masks.
