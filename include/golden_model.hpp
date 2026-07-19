#pragma once

#include "trace_loader.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Golden reference model: a single-cycle, in-order functional emulator of the ISA.
//
// It executes one instruction at a time, instantly updating the architectural register
// file and main memory, following real (non-speculative) control flow. It has NO
// pipeline, no renaming, no speculation -- so it is simple enough to trust by
// inspection, and it defines the architecturally-correct end state that the complex
// out-of-order simulator must reproduce. This is the standard "golden model / ISS
// co-simulation" approach used in industry CPU verification.
class GoldenModel {
public:
    explicit GoldenModel(int num_regs = 16);

    void load(const ParsedProgram& prog);
    void loadTrace(const std::string& path);

    // Execute to completion (until PC leaves the program, or max_steps as a runaway
    // guard). Returns the number of instructions retired.
    uint64_t run(uint64_t initial_pc = 0x1000, uint64_t max_steps = 200000000ULL);

    bool hitStepLimit() const { return hit_limit; }

    const std::vector<int>& registers() const { return arf; }
    const std::vector<int>& memory() const { return mem; }

    // Same textual format as Processor::printArchState so the two can be diffed.
    void printArchState() const;

private:
    int num_regs;
    std::vector<int> arf;
    std::vector<int> mem;
    std::unordered_map<uint64_t, FetchedInstruction> icache;

    // Architectural return-address stack for CALL/RET.
    std::vector<uint64_t> ras;

    bool hit_limit = false;

    int readReg(int r) const;
    void writeReg(int r, int value);
    int memRead(int addr) const;
    void memWrite(int addr, int value);
};
