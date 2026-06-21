#include "processor.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

constexpr uint64_t INSTR_SIZE = 4;

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

uint64_t parseUint64(const std::string& token) {
    if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
        return std::stoull(token, nullptr, 16);
    }
    return std::stoull(token, nullptr, 10);
}

}  // namespace

Processor::Processor(const ProcessorConfig& cfg)
    : config(cfg),
      ifq(cfg.ifq_size),
      rat(cfg.num_regs),
      rs(cfg.rs_size),
      rob(cfg.rob_size),
      lsq(cfg.lsq_size, cfg.cdb_width),
      ee(cfg.cdb_width),
      predictor(cfg.pht_size, cfg.btb_size),
      ras(cfg.ras_size),
      arf(cfg.num_regs, 0),
      fetch_pc(cfg.initial_pc),
      cycle_count(0),
      next_inst_id(0),
      flush_pending(false),
      flush_pc(0),
      flush_ras_tos(0),
      flush_ras_count(0),
      flush_inst_id(0),
      mov_pending(false) {}

void Processor::loadInstructionCacheFromVector(const std::vector<FetchedInstruction>& program) {
    icache.clear();
    for (const auto& inst : program) {
        icache[inst.pc] = inst;
    }
}

void Processor::loadInstructionCache(const std::string& trace_path) {
    std::ifstream file(trace_path);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open trace file: " + trace_path);
    }

    icache.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string pc_token;
        std::string opcode;
        int dest = 0;
        int src1 = 0;
        int src2 = 0;
        int imm = 0;

        if (!(iss >> pc_token >> opcode >> dest >> src1 >> src2)) {
            continue;
        }
        iss >> imm;

        FetchedInstruction inst{};
        inst.pc = parseUint64(pc_token);
        inst.opcode = opcode;
        inst.dest_reg = dest;
        inst.src1_reg = src1;
        inst.src2_reg = src2;
        inst.imm = imm;
        inst.predicted_taken = false;
        inst.predicted_target = 0;

        icache[inst.pc] = inst;
    }
}

bool Processor::isMemoryOp(const std::string& op) const {
    return iequals(op, "LOAD") || iequals(op, "STORE");
}

bool Processor::isBranchOp(const std::string& op) const {
    return iequals(op, "BEQ") || iequals(op, "BNE") ||
           iequals(op, "CALL") || iequals(op, "RET");
}

InstInfo* Processor::findInstById(uint32_t inst_id) {
    auto it = inst_table.find(inst_id);
    if (it == inst_table.end()) {
        return nullptr;
    }
    return &it->second;
}

const InstInfo* Processor::findInstByRobId(int rob_id) const {
    for (const auto& entry : inst_table) {
        if (entry.second.rob_id == rob_id) {
            return &entry.second;
        }
    }
    return nullptr;
}

void Processor::resolveOperand(int logical_reg, bool& ready, int& tag, int& val) {
    if (logical_reg <= 0) {
        ready = true;
        tag = -1;
        val = 0;
        return;
    }

    tag = rat.lookup(logical_reg);
    if (tag == -1) {
        ready = true;
        val = arf[logical_reg];
    } else if (rob.isReady(tag)) {
        ready = true;
        val = rob.getValue(tag);
    } else {
        ready = false;
        val = 0;
    }
}

bool Processor::canDispatchLoadConservative(const InstInfo& load) const {
    for (const auto& entry : inst_table) {
        const InstInfo& other = entry.second;
        if (other.is_store && other.inst_id < load.inst_id && !other.addr_computed) {
            return false;
        }
    }
    return true;
}

void Processor::updateInstOperandsFromCDB(int rob_tag, int value) {
    for (auto& entry : inst_table) {
        InstInfo& info = entry.second;
        if (info.completed) {
            continue;
        }
        if (!info.src1_ready && info.src1_tag == rob_tag) {
            info.src1_ready = true;
            info.src1_val = value;
        }
        if (!info.src2_ready && info.src2_tag == rob_tag) {
            info.src2_ready = true;
            info.src2_val = value;
        }
        if (info.is_store && !info.store_data_ready && info.store_data_tag == rob_tag) {
            info.store_data_ready = true;
            info.store_data_val = value;
            lsq.setStoreData(info.inst_id, value);
        }
    }
}

