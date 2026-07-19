#include "execution_engine.hpp"

#include <cassert>
#include <cstdint>

int32_t executeOp(Op op, int32_t src1, int32_t src2, int32_t imm, uint32_t pc) {
    const uint32_t u1 = static_cast<uint32_t>(src1);
    const uint32_t u2 = static_cast<uint32_t>(src2);
    const uint32_t shamt = u2 & 0x1Fu;

    switch (op) {
        case Op::ALU_ADD:   return src1 + src2;
        case Op::ALU_SUB:   return src1 - src2;
        case Op::ALU_SLL:   return static_cast<int32_t>(u1 << shamt);
        case Op::ALU_SLT:   return (src1 < src2) ? 1 : 0;
        case Op::ALU_SLTU:  return (u1 < u2) ? 1 : 0;
        case Op::ALU_XOR:   return src1 ^ src2;
        case Op::ALU_SRL:   return static_cast<int32_t>(u1 >> shamt);
        case Op::ALU_SRA:   return src1 >> static_cast<int32_t>(shamt);
        case Op::ALU_OR:    return src1 | src2;
        case Op::ALU_AND:   return src1 & src2;
        case Op::ALU_LUI:   return imm;
        case Op::ALU_AUIPC: return static_cast<int32_t>(pc) + imm;

        case Op::MD_MUL: {
            const uint64_t prod = static_cast<uint64_t>(u1) * static_cast<uint64_t>(u2);
            return static_cast<int32_t>(prod & 0xFFFFFFFFu);
        }
        case Op::MD_MULH: {
            const int64_t prod = static_cast<int64_t>(src1) * static_cast<int64_t>(src2);
            return static_cast<int32_t>(prod >> 32);
        }
        case Op::MD_MULHSU: {
            const int64_t prod = static_cast<int64_t>(src1) *
                                 static_cast<int64_t>(static_cast<uint64_t>(u2));
            return static_cast<int32_t>(prod >> 32);
        }
        case Op::MD_MULHU: {
            const uint64_t prod = static_cast<uint64_t>(u1) * static_cast<uint64_t>(u2);
            return static_cast<int32_t>(prod >> 32);
        }
        case Op::MD_DIV: {
            if (u2 == 0) return -1;
            if (u1 == 0x80000000u && u2 == 0xFFFFFFFFu) return static_cast<int32_t>(0x80000000u);
            return src1 / src2;
        }
        case Op::MD_DIVU: {
            if (u2 == 0) return -1;
            return static_cast<int32_t>(u1 / u2);
        }
        case Op::MD_REM: {
            if (u2 == 0) return src1;
            if (u1 == 0x80000000u && u2 == 0xFFFFFFFFu) return 0;
            return src1 % src2;
        }
        case Op::MD_REMU: {
            if (u2 == 0) return src1;
            return static_cast<int32_t>(u1 % u2);
        }

        case Op::BR_EQ:  return (src1 == src2) ? 1 : 0;
        case Op::BR_NE:  return (src1 != src2) ? 1 : 0;
        case Op::BR_LT:  return (src1 < src2) ? 1 : 0;
        case Op::BR_GE:  return (src1 >= src2) ? 1 : 0;
        case Op::BR_LTU: return (u1 < u2) ? 1 : 0;
        case Op::BR_GEU: return (u1 >= u2) ? 1 : 0;
        case Op::BR_JAL:
        case Op::BR_JALR:
            return static_cast<int32_t>(pc + 4);

        case Op::UOP_NOP:
        default:
            return 0;
    }
}

bool branchTaken(Op op, int32_t src1, int32_t src2) {
    const uint32_t u1 = static_cast<uint32_t>(src1);
    const uint32_t u2 = static_cast<uint32_t>(src2);
    switch (op) {
        case Op::BR_EQ:  return src1 == src2;
        case Op::BR_NE:  return src1 != src2;
        case Op::BR_LT:  return src1 < src2;
        case Op::BR_GE:  return src1 >= src2;
        case Op::BR_LTU: return u1 < u2;
        case Op::BR_GEU: return u1 >= u2;
        case Op::BR_JAL:
        case Op::BR_JALR:
            return true;
        default:
            return false;
    }
}

uint32_t branchTarget(Op op, uint32_t pc, int32_t src1, int32_t imm) {
    if (op == Op::BR_JALR) {
        return (static_cast<uint32_t>(src1) + static_cast<uint32_t>(imm)) & ~1u;
    }
    return pc + static_cast<uint32_t>(imm);
}

