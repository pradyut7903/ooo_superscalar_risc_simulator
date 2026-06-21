#pragma once

#include <cstdint>
#include <string>

// Common Data Bus broadcast payload
struct CDBResult {
    int rob_id;
    int value;
};

// Raw instruction pulled from the Instruction Cache
struct FetchedInstruction {
    uint64_t pc;
    std::string opcode;
    int dest_reg;
    int src1_reg;
    int src2_reg;
    int imm;               // Immediate / absolute address operand

    bool predicted_taken;
    uint64_t predicted_target;
};
