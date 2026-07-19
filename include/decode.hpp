#pragma once

#include "types.hpp"

#include <cstdint>

// Combinational RV32IM decoder (mirrors github_ooo_rv32im/rtl/decode.sv).
// Takes a 32-bit instruction word + PC and produces a Uop. FENCE / SYSTEM /
// illegal encodings become UOP_NOP. Writes to x0 clear rd_used.
Uop decodeInstruction(uint32_t inst, uint64_t pc);
