#include "golden_model.hpp"

#include <cctype>
#include <iostream>

namespace {

constexpr uint64_t INSTR_SIZE = 4;

bool ieq(const std::string& a, const char* b) {
    std::string bs(b);
    if (a.size() != bs.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(bs[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

GoldenModel::GoldenModel(int n)
    : num_regs(n), arf(n, 0), mem(MEMORY_WORDS, 0) {}

void GoldenModel::load(const ParsedProgram& prog) {
    icache = prog.icache;
    for (const auto& kv : prog.mem_init) {
        memWrite(kv.first, kv.second);
    }
}

void GoldenModel::loadTrace(const std::string& path) {
    load(loadTraceFile(path));
}

int GoldenModel::readReg(int r) const {
    // R0 is hardwired to 0; out-of-range ids read as 0 (mirrors the OoO core).
    if (r <= 0 || r >= num_regs) return 0;
    return arf[r];
}

void GoldenModel::writeReg(int r, int value) {
    if (r > 0 && r < num_regs) arf[r] = value;
}

int GoldenModel::memRead(int addr) const {
    if (addr < 0 || static_cast<size_t>(addr) >= mem.size()) return 0;
    return mem[addr];
}

void GoldenModel::memWrite(int addr, int value) {
    if (addr < 0 || static_cast<size_t>(addr) >= mem.size()) return;
    mem[addr] = value;
}

uint64_t GoldenModel::run(uint64_t initial_pc, uint64_t max_steps) {
    uint64_t pc = initial_pc;
    uint64_t steps = 0;
    hit_limit = false;

    while (true) {
        auto it = icache.find(pc);
        if (it == icache.end()) break;          // ran off the end of the program
        if (steps >= max_steps) { hit_limit = true; break; }
        const FetchedInstruction& in = it->second;
        const std::string& op = in.opcode;

        int s1 = readReg(in.src1_reg);
        int s2 = readReg(in.src2_reg);
        uint64_t next = pc + INSTR_SIZE;

        if (ieq(op, "ADD")) {
            writeReg(in.dest_reg, s1 + s2);
        } else if (ieq(op, "SUB")) {
            writeReg(in.dest_reg, s1 - s2);
        } else if (ieq(op, "MUL")) {
            writeReg(in.dest_reg, s1 * s2);
        } else if (ieq(op, "DIV")) {
            writeReg(in.dest_reg, (s2 != 0) ? (s1 / s2) : 0);
        } else if (ieq(op, "ADDI")) {
            writeReg(in.dest_reg, s1 + in.imm);
        } else if (ieq(op, "LOAD")) {
            writeReg(in.dest_reg, memRead(s1 + in.imm));
        } else if (ieq(op, "STORE")) {
            // STORE's data comes from the DEST field (the register that holds the value).
            memWrite(s1 + in.imm, readReg(in.dest_reg));
        } else if (ieq(op, "BEQ")) {
            if (s1 == s2) next = static_cast<uint64_t>(in.imm);
        } else if (ieq(op, "BNE")) {
            if (s1 != s2) next = static_cast<uint64_t>(in.imm);
        } else if (ieq(op, "CALL")) {
            ras.push_back(pc + INSTR_SIZE);
            next = (in.imm != 0) ? static_cast<uint64_t>(in.imm)
                                 : static_cast<uint64_t>(in.src1_reg);
        } else if (ieq(op, "RET")) {
            if (!ras.empty()) { next = ras.back(); ras.pop_back(); }
        }
        // unknown opcode: treated as a no-op (PC advances)

        pc = next;
        ++steps;
    }
    return steps;
}

void GoldenModel::printArchState() const {
    std::cout << "\n--------- Final Architectural State ---------\n";
    std::cout << "Registers (nonzero):\n";
    bool any = false;
    for (int r = 1; r < num_regs; ++r) {
        if (arf[r] != 0) { std::cout << "  R" << r << " = " << arf[r] << "\n"; any = true; }
    }
    if (!any) std::cout << "  (all zero)\n";

    std::cout << "Memory (nonzero words):\n";
    any = false;
    for (size_t a = 0; a < mem.size(); ++a) {
        if (mem[a] != 0) { std::cout << "  MEM[" << a << "] = " << mem[a] << "\n"; any = true; }
    }
    if (!any) std::cout << "  (all zero)\n";
    std::cout << "--------------------------------------------\n";
}
