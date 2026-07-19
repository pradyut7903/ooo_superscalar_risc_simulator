#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// RTL-aligned micro-op vocabulary (mirrors github_ooo_rv32im/rtl/pkg_cpu.sv).
// Stage 1: types only — the cycle pipeline still uses string opcodes.
// ---------------------------------------------------------------------------

// Functional-unit class (pkg_cpu::fu_e)
enum class Fu {
    ALU = 0,   // integer ALU (incl. LUI/AUIPC, immediate forms)
    MUL = 1,
    DIV = 2,
    MEM = 3,   // loads / stores -> LSQ
    BR  = 4    // branches + JAL/JALR
};

// Decoded operation (pkg_cpu::op_e)
enum class Op {
    // integer ALU
    ALU_ADD, ALU_SUB, ALU_SLL, ALU_SLT, ALU_SLTU,
    ALU_XOR, ALU_SRL, ALU_SRA, ALU_OR,  ALU_AND,
    ALU_LUI, ALU_AUIPC,
    // M extension
    MD_MUL, MD_MULH, MD_MULHSU, MD_MULHU,
    MD_DIV, MD_DIVU, MD_REM, MD_REMU,
    // branches / jumps
    BR_EQ, BR_NE, BR_LT, BR_GE, BR_LTU, BR_GEU,
    BR_JAL, BR_JALR,
    // memory
    MEM_LB, MEM_LH, MEM_LW, MEM_LBU, MEM_LHU,
    MEM_SB, MEM_SH, MEM_SW,
    // bubble / illegal -> no architectural effect
    UOP_NOP
};

// Memory access size (pkg_cpu::memsz_e)
enum class MemSize {
    B = 0,
    H = 1,
    W = 2
};

// Memory-system mode (pkg_cpu::MEM_SYSTEM_*). Unused by the cycle model until
// later stages; default Ideal matches today's flat MainMemory path.
enum class MemSystem {
    Ideal  = 0,
    Cached = 1
};

// Decoded micro-op (pkg_cpu::uop_t). Not yet flowing through the pipeline.
struct Uop {
    uint64_t pc = 0;
    Op op = Op::UOP_NOP;
    Fu fu = Fu::ALU;

    bool rs1_used = false;
    int rs1 = 0;
    bool rs2_used = false;
    int rs2 = 0;

    bool rd_used = false;   // writes a register (false for stores/branches/NOP or rd==x0)
    int rd = 0;

    int32_t imm = 0;        // sign-extended immediate
    bool src2_is_imm = false;

    bool is_load = false;
    bool is_store = false;
    MemSize mem_size = MemSize::W;
    bool mem_unsigned = false;  // LBU / LHU

    bool is_branch = false;     // conditional branch
    bool is_jump = false;       // JAL / JALR

    bool pred_taken = false;
    uint64_t pred_target = 0;
};

// Resolved source operand (pkg_cpu::operand_t). Unused until rename rework.
struct Operand {
    bool ready = false;
    int tag = -1;
    int value = 0;
};

// Map today's toy string opcodes onto Op. CALL/RET have no RV32IM equivalent
// yet and map to UOP_NOP. The cycle pipeline still uses string opcodes; the
// Op path is used by decode / executeOp (Stage 2+) and later rename.
inline Op opcodeToOp(const std::string& opcode) {
    if (opcode == "ADD")  return Op::ALU_ADD;
    if (opcode == "SUB")  return Op::ALU_SUB;
    if (opcode == "MUL")  return Op::MD_MUL;
    if (opcode == "DIV")  return Op::MD_DIV;
    if (opcode == "ADDI") return Op::ALU_ADD;   // with src2_is_imm in a real Uop
    if (opcode == "LOAD") return Op::MEM_LW;
    if (opcode == "STORE") return Op::MEM_SW;
    if (opcode == "BEQ")  return Op::BR_EQ;
    if (opcode == "BNE")  return Op::BR_NE;
    return Op::UOP_NOP;
}

inline Fu fuForOp(Op op) {
    switch (op) {
        case Op::MD_MUL: case Op::MD_MULH: case Op::MD_MULHSU: case Op::MD_MULHU:
            return Fu::MUL;
        case Op::MD_DIV: case Op::MD_DIVU: case Op::MD_REM: case Op::MD_REMU:
            return Fu::DIV;
        case Op::MEM_LB: case Op::MEM_LH: case Op::MEM_LW:
        case Op::MEM_LBU: case Op::MEM_LHU:
        case Op::MEM_SB: case Op::MEM_SH: case Op::MEM_SW:
            return Fu::MEM;
        case Op::BR_EQ: case Op::BR_NE: case Op::BR_LT: case Op::BR_GE:
        case Op::BR_LTU: case Op::BR_GEU: case Op::BR_JAL: case Op::BR_JALR:
            return Fu::BR;
        case Op::UOP_NOP:
            return Fu::ALU;
        default:
            return Fu::ALU;
    }
}

// Common Data Bus broadcast payload (existing cycle-path type)
struct CDBResult {
    int rob_id;
    int value;
};

// Raw instruction pulled from the Instruction Cache (toy traces) or built from
// a decoded RV32IM word (hex programs). has_uop distinguishes the two paths.
struct FetchedInstruction {
    uint64_t pc = 0;
    std::string opcode;
    int dest_reg = 0;
    int src1_reg = 0;
    int src2_reg = 0;
    int imm = 0;               // Immediate / absolute address operand (toy BEQ target)

    bool predicted_taken = false;
    uint64_t predicted_target = 0;

    // Toy path: RAS state at fetch *before* this instr's push/pop.
    int ras_tos = 0;
    int ras_count = 0;
    std::vector<uint64_t> ras_stack;
    bool ras_had_return = false;     // RET: peek/pop succeeded at fetch
    uint64_t ras_ret_target = 0;     // architectural return PC for RET

    // Stage 3+: filled when fetching from imem.hex
    bool has_uop = false;
    uint32_t raw_instr = 0;
    Uop uop{};
};
