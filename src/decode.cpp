#include "decode.hpp"

namespace {

// RV32IM encoding constants (pkg_cpu.sv)
constexpr uint32_t OPC_LUI     = 0x37;
constexpr uint32_t OPC_AUIPC   = 0x17;
constexpr uint32_t OPC_JAL     = 0x6F;
constexpr uint32_t OPC_JALR    = 0x67;
constexpr uint32_t OPC_BRANCH  = 0x63;
constexpr uint32_t OPC_LOAD    = 0x03;
constexpr uint32_t OPC_STORE   = 0x23;
constexpr uint32_t OPC_OPIMM   = 0x13;
constexpr uint32_t OPC_OP      = 0x33;

constexpr uint32_t F7_BASE   = 0x00;
constexpr uint32_t F7_ALT    = 0x20;
constexpr uint32_t F7_MULDIV = 0x01;

uint32_t bits(uint32_t inst, int hi, int lo) {
    return (inst >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

int32_t immI(uint32_t inst) {
    return static_cast<int32_t>(inst) >> 20;  // sign-extend [31:20]
}

int32_t immS(uint32_t inst) {
    uint32_t raw = (bits(inst, 31, 25) << 5) | bits(inst, 11, 7);
    return static_cast<int32_t>(raw << 20) >> 20;
}

int32_t immB(uint32_t inst) {
    uint32_t raw = (bits(inst, 31, 31) << 12) | (bits(inst, 7, 7) << 11) |
                   (bits(inst, 30, 25) << 5)  | (bits(inst, 11, 8) << 1);
    return static_cast<int32_t>(raw << 19) >> 19;
}

int32_t immU(uint32_t inst) {
    return static_cast<int32_t>(inst & 0xFFFFF000u);
}

int32_t immJ(uint32_t inst) {
    uint32_t raw = (bits(inst, 31, 31) << 20) | (bits(inst, 19, 12) << 12) |
                   (bits(inst, 20, 20) << 11) | (bits(inst, 30, 21) << 1);
    return static_cast<int32_t>(raw << 11) >> 11;
}

void makeNop(Uop& uop) {
    uop.op = Op::UOP_NOP;
    uop.fu = Fu::ALU;
    uop.rd_used = false;
    uop.rs1_used = false;
    uop.rs2_used = false;
    uop.src2_is_imm = false;
    uop.is_load = false;
    uop.is_store = false;
    uop.is_branch = false;
    uop.is_jump = false;
}

}  // namespace

Uop decodeInstruction(uint32_t inst, uint64_t pc) {
    Uop uop;
    uop.pc = pc;
    uop.op = Op::UOP_NOP;
    uop.fu = Fu::ALU;
    uop.rd = static_cast<int>(bits(inst, 11, 7));
    uop.rs1 = static_cast<int>(bits(inst, 19, 15));
    uop.rs2 = static_cast<int>(bits(inst, 24, 20));
    uop.mem_size = MemSize::W;

    const uint32_t opcode = bits(inst, 6, 0);
    const uint32_t funct3 = bits(inst, 14, 12);
    const uint32_t funct7 = bits(inst, 31, 25);

    switch (opcode) {
        case OPC_OPIMM: {
            uop.fu = Fu::ALU;
            uop.rd_used = true;
            uop.rs1_used = true;
            uop.src2_is_imm = true;
            uop.imm = immI(inst);
            switch (funct3) {
                case 0x0: uop.op = Op::ALU_ADD; break;
                case 0x1:
                    if (funct7 == F7_BASE) uop.op = Op::ALU_SLL;
                    else makeNop(uop);
                    break;
                case 0x2: uop.op = Op::ALU_SLT; break;
                case 0x3: uop.op = Op::ALU_SLTU; break;
                case 0x4: uop.op = Op::ALU_XOR; break;
                case 0x5:
                    if (funct7 == F7_BASE) uop.op = Op::ALU_SRL;
                    else if (funct7 == F7_ALT) uop.op = Op::ALU_SRA;
                    else makeNop(uop);
                    break;
                case 0x6: uop.op = Op::ALU_OR; break;
                case 0x7: uop.op = Op::ALU_AND; break;
                default: makeNop(uop); break;
            }
            break;
        }

        case OPC_OP: {
            uop.rd_used = true;
            uop.rs1_used = true;
            uop.rs2_used = true;
            if (funct7 == F7_MULDIV) {
                switch (funct3) {
                    case 0x0: uop.op = Op::MD_MUL;    uop.fu = Fu::MUL; break;
                    case 0x1: uop.op = Op::MD_MULH;   uop.fu = Fu::MUL; break;
                    case 0x2: uop.op = Op::MD_MULHSU; uop.fu = Fu::MUL; break;
                    case 0x3: uop.op = Op::MD_MULHU;  uop.fu = Fu::MUL; break;
                    case 0x4: uop.op = Op::MD_DIV;    uop.fu = Fu::DIV; break;
                    case 0x5: uop.op = Op::MD_DIVU;   uop.fu = Fu::DIV; break;
                    case 0x6: uop.op = Op::MD_REM;    uop.fu = Fu::DIV; break;
                    case 0x7: uop.op = Op::MD_REMU;   uop.fu = Fu::DIV; break;
                    default: makeNop(uop); break;
                }
            } else {
                uop.fu = Fu::ALU;
                switch (funct3) {
                    case 0x0:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_ADD;
                        else if (funct7 == F7_ALT) uop.op = Op::ALU_SUB;
                        else makeNop(uop);
                        break;
                    case 0x1:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_SLL;
                        else makeNop(uop);
                        break;
                    case 0x2:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_SLT;
                        else makeNop(uop);
                        break;
                    case 0x3:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_SLTU;
                        else makeNop(uop);
                        break;
                    case 0x4:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_XOR;
                        else makeNop(uop);
                        break;
                    case 0x5:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_SRL;
                        else if (funct7 == F7_ALT) uop.op = Op::ALU_SRA;
                        else makeNop(uop);
                        break;
                    case 0x6:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_OR;
                        else makeNop(uop);
                        break;
                    case 0x7:
                        if (funct7 == F7_BASE) uop.op = Op::ALU_AND;
                        else makeNop(uop);
                        break;
                    default: makeNop(uop); break;
                }
            }
            break;
        }

        case OPC_LOAD: {
            uop.fu = Fu::MEM;
            uop.is_load = true;
            uop.rd_used = true;
            uop.rs1_used = true;
            uop.imm = immI(inst);
            switch (funct3) {
                case 0x0: uop.op = Op::MEM_LB;  uop.mem_size = MemSize::B; uop.mem_unsigned = false; break;
                case 0x1: uop.op = Op::MEM_LH;  uop.mem_size = MemSize::H; uop.mem_unsigned = false; break;
                case 0x2: uop.op = Op::MEM_LW;  uop.mem_size = MemSize::W; uop.mem_unsigned = false; break;
                case 0x4: uop.op = Op::MEM_LBU; uop.mem_size = MemSize::B; uop.mem_unsigned = true;  break;
                case 0x5: uop.op = Op::MEM_LHU; uop.mem_size = MemSize::H; uop.mem_unsigned = true;  break;
                default: makeNop(uop); break;
            }
            break;
        }

        case OPC_STORE: {
            uop.fu = Fu::MEM;
            uop.is_store = true;
            uop.rs1_used = true;
            uop.rs2_used = true;
            uop.imm = immS(inst);
            switch (funct3) {
                case 0x0: uop.op = Op::MEM_SB; uop.mem_size = MemSize::B; break;
                case 0x1: uop.op = Op::MEM_SH; uop.mem_size = MemSize::H; break;
                case 0x2: uop.op = Op::MEM_SW; uop.mem_size = MemSize::W; break;
                default: makeNop(uop); break;
            }
            break;
        }

        case OPC_BRANCH: {
            uop.fu = Fu::BR;
            uop.is_branch = true;
            uop.rs1_used = true;
            uop.rs2_used = true;
            uop.imm = immB(inst);
            switch (funct3) {
                case 0x0: uop.op = Op::BR_EQ;  break;
                case 0x1: uop.op = Op::BR_NE;  break;
                case 0x4: uop.op = Op::BR_LT;  break;
                case 0x5: uop.op = Op::BR_GE;  break;
                case 0x6: uop.op = Op::BR_LTU; break;
                case 0x7: uop.op = Op::BR_GEU; break;
                default: makeNop(uop); break;
            }
            break;
        }

        case OPC_JAL: {
            uop.fu = Fu::BR;
            uop.is_jump = true;
            uop.op = Op::BR_JAL;
            uop.rd_used = true;
            uop.imm = immJ(inst);
            break;
        }

        case OPC_JALR: {
            if (funct3 == 0x0) {
                uop.fu = Fu::BR;
                uop.is_jump = true;
                uop.op = Op::BR_JALR;
                uop.rd_used = true;
                uop.rs1_used = true;
                uop.imm = immI(inst);
            }
            // else illegal funct3 -> leave as UOP_NOP
            break;
        }

        case OPC_LUI: {
            uop.fu = Fu::ALU;
            uop.op = Op::ALU_LUI;
            uop.rd_used = true;
            uop.src2_is_imm = true;
            uop.imm = immU(inst);
            break;
        }

        case OPC_AUIPC: {
            uop.fu = Fu::ALU;
            uop.op = Op::ALU_AUIPC;
            uop.rd_used = true;
            uop.src2_is_imm = true;
            uop.imm = immU(inst);
            break;
        }

        // FENCE / SYSTEM / illegal -> NOP (defaults already set)
        default:
            break;
    }

    // x0 is hardwired 0: never rename a write to it.
    if (uop.rd == 0) {
        uop.rd_used = false;
    }

    return uop;
}