void Processor::broadcastCDB(const CDBResult& result) {
    rs.updateFromCDB(result.rob_id, result.value);
    rob.writeResult(result.rob_id, result.value);
    updateInstOperandsFromCDB(result.rob_id, result.value);

    const InstInfo* info = findInstByRobId(result.rob_id);
    if (info != nullptr && info->is_branch) {
        bool actual_taken = (result.value != 0);
        uint64_t branch_target = (info->imm != 0)
            ? static_cast<uint64_t>(info->imm)
            : info->predicted_target;

        if (actual_taken != info->predicted_taken) {
            flush_pending = true;
            flush_pc = actual_taken ? branch_target : info->fallthrough_pc;
            flush_rat_snapshot = info->rat_snapshot;
            flush_ras_tos = info->ras_tos;
            flush_ras_count = info->ras_count;
            flush_inst_id = info->inst_id;
        }

        predictor.update(info->pc, actual_taken,
                         actual_taken ? branch_target : info->fallthrough_pc);
    }
}

void Processor::rebuildPipelineState() {
    rs = ReservationStation(config.rs_size);
    rob = ReorderBuffer(config.rob_size);
    lsq = LoadStoreQueue(config.lsq_size, config.cdb_width);
    ee = ExecutionEngine(config.cdb_width);
    ifq.flush();
}

void Processor::performFlush() {
    rat.restoreState(flush_rat_snapshot);
    ras.restoreState(flush_ras_tos, flush_ras_count);
    rebuildPipelineState();

    inst_table.clear();

    // Committed values live in ARF; in-flight rename tags are invalid after squash.
    std::vector<int> arf_view(config.num_regs, -1);
    rat.restoreState(arf_view);

    fetch_pc = flush_pc;
    flush_pending = false;
    mov_pending = false;
}

// ---------------------------------------------------------------------------
// Stage 5 (last in tick): FETCH
// Reads WIDTH instructions from the I-cache, predicts branches, enqueues IFQ.
// ---------------------------------------------------------------------------
void Processor::fetchStage() {
    if (flush_pending) {
        return;
    }

    for (int i = 0; i < ProcessorConfig::WIDTH; ++i) {
        if (ifq.isFull()) {
            break;
        }

        auto cache_it = icache.find(fetch_pc);
        if (cache_it == icache.end()) {
            break;
        }

        FetchedInstruction inst = cache_it->second;
        inst.predicted_taken = false;
        inst.predicted_target = fetch_pc + INSTR_SIZE;

        if (iequals(inst.opcode, "CALL")) {
            ras.push(fetch_pc + INSTR_SIZE);
            inst.predicted_taken = true;
            inst.predicted_target = (inst.imm != 0)
                ? static_cast<uint64_t>(inst.imm)
                : static_cast<uint64_t>(inst.src1_reg);
        } else if (iequals(inst.opcode, "RET")) {
            uint64_t ret_target = 0;
            if (ras.pop(ret_target)) {
                inst.predicted_taken = true;
                inst.predicted_target = ret_target;
            }
        } else if (isBranchOp(inst.opcode)) {
            uint64_t target = 0;
            inst.predicted_taken = predictor.predict(fetch_pc, target);
            if (inst.predicted_taken) {
                inst.predicted_target = target;
            } else {
                inst.predicted_target = fetch_pc + INSTR_SIZE;
            }
        }

        if (!ifq.enqueue(inst)) {
            break;
        }

        if (inst.predicted_taken) {
            fetch_pc = inst.predicted_target;
            break;
        }

        fetch_pc += INSTR_SIZE;
    }
}

