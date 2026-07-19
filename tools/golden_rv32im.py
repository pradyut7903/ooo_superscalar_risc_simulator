#!/usr/bin/env python3
"""Independent architectural golden model for RV32IM (user-level subset).

This is intentionally *not* the C++ ooo_simulator.  It is a simple in-order
ISA emulator used to check RTL commit streams and final architectural state.

Halt convention matches rtl_v2: instruction word 0xFFFFFFFF ends fetch/execution.
Illegal / unsupported encodings retire as architectural NOPs (no register write),
matching decode.sv.
"""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import asdict, dataclass
from pathlib import Path


INSTR_INVALID = 0xFFFF_FFFF
MASK32 = 0xFFFF_FFFF


def sext(value: int, bits: int) -> int:
    value &= (1 << bits) - 1
    sign = 1 << (bits - 1)
    return value - (1 << bits) if (value & sign) else value


def to_u32(value: int) -> int:
    return value & MASK32


def to_s32(value: int) -> int:
    value = to_u32(value)
    return value - 0x1_0000_0000 if value & 0x8000_0000 else value


@dataclass
class CommitEvent:
    pc: int
    instr: int
    rd_used: bool
    rd: int
    value: int
    is_store: bool = False
    store_addr: int = 0
    store_data: int = 0
    store_wstrb: int = 0


class Memory:
    def __init__(self, words: list[int] | None = None, size_bytes: int = 64 * 1024):
        self.size = size_bytes
        self.data = bytearray(size_bytes)
        if words:
            for i, w in enumerate(words):
                self.store_word(i * 4, w)

    def load_bytes(self, addr: int, n: int) -> int:
        addr &= MASK32
        if addr + n > self.size:
            raise ValueError(f"load OOB addr={addr:#x} n={n}")
        return int.from_bytes(self.data[addr : addr + n], "little")

    def store_bytes(self, addr: int, value: int, n: int) -> None:
        addr &= MASK32
        if addr + n > self.size:
            raise ValueError(f"store OOB addr={addr:#x} n={n}")
        self.data[addr : addr + n] = int.to_bytes(value & ((1 << (8 * n)) - 1), n, "little")

    def load_word(self, addr: int) -> int:
        return self.load_bytes(addr & ~3, 4)

    def store_word(self, addr: int, value: int) -> None:
        self.store_bytes(addr & ~3, value, 4)

    def words(self) -> list[int]:
        return [self.load_word(i) for i in range(0, self.size, 4)]


def load_hex_words(path: Path) -> list[int]:
    words: list[int] = []
    if not path.exists():
        return words
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        words.append(int(line, 16) & MASK32)
    return words


