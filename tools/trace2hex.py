#!/usr/bin/env python3
"""trace2hex.py -- convert a pipeline_simulator trace into $readmemh images.

Emits:
  <prefix>.imem.hex  one 64-bit instruction word per slot, index = (pc-0x1000)/4,
                     gaps/tail padded with the all-ones "no instruction" sentinel.
  <prefix>.dmem.hex  (only if the trace has `.mem ADDR VALUE` directives) data-memory
                     init in $readmemh @address format -- used by the LSQ/dmem at M5.

The 64-bit encoding matches pkg_cpu::instr_word_t exactly:
  [63:32] imm   [31:16] rsvd(0)   [15:12] op   [11:8] rd   [7:4] rs1   [3:0] rs2

Register fields are the raw trace fields (rd = trace DEST); the RTL `decode` module
applies the STORE rd->src2 remap, so this converter stays a dumb, faithful packer.
"""
import argparse
import os
import sys

# Must match pkg_cpu::opcode_e
OPCODES = {
    "ADD": 0, "SUB": 1, "MUL": 2, "DIV": 3, "ADDI": 4, "LOAD": 5, "STORE": 6,
    "BEQ": 7, "BNE": 8, "CALL": 9, "RET": 10, "NOP": 11,
}
BASE_PC = 0x1000
SENTINEL = (1 << 64) - 1            # all-ones == pkg_cpu::INSTR_INVALID


def parse_int(tok):
    """Signed int: hex (0x..), negative, or decimal -- mirrors the C++ parser."""
    tok = tok.strip()
    neg = False
    if tok.startswith("-"):
        neg = True
        tok = tok[1:]
    elif tok.startswith("+"):
        tok = tok[1:]
    val = int(tok, 16) if tok.lower().startswith("0x") else int(tok, 10)
    return -val if neg else val


def parse_uint(tok):
    tok = tok.strip()
    return int(tok, 16) if tok.lower().startswith("0x") else int(tok, 10)


def encode(op, rd, rs1, rs2, imm):
    imm32 = imm & 0xFFFFFFFF
    low16 = ((op & 0xF) << 12) | ((rd & 0xF) << 8) | ((rs1 & 0xF) << 4) | (rs2 & 0xF)
    return (imm32 << 32) | (low16 & 0xFFFF)


def main():
    ap = argparse.ArgumentParser(description="trace -> $readmemh image converter")
    ap.add_argument("trace")
    ap.add_argument("-o", "--prefix",
                    help="output prefix (default: trace path without extension)")
    ap.add_argument("--imem", help="explicit imem hex path")
    ap.add_argument("--dmem", help="explicit dmem hex path")
    args = ap.parse_args()

    prefix = args.prefix or os.path.splitext(args.trace)[0]
    imem_path = args.imem or (prefix + ".imem.hex")
    dmem_path = args.dmem or (prefix + ".dmem.hex")

    instrs = {}          # index -> 64-bit word
    mem_init = []        # list of (addr, value)
    max_idx = -1

    with open(args.trace) as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            toks = line.split()

            if toks[0] == ".mem":
                if len(toks) >= 3:
                    mem_init.append((parse_int(toks[1]), parse_int(toks[2])))
                continue

            # PC OPCODE DEST SRC1 SRC2 [IMM]
            if len(toks) < 5:
                print(f"warning: {args.trace}:{lineno}: skipping malformed line: {line}",
                      file=sys.stderr)
                continue

            pc = parse_uint(toks[0])
            op_s = toks[1].upper()
            if op_s not in OPCODES:
                print(f"warning: {args.trace}:{lineno}: unknown opcode {op_s}; emitting NOP",
                      file=sys.stderr)
                op, rd, rs1, rs2, imm = OPCODES["NOP"], 0, 0, 0, 0
            else:
                op = OPCODES[op_s]
                rd = parse_int(toks[2])
                rs1 = parse_int(toks[3])
                rs2 = parse_int(toks[4])
                imm = parse_int(toks[5]) if len(toks) >= 6 else 0

            if pc < BASE_PC or (pc - BASE_PC) % 4 != 0:
                sys.exit(f"error: {args.trace}:{lineno}: pc {pc:#x} not aligned "
                         f"to base {BASE_PC:#x} / stride 4")
            idx = (pc - BASE_PC) // 4
            if idx in instrs:
                sys.exit(f"error: {args.trace}:{lineno}: duplicate pc {pc:#x}")
            instrs[idx] = encode(op, rd, rs1, rs2, imm)
            max_idx = max(max_idx, idx)

    # ---- imem image: dense, sentinel-padded, one word per line ----
    with open(imem_path, "w") as f:
        for i in range(max_idx + 1):
            f.write(f"{instrs.get(i, SENTINEL):016X}\n")
    n_slots = max_idx + 1 if max_idx >= 0 else 0
    print(f"[trace2hex] {args.trace}: {len(instrs)} instrs over {n_slots} slots "
          f"-> {imem_path}")

    # ---- dmem image: sparse @address format (for M5) ----
    if mem_init:
        with open(dmem_path, "w") as f:
            for addr, val in mem_init:
                f.write(f"@{addr & 0xFFFFFFFF:08X}\n{val & 0xFFFFFFFF:08X}\n")
        print(f"[trace2hex] {len(mem_init)} .mem inits -> {dmem_path}")


if __name__ == "__main__":
    main()