ExecutionUnit::ExecutionUnit(std::string t, int l)
    : type(std::move(t)), latency(l < 1 ? 1 : l) {
    pipe.assign(static_cast<size_t>(latency), Stage{});
}

bool ExecutionUnit::canIssue(bool lane_ready) const {
    // RTL ALU/MUL/DIV: in_ready = cdb_ready (blocks whole unit when lane not ready).
    // BR: in_ready = !holding_cdb; holding only when offer_cdb out is blocked.
    if (!lane_ready) return false;
    return !pipe[0].valid;
}

bool ExecutionUnit::issue(std::string op, int val1, int val2, int r_id) {
    if (!canIssue(true)) return false;

    int value = 0;
    if (op == "ADD") value = val1 + val2;
    else if (op == "SUB") value = val1 - val2;
    else if (op == "MUL") value = val1 * val2;
    else if (op == "DIV") value = (val2 != 0) ? (val1 / val2) : 0;
    else if (op == "BEQ") value = (val1 == val2) ? 1 : 0;
    else if (op == "BNE") value = (val1 != val2) ? 1 : 0;
    else if (op == "CALL" || op == "RET") value = 0;

    const bool is_br = (op == "BEQ" || op == "BNE" || op == "CALL" || op == "RET");
    pipe[0].valid = true;
    pipe[0].rob_id = r_id;
    pipe[0].value = value;
    pipe[0].is_control = is_br;
    // Toy BEQ/BNE/CALL/RET: resolve sideband only (no link CDB).
    pipe[0].offer_cdb = !is_br;
    return true;
}

bool ExecutionUnit::issue(Op op, int val1, int val2, int r_id, int32_t imm, uint32_t pc,
                          bool rd_used) {
    if (!canIssue(true)) return false;
    const bool is_br = (fuForOp(op) == Fu::BR);
    const bool is_jump = (op == Op::BR_JAL || op == Op::BR_JALR);
    pipe[0].valid = true;
    pipe[0].rob_id = r_id;
    pipe[0].value = executeOp(op, val1, val2, imm, pc);
    pipe[0].is_control = is_br;
    // RTL needs_link: only JAL/JALR with rd_used offer CDB; cond BR uses complete2.
    pipe[0].offer_cdb = !is_br || (is_jump && rd_used);
    return true;
}

bool ExecutionUnit::outputValid() const {
    return pipe.back().valid;
}

int ExecutionUnit::outputRobId() const {
    return pipe.back().rob_id;
}

int ExecutionUnit::outputValue() const {
    return pipe.back().value;
}

bool ExecutionUnit::outputOfferCdb() const {
    return pipe.back().offer_cdb;
}

bool ExecutionUnit::outputIsControl() const {
    return pipe.back().is_control;
}

void ExecutionUnit::consumeOutput() {
    assert(pipe.back().valid);
    pipe.back() = Stage{};
}

void ExecutionUnit::advance(bool cdb_ready) {
    // RTL: pipe shifts only when cdb_ready (even if output stage is empty).
    if (!cdb_ready) {
        return;
    }
    // Caller consumes granted outputs before advance. If the CDB stage is still
    // valid here, it was not granted — freeze.
    if (pipe.back().valid) {
        return;
    }
    for (int i = latency - 1; i > 0; --i) {
        pipe[static_cast<size_t>(i)] = pipe[static_cast<size_t>(i - 1)];
    }
    pipe[0] = Stage{};
}

void ExecutionUnit::clear() {
    for (auto& s : pipe) s = Stage{};
}

bool ExecutionUnit::anyValid() const {
    for (const auto& s : pipe) {
        if (s.valid) return true;
    }
    return false;
}

ExecutionEngine::ExecutionEngine(int n_alu, int n_mul, int n_div, int n_br,
                                 int alu_lat, int mul_lat, int div_lat, int br_lat) {
    for (int i = 0; i < n_alu; ++i) units.emplace_back("ALU", alu_lat);
    for (int i = 0; i < n_mul; ++i) units.emplace_back("MUL", mul_lat);
    for (int i = 0; i < n_div; ++i) units.emplace_back("DIV", div_lat);
    for (int i = 0; i < n_br; ++i) units.emplace_back("BR", br_lat);
    lane_ready_.assign(units.size(), true);
}

bool ExecutionEngine::issueInstruction(std::string op, int val1, int val2, int rob_id) {
    std::string required_type = "ALU";
    if (op == "MUL") required_type = "MUL";
    else if (op == "DIV") required_type = "DIV";
    else if (op == "BEQ" || op == "BNE" || op == "CALL" || op == "RET") {
        required_type = "BR";
    }

    for (size_t i = 0; i < units.size(); ++i) {
        auto& unit = units[i];
        if (unit.type == required_type &&
            unit.canIssue(lane_ready_[i])) {
            return unit.issue(op, val1, val2, rob_id);
        }
    }
    return false;
}