class GoldenRV32IM:
    def __init__(self, imem: list[int], dmem: list[int] | None = None, dmem_bytes: int = 64 * 1024):
        self.imem = list(imem)
        self.mem = Memory(dmem or [], size_bytes=dmem_bytes)
        self.x = [0] * 32
        self.pc = 0
        self.commits: list[CommitEvent] = []
        self.halted = False
        self.cycles = 0  # architectural steps (== retired instructions)

    def fetch(self) -> int:
        idx = self.pc >> 2
        if idx < 0 or idx >= len(self.imem):
            return INSTR_INVALID
        return self.imem[idx]

    def set_x(self, rd: int, value: int) -> None:
        if rd != 0:
            self.x[rd] = to_u32(value)

    def retire(
        self,
        instr: int,
        rd_used: bool,
        rd: int,
        value: int,
        *,
        is_store: bool = False,
        store_addr: int = 0,
        store_data: int = 0,
        store_wstrb: int = 0,
    ) -> None:
        if rd_used and rd != 0:
            self.set_x(rd, value)
        else:
            rd_used = False
            rd = 0
            value = 0
        self.commits.append(
            CommitEvent(
                pc=self.pc,
                instr=instr,
                rd_used=rd_used,
                rd=rd,
                value=to_u32(value) if rd_used else 0,
                is_store=is_store,
                store_addr=store_addr,
                store_data=store_data,
                store_wstrb=store_wstrb,
            )
        )
        self.cycles += 1

    def step(self) -> bool:
        """Execute one instruction. Returns False when halted."""
        if self.halted:
            return False
        instr = self.fetch()
        if instr == INSTR_INVALID:
            self.halted = True
            return False

        opc = instr & 0x7F
        rd = (instr >> 7) & 0x1F
        f3 = (instr >> 12) & 0x7
        rs1 = (instr >> 15) & 0x1F
        rs2 = (instr >> 20) & 0x1F
        f7 = (instr >> 25) & 0x7F
        imm_i = sext(instr >> 20, 12)
        imm_s = sext(((instr >> 25) << 5) | ((instr >> 7) & 0x1F), 12)
        imm_b = sext(
            ((instr >> 31) << 12)
            | (((instr >> 7) & 1) << 11)
            | (((instr >> 25) & 0x3F) << 5)
            | (((instr >> 8) & 0xF) << 1),
            13,
        )
        imm_u = instr & 0xFFFFF000
        imm_j = sext(
            ((instr >> 31) << 20)
            | (((instr >> 12) & 0xFF) << 12)
            | (((instr >> 20) & 1) << 11)
            | (((instr >> 21) & 0x3FF) << 1),
            21,
        )

        next_pc = to_u32(self.pc + 4)
        rd_used = False
        value = 0
        is_store = False
        store_addr = store_data = store_wstrb = 0

        def alu_done(v: int, use_rd: bool = True) -> None:
            nonlocal rd_used, value
            rd_used = use_rd and rd != 0
            value = to_u32(v)

        # Match RTL rename_dispatch: UOP_NOP (zero encoding) is not allocated
        # into the ROB and does not generate a commit event.
        if instr == 0:
            self.pc = next_pc
            self.cycles += 1  # architectural step, but not a ROB commit
            return True

        if opc == 0x33:  # OP
            a, b = self.x[rs1], self.x[rs2]
            if f7 == 0x01:  # M
                sa, sb = to_s32(a), to_s32(b)
                if f3 == 0:  # MUL
                    alu_done(sa * sb)
                elif f3 == 1:  # MULH
                    alu_done((sa * sb) >> 32)
                elif f3 == 2:  # MULHSU
                    alu_done((sa * to_u32(b)) >> 32)
                elif f3 == 3:  # MULHU
                    alu_done((to_u32(a) * to_u32(b)) >> 32)
                elif f3 == 4:  # DIV
                    if b == 0:
                        alu_done(-1)
                    elif sa == -0x8000_0000 and sb == -1:
                        alu_done(sa)
                    else:
                        # RISC-V toward-zero division
                        alu_done(int(sa / sb))
                elif f3 == 5:  # DIVU
                    alu_done(MASK32 if b == 0 else to_u32(a) // to_u32(b))
                elif f3 == 6:  # REM
                    if b == 0:
                        alu_done(sa)
                    elif sa == -0x8000_0000 and sb == -1:
                        alu_done(0)
                    else:
                        q = int(sa / sb)
                        alu_done(sa - q * sb)
                elif f3 == 7:  # REMU
                    alu_done(to_u32(a) if b == 0 else to_u32(a) % to_u32(b))
                else:
                    pass  # NOP
            else:
                if f3 == 0 and f7 == 0x00:
                    alu_done(a + b)
                elif f3 == 0 and f7 == 0x20:
                    alu_done(a - b)
                elif f3 == 1 and f7 == 0x00:
                    alu_done(a << (b & 0x1F))
                elif f3 == 2 and f7 == 0x00:
                    alu_done(1 if to_s32(a) < to_s32(b) else 0)
                elif f3 == 3 and f7 == 0x00:
                    alu_done(1 if to_u32(a) < to_u32(b) else 0)
                elif f3 == 4 and f7 == 0x00:
                    alu_done(a ^ b)
                elif f3 == 5 and f7 == 0x00:
                    alu_done(to_u32(a) >> (b & 0x1F))
                elif f3 == 5 and f7 == 0x20:
                    alu_done(to_s32(a) >> (b & 0x1F))
                elif f3 == 6 and f7 == 0x00:
                    alu_done(a | b)
                elif f3 == 7 and f7 == 0x00:
                    alu_done(a & b)
                else:
                    pass
            self.retire(instr, rd_used, rd, value)
            self.pc = next_pc
            return True

        if opc == 0x13:  # OP-IMM
            a = self.x[rs1]
            shamt = rs2
            if f3 == 0:
                alu_done(a + imm_i)
            elif f3 == 2:
                alu_done(1 if to_s32(a) < imm_i else 0)
            elif f3 == 3:
                alu_done(1 if to_u32(a) < to_u32(imm_i) else 0)
            elif f3 == 4:
                alu_done(a ^ imm_i)
            elif f3 == 6:
                alu_done(a | imm_i)
            elif f3 == 7:
                alu_done(a & imm_i)
            elif f3 == 1 and f7 == 0x00:
                alu_done(a << shamt)
            elif f3 == 5 and f7 == 0x00:
                alu_done(to_u32(a) >> shamt)
            elif f3 == 5 and f7 == 0x20:
                alu_done(to_s32(a) >> shamt)
            else:
                pass
            self.retire(instr, rd_used, rd, value)
            self.pc = next_pc
            return True

        if opc == 0x37:  # LUI
            alu_done(imm_u)
            self.retire(instr, rd_used, rd, value)
            self.pc = next_pc
            return True

        if opc == 0x17:  # AUIPC
            alu_done(self.pc + imm_u)
            self.retire(instr, rd_used, rd, value)
            self.pc = next_pc
            return True

        if opc == 0x6F:  # JAL
            alu_done(next_pc)
            self.retire(instr, rd_used, rd, value)
            self.pc = to_u32(self.pc + imm_j)
            return True

        if opc == 0x67:  # JALR
            target = to_u32((self.x[rs1] + imm_i) & ~1)
            alu_done(next_pc)
            self.retire(instr, rd_used, rd, value)
            self.pc = target
            return True

        if opc == 0x63:  # BRANCH
            a, b = self.x[rs1], self.x[rs2]
            take = False
            if f3 == 0:
                take = a == b
            elif f3 == 1:
                take = a != b
            elif f3 == 4:
                take = to_s32(a) < to_s32(b)
            elif f3 == 5:
                take = to_s32(a) >= to_s32(b)
            elif f3 == 6:
                take = to_u32(a) < to_u32(b)
            elif f3 == 7:
                take = to_u32(a) >= to_u32(b)
            self.retire(instr, False, 0, 0)
            self.pc = to_u32(self.pc + imm_b) if take else next_pc
            return True

        if opc == 0x03:  # LOAD
            addr = to_u32(self.x[rs1] + imm_i)
            if f3 == 0:  # LB
                alu_done(sext(self.mem.load_bytes(addr, 1), 8))
            elif f3 == 1:  # LH
                alu_done(sext(self.mem.load_bytes(addr, 2), 16))
            elif f3 == 2:  # LW
                alu_done(self.mem.load_bytes(addr, 4))
            elif f3 == 4:  # LBU
                alu_done(self.mem.load_bytes(addr, 1))
            elif f3 == 5:  # LHU
                alu_done(self.mem.load_bytes(addr, 2))
            else:
                pass
            self.retire(instr, rd_used, rd, value)
            self.pc = next_pc
            return True

        if opc == 0x23:  # STORE
            addr = to_u32(self.x[rs1] + imm_s)
            data = self.x[rs2]
            is_store = True
            store_addr = addr
            if f3 == 0:
                self.mem.store_bytes(addr, data, 1)
                store_data = data & 0xFF
                store_wstrb = 1 << (addr & 3)
            elif f3 == 1:
                self.mem.store_bytes(addr, data, 2)
                store_data = data & 0xFFFF
                store_wstrb = 3 << (addr & 3)
            elif f3 == 2:
                self.mem.store_bytes(addr, data, 4)
                store_data = data
                store_wstrb = 0xF
            self.retire(
                instr,
                False,
                0,
                0,
                is_store=is_store,
                store_addr=store_addr,
                store_data=store_data,
                store_wstrb=store_wstrb,
            )
            self.pc = next_pc
            return True

        # Illegal / SYSTEM / FENCE -> architectural NOP (still retires).
        self.retire(instr, False, 0, 0)
        self.pc = next_pc
        return True

    def run(self, max_steps: int = 10_000_000) -> None:
        for _ in range(max_steps):
            if not self.step():
                return
        raise RuntimeError(f"golden timeout after {max_steps} steps pc={self.pc:#x}")

    def summary(self) -> dict:
        return {
            "halted": self.halted,
            "commits": len(self.commits),
            "pc": self.pc,
            "x": self.x[:],
            "dmem_words": [self.mem.load_word(i) for i in range(0, min(self.mem.size, 4096), 4)],
            "commit_events": [asdict(c) for c in self.commits],
        }


def compare_commit_stream(
    golden: list[CommitEvent],
    rtl: list[dict],
) -> list[str]:
    errors: list[str] = []
    if len(golden) != len(rtl):
        errors.append(f"commit count mismatch golden={len(golden)} rtl={len(rtl)}")
    n = min(len(golden), len(rtl))
    for i in range(n):
        g = golden[i]
        r = rtl[i]
        g_used = bool(g.rd_used)
        r_used = bool(int(r.get("rd_used", 0)))
        if g_used != r_used:
            errors.append(
                f"[{i}] rd_used mismatch golden={int(g_used)} rtl={int(r_used)} "
                f"pc={g.pc:#x} instr={g.instr:08x}"
            )
            if len(errors) >= 20:
                break
            continue
        if g_used:
            grd = int(g.rd)
            gval = to_u32(g.value)
            rrd = int(r.get("rd", -1))
            rval = to_u32(int(r.get("value", 0), 0) if isinstance(r.get("value"), str) else int(r.get("value", 0)))
            if grd != rrd or gval != rval:
                errors.append(
                    f"[{i}] write mismatch golden=x{grd}={gval:08x} rtl=x{rrd}={rval:08x} "
                    f"pc={g.pc:#x} instr={g.instr:08x}"
                )
                if len(errors) >= 20:
                    break
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="RV32IM architectural golden model")
    parser.add_argument("--imem", type=Path, required=True)
    parser.add_argument("--dmem", type=Path, default=None)
    parser.add_argument("--max-steps", type=int, default=10_000_000)
    parser.add_argument("--dump-json", type=Path, default=None)
    parser.add_argument("--compare-commits", type=Path, default=None, help="JSON list of RTL commits")
    args = parser.parse_args()

    imem = load_hex_words(args.imem)
    dmem = load_hex_words(args.dmem) if args.dmem else []
    model = GoldenRV32IM(imem, dmem)
    model.run(args.max_steps)
    summary = model.summary()
    print(f"GOLDEN halted={model.halted} commits={len(model.commits)} pc={model.pc:#x}")

    if args.dump_json:
        args.dump_json.write_text(json.dumps(summary, indent=2))
        print(f"Wrote {args.dump_json}")

    if args.compare_commits:
        rtl = json.loads(args.compare_commits.read_text())
        errs = compare_commit_stream(model.commits, rtl)
        if errs:
            print("GOLDEN_COMPARE: FAIL")
            for e in errs:
                print(" ", e)
            return 1
        print("GOLDEN_COMPARE: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