// ---------------------------------------------------------------------------
// Stage 4: ISSUE (Rename / Dispatch into ROB, RS, LSQ)
// Pops WIDTH instructions from IFQ, allocates ROB/RS/LSQ, updates RAT.
// ---------------------------------------------------------------------------
void Processor::issueStage() {
    if (flush_pending) {
        return;
    }

    for (int i = 0; i < ProcessorConfig::WIDTH; ++i) {
        if (!rob.hasSpace()) {
            break;
        }

        FetchedInstruction inst{};
        if (!ifq.peek(inst)) {
            break;
        }

        const bool needs_mem = isMemoryOp(inst.opcode);
        if (needs_mem && !lsq.hasSpace()) {
            break;
        }
        if (!needs_mem && !rs.hasSpace()) {
            break;
        }

        ifq.dequeue(inst);

        int rob_id = rob.dispatch(inst.opcode, inst.dest_reg);
        if (rob_id < 0) {
            break;
        }

        InstInfo info{};
        info.inst_id = next_inst_id++;
        info.rob_id = rob_id;
        info.opcode = inst.opcode;
        info.pc = inst.pc;
        info.dest_reg = inst.dest_reg;
        info.src1_reg = inst.src1_reg;
        info.src2_reg = inst.src2_reg;
        info.imm = inst.imm;
        info.is_load = iequals(inst.opcode, "LOAD");
        info.is_store = iequals(inst.opcode, "STORE");
        info.is_branch = iequals(inst.opcode, "BEQ") || iequals(inst.opcode, "BNE");
        info.is_call = iequals(inst.opcode, "CALL");
        info.is_ret = iequals(inst.opcode, "RET");
        info.predicted_taken = inst.predicted_taken;
        info.predicted_target = inst.predicted_target;
        info.fallthrough_pc = inst.pc + INSTR_SIZE;
        info.rat_snapshot = rat.getState();
        ras.getSnapshot(info.ras_tos, info.ras_count);

        resolveOperand(inst.src1_reg, info.src1_ready, info.src1_tag, info.src1_val);
        resolveOperand(inst.src2_reg, info.src2_ready, info.src2_tag, info.src2_val);

        if (info.is_load || info.is_store) {
            if (!lsq.allocate(info.inst_id, rob_id, info.is_load)) {
                break;
            }
        } else {
            if (!rs.allocate(info.inst_id,
                              info.src1_ready, info.src1_tag, info.src1_val,
                              info.src2_ready, info.src2_tag, info.src2_val)) {
                break;
            }
        }

        if (info.dest_reg > 0 && !info.is_store) {
            rat.rename(info.dest_reg, rob_id);
        }

        if (info.is_store && info.dest_reg > 0) {
            resolveOperand(info.dest_reg, info.store_data_ready, info.store_data_tag, info.store_data_val);
            if (info.store_data_ready) {
                lsq.setStoreData(info.inst_id, info.store_data_val);
            }
        }

        inst_table[info.inst_id] = info;
    }
}