bool ExecutionEngine::issueInstruction(Op op, int val1, int val2, int rob_id,
                                       int32_t imm, uint32_t pc, bool rd_used) {
    std::string required_type = "ALU";
    const Fu fu = fuForOp(op);
    if (fu == Fu::MUL) required_type = "MUL";
    else if (fu == Fu::DIV) required_type = "DIV";
    else if (fu == Fu::BR) required_type = "BR";
    else if (fu == Fu::MEM) return false;

    for (size_t i = 0; i < units.size(); ++i) {
        auto& unit = units[i];
        if (unit.type == required_type &&
            unit.canIssue(lane_ready_[i])) {
            return unit.issue(op, val1, val2, rob_id, imm, pc, rd_used);
        }
    }
    return false;
}

std::vector<CdbProducer> ExecutionEngine::collectProducersFixed() const {
    std::vector<CdbProducer> out(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        out[i].source = 0;
        out[i].slot = static_cast<int>(i);
        if (units[i].outputValid() && units[i].outputOfferCdb()) {
            out[i].valid = true;
            out[i].rob_id = units[i].outputRobId();
            out[i].value = units[i].outputValue();
        }
    }
    return out;
}

std::vector<CdbProducer> ExecutionEngine::collectProducers() const {
    std::vector<CdbProducer> out;
    for (const auto& p : collectProducersFixed()) {
        if (p.valid) out.push_back(p);
    }
    return out;
}

void ExecutionEngine::setLaneReady(const std::vector<bool>& ready) {
    lane_ready_.assign(units.size(), true);
    for (size_t i = 0; i < units.size() && i < ready.size(); ++i) {
        lane_ready_[i] = ready[i];
    }
}

std::vector<BrResolve> ExecutionEngine::collectBranchResolves() const {
    std::vector<BrResolve> out;
    for (size_t i = 0; i < units.size(); ++i) {
        if (!units[i].outputValid() || !units[i].outputIsControl()) continue;
        BrResolve r;
        r.slot = static_cast<int>(i);
        r.rob_id = units[i].outputRobId();
        r.value = units[i].outputValue();
        r.offer_cdb = units[i].outputOfferCdb();
        out.push_back(r);
    }
    return out;
}

void ExecutionEngine::applyCdbAndAdvance(const std::unordered_set<int>& granted_slots) {
    for (size_t i = 0; i < units.size(); ++i) {
        auto& u = units[i];
        const bool granted = granted_slots.count(static_cast<int>(i)) > 0;
        if (u.outputValid() && granted) {
            u.consumeOutput();
        }
        // Gate on arbiter cdb_ready (lane_ready_), not merely this cycle's grant.
        const bool ready = (i < lane_ready_.size()) ? lane_ready_[i] : true;
        u.advance(ready);
    }
}

void ExecutionEngine::tick() {
    // Test helper: advance pipes; valid CDB outputs freeze (no auto-grant).
    applyCdbAndAdvance({});
}

void ExecutionEngine::releaseSlot(int unit_index) {
    assert(unit_index >= 0 && unit_index < static_cast<int>(units.size()));
    assert(units[static_cast<size_t>(unit_index)].outputValid());
    units[static_cast<size_t>(unit_index)].consumeOutput();
}

void ExecutionEngine::squashRobTags(const std::vector<int>& rob_tags) {
    for (auto& unit : units) {
        for (auto& s : unit.pipe) {
            if (!s.valid) continue;
            for (int tag : rob_tags) {
                if (s.rob_id == tag) {
                    s = ExecutionUnit::Stage{};
                    break;
                }
            }
        }
    }
}

void ExecutionEngine::printState() const {
    std::cout << "Execution Units State:\n";
    for (size_t i = 0; i < units.size(); i++) {
        const auto& u = units[i];
        std::cout << "  Unit " << i << " (" << u.type << ", " << u.latency << "-stage) | ";
        if (!u.anyValid()) {
            std::cout << "IDLE";
        } else {
            for (int s = 0; s < u.latency; ++s) {
                if (s) std::cout << " -> ";
                if (u.pipe[static_cast<size_t>(s)].valid) {
                    std::cout << "ROB" << u.pipe[static_cast<size_t>(s)].rob_id;
                } else {
                    std::cout << "-";
                }
            }
            if (u.outputValid()) std::cout << " (CDB out)";
        }
        std::cout << "\n";
    }
    std::cout << "-----------------------\n";
}
