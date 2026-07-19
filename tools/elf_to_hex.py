#!/usr/bin/env python3
"""Convert a bare-metal RV32 ELF into separate imem/dmem $readmemh images.

Harvard convention used by ooo_rtl:
  * SHF_EXECINSTR sections -> imem.hex (word-addressed from VMA)
  * other SHF_ALLOC sections -> dmem.hex (word-addressed from VMA)
Overlapping IM/DM origins (both at 0) are supported and expected.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_NOBITS = 8
PT_LOAD = 1


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def load_elf32(path: Path) -> tuple[dict[int, int], dict[int, int]]:
    raw = path.read_bytes()
    if raw[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not ELF")
    if raw[4] != 1:
        raise ValueError(f"{path}: only ELF32 supported")
    if raw[5] != 1:
        raise ValueError(f"{path}: only little-endian supported")

    e_shoff = u32(raw, 32)
    e_shentsize = u16(raw, 46)
    e_shnum = u16(raw, 48)
    e_shstrndx = u16(raw, 50)

    def shdr(i: int) -> dict[str, int]:
        off = e_shoff + i * e_shentsize
        return {
            "name_off": u32(raw, off + 0),
            "type": u32(raw, off + 4),
            "flags": u32(raw, off + 8),
            "addr": u32(raw, off + 12),
            "offset": u32(raw, off + 16),
            "size": u32(raw, off + 20),
        }

    strtab = b""
    if e_shstrndx != 0:
        shstr = shdr(e_shstrndx)
        strtab = raw[shstr["offset"] : shstr["offset"] + shstr["size"]]

    def name_of(sh: dict[str, int]) -> str:
        n = sh["name_off"]
        end = strtab.find(b"\0", n)
        return strtab[n:end].decode("ascii", errors="replace") if end >= 0 else ""

    imem: dict[int, int] = {}
    dmem: dict[int, int] = {}

    for i in range(e_shnum):
        sh = shdr(i)
        if not (sh["flags"] & SHF_ALLOC):
            continue
        if sh["size"] == 0:
            continue
        nm = name_of(sh)
        is_text = bool(sh["flags"] & SHF_EXECINSTR) or nm.startswith(".text")
        target = imem if is_text else dmem
        if sh["type"] == SHT_NOBITS:
            # BSS: leave zeros (default fill). Still reserve extent via max addr.
            for a in range(sh["addr"], sh["addr"] + sh["size"], 4):
                target.setdefault(a // 4, 0)
            continue
        payload = raw[sh["offset"] : sh["offset"] + sh["size"]]
        for bi, byte in enumerate(payload):
            addr = sh["addr"] + bi
            word_i = addr // 4
            shift = (addr & 3) * 8
            cur = target.get(word_i, 0)
            cur = (cur & ~(0xFF << shift)) | ((byte & 0xFF) << shift)
            target[word_i] = cur & 0xFFFF_FFFF

    return imem, dmem


def write_hex(path: Path, words: dict[int, int], default: int, pad_to: int | None) -> int:
    max_i = max(words.keys(), default=-1)
    n = max_i + 1
    if pad_to is not None:
        n = max(n, pad_to)
    # Always emit at least one word so $readmemh has a well-formed file.
    if n <= 0:
        n = 1
    lines = [f"{words.get(i, default):08x}\n" for i in range(n)]
    path.write_text("".join(lines))
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--name", type=str, default=None, help="stem for hex files")
    ap.add_argument("--imem-pad", type=int, default=None, help="min imem words")
    ap.add_argument("--dmem-pad", type=int, default=None, help="min dmem words")
    args = ap.parse_args()

    stem = args.name or args.elf.stem
    args.out_dir.mkdir(parents=True, exist_ok=True)
    imem, dmem = load_elf32(args.elf)

    imem_path = args.out_dir / f"{stem}.imem.hex"
    dmem_path = args.out_dir / f"{stem}.dmem.hex"
    ni = write_hex(imem_path, imem, 0xFFFF_FFFF, args.imem_pad)
    nd = write_hex(dmem_path, dmem, 0x0000_0000, args.dmem_pad)

    print(f"elf_to_hex: {args.elf.name} -> {imem_path.name} ({ni} words), {dmem_path.name} ({nd} words)")
    print(f"  text_words_used={max(imem.keys(), default=-1)+1} data_words_used={max(dmem.keys(), default=-1)+1}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