// ---------------------------------------------------------------------------
// Stage 3: DISPATCH (Issue to Execution)
// Moves ready RS entries to ExecutionEngine; dispatches ready loads to LSQ.
// ---------------------------------------------------------------------------
void Processor::dispatchStage() {
    if (flush_pending) {
        return;
    }

    for (auto& entry : inst_table) {
        InstInfo& info = entry.second;
        if (info.completed || info.dispatched_alu || info.is_load || info.is_store) {
            continue;
        }
        resolveOperand(info.src1_reg, info.src1_ready, info.src1_tag, info.src1_val);
        resolveOperand(info.src2_reg, info.src2_ready, info.src2_tag, info.src2_val);
        rs.syncOperands(info.inst_id,
                        info.src1_ready, info.src1_tag, info.src1_val,
                        info.src2_ready, info.src2_tag, info.src2_val);
    }

    for (auto& entry : inst_table) {
        InstInfo& info = entry.second;
        if (info.completed) {
            continue;
        }

        if ((info.is_load || info.is_store) && !info.addr_computed) {
            if (!info.src1_ready) {
                continue;
            }
            info.address = info.src1_val + info.imm;
            info.addr_computed = true;

            if (lsq.setAddress(info.inst_id, info.address)) {
                if (info.is_store) {
                    for (const auto& entry : inst_table) {
                        const InstInfo& other = entry.second;
                        if (other.is_load && other.inst_id > info.inst_id &&
                            other.addr_computed && other.address == info.address) {
                            conservative_load_pcs.insert(other.pc);
                        }
                    }
                    flush_pc = info.pc;
                } else {
                    flush_pc = info.pc;
                }
                mov_pending = true;
                flush_pending = true;
                flush_rat_snapshot = info.rat_snapshot;
                flush_ras_tos = info.ras_tos;
                flush_ras_count = info.ras_count;
                flush_inst_id = info.inst_id;
                return;
            }

            if (info.is_store && info.store_data_ready) {
                lsq.setStoreData(info.inst_id, info.store_data_val);
            }
        }

        if (info.is_store && info.addr_computed && info.store_data_ready && !info.completed) {
            lsq.setStoreData(info.inst_id, info.store_data_val);
            rob.writeResult(info.rob_id, 0);
            info.completed = true;
        }
    }

    int issued = 0;
    const std::vector<uint32_t> ready_insts = rs.collectReadyInstructions();
    for (uint32_t ready_id : ready_insts) {
        if (issued >= ProcessorConfig::WIDTH) {
            break;
        }

        InstInfo* info = findInstById(ready_id);
        if (info == nullptr || info->dispatched_alu) {
            rs.removeInstruction(ready_id);
            continue;
        }

        if (ee.issueInstruction(info->opcode, info->src1_val, info->src2_val, info->rob_id)) {
            info->dispatched_alu = true;
            rs.removeInstruction(ready_id);
            issued++;
        }
    }

    for (int i = 0; i < ProcessorConfig::WIDTH; ++i) {
        for (auto& entry : inst_table) {
            InstInfo& info = entry.second;
            if (!info.is_load || info.dispatched_load || !info.addr_computed || info.completed) {
                continue;
            }
            if (conservative_load_pcs.count(info.pc) && !canDispatchLoadConservative(info)) {
                continue;
            }
            if (lsq.dispatchLoad(info.inst_id)) {
                info.dispatched_load = true;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 2: WRITEBACK
// Ticks ExecutionEngine and LSQ, broadcasts CDB results to RS and ROB.
// ---------------------------------------------------------------------------
void Processor::writebackStage() {
    std::vector<CDBResult> ee_results = ee.tick();
    std::vector<CDBResult> lsq_results = lsq.tick();

    for (const auto& result : ee_results) {
        broadcastCDB(result);
        const InstInfo* info = findInstByRobId(result.rob_id);
        if (info != nullptr) {
            InstInfo* mutable_info = findInstById(info->inst_id);
            if (mutable_info != nullptr) {
                mutable_info->completed = true;
            }
        }
    }

    for (const auto& result : lsq_results) {
        broadcastCDB(result);
        const InstInfo* info = findInstByRobId(result.rob_id);
        if (info != nullptr) {
            InstInfo* mutable_info = findInstById(info->inst_id);
            if (mutable_info != nullptr) {
                mutable_info->completed = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 1 (first in tick): COMMIT
// In-order ROB commit, ARF writeback, store commit, flush on mispredict/MOV.
// ---------------------------------------------------------------------------
void Processor::commitStage() {
    if (flush_pending || mov_pending) {
        performFlush();
        return;
    }

    for (int i = 0; i < ProcessorConfig::WIDTH; ++i) {
        int dest_reg = 0;
        int value = 0;
        int rob_id = -1;

        if (!rob.commit(dest_reg, value, rob_id)) {
            break;
        }

        const InstInfo* info = findInstByRobId(rob_id);
        if (info == nullptr) {
            continue;
        }

        if (info->is_store) {
            lsq.commitStore(info->inst_id);
        } else if (info->is_load) {
            lsq.freeLoad(info->inst_id);
            if (dest_reg > 0) {
                arf[dest_reg] = value;
                rat.commit(dest_reg, rob_id);
            }
        } else {
            if (dest_reg > 0) {
                arf[dest_reg] = value;
                rat.commit(dest_reg, rob_id);
            }
        }

        inst_table.erase(info->inst_id);
    }
}

void Processor::tick() {
    ++cycle_count;

    commitStage();
    writebackStage();
    dispatchStage();
    issueStage();
    fetchStage();
}

bool Processor::isFinished() const {
    if (cycle_count >= static_cast<uint64_t>(config.max_cycles)) {
        return true;
    }

    const bool fetch_done = icache.find(fetch_pc) == icache.end();
    return fetch_done && ifq.isEmpty() && inst_table.empty() && rob.isEmpty();
}

void Processor::run() {
    while (!isFinished()) {
        tick();
    }
}

void Processor::printState() const {
    std::cout << "\n=== Cycle " << cycle_count << " | PC=0x" << std::hex << fetch_pc << std::dec << " ===\n";
    ifq.printState();
    rob.printState();
    rs.printState();
    lsq.printState();
    ee.printState();
    rat.printState();
    ras.printState();
}
