#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Flat-memory size shared by the OoO simulator and the golden model so both index
// the same address space and apply identical bounds behavior.
constexpr int MEMORY_WORDS = 65536;

// A program parsed from a trace file: the instruction cache (PC -> instruction) plus
// the data-segment initializers from `.mem ADDR VALUE` directives. Factored out so the
// OoO `Processor` and the standalone golden model share ONE source of truth for the
// trace format (the verification is only meaningful if both read the input identically).
struct ParsedProgram {
    std::unordered_map<uint64_t, FetchedInstruction> icache;
    std::vector<std::pair<int, int>> mem_init;   // (address, value)
};

// Parse a trace file. Throws std::runtime_error if the file cannot be opened.
ParsedProgram loadTraceFile(const std::string& path);
