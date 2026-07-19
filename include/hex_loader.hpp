#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Load a Verilog-style hex file: one 32-bit word per non-empty line (hex digits).
// Matches github_ooo_rv32im/tools/golden_rv32im.py::load_hex_words.
std::vector<uint32_t> loadHexWords(const std::string& path);
