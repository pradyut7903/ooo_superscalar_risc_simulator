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

// Parse a signed immediate that may be hex (0x..), negative decimal (-5), or
// negative hex (-0x10). std::istringstream's >> int silently truncates hex at
// the 'x', which would turn a branch target like 0x1024 into 0 — so we parse
// the immediate as a string token and convert it here instead.
int parseInt(const std::string& token) {
    if (token.empty()) return 0;
    bool neg = false;
    std::string t = token;
    if (t[0] == '-') { neg = true; t = t.substr(1); }
    else if (t[0] == '+') { t = t.substr(1); }
    long long value = 0;
    if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) {
        value = std::stoll(t, nullptr, 16);
    } else {
        value = std::stoll(t, nullptr, 10);
    }
    return static_cast<int>(neg ? -value : value);
}

}  // namespace

Processor::Processor(const ProcessorConfig& cfg)
    : config(cfg),
      ifq(cfg.ifq_size),
      rat(cfg.num_regs),
      rs(cfg.rs_size),
      rob(cfg.rob_size),
      lsq(cfg.lsq_size, cfg.cdb_width, cfg.mem_latency, cfg.fwd_latency, cfg.store_forwarding),
      ee(cfg.cdb_width, cfg.num_alu, cfg.num_mul, cfg.num_div,
         cfg.alu_latency, cfg.mul_latency, cfg.div_latency),
      predictor(cfg.pht_size, cfg.btb_size),
      ras(cfg.ras_size),
      arf(cfg.num_regs, 0),
      fetch_pc(cfg.initial_pc),
      cycle_count(0),
      next_inst_id(0),
      flush_pending(false),
      mispredict_outstanding(false),
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
        std::string first;
        if (!(iss >> first)) {
            continue;
        }

        // Data-segment directive: ".mem ADDR VALUE" preloads main memory so
        // benchmarks can start with initialized arrays / linked lists.
        if (first == ".mem") {
            std::string addr_tok, val_tok;
            if (iss >> addr_tok >> val_tok) {
                int addr = parseInt(addr_tok);
                int value = parseInt(val_tok);
                if (addr >= 0 && static_cast<size_t>(addr) < MainMemory.size()) {
                    MainMemory[addr] = value;
                }
            }
            continue;
        }

        std::string opcode;
        int dest = 0;
        int src1 = 0;
        int src2 = 0;
        std::string imm_token;

        if (!(iss >> opcode >> dest >> src1 >> src2)) {
            continue;
        }

        FetchedInstruction inst{};
        inst.pc = parseUint64(first);
        inst.opcode = opcode;
        inst.dest_reg = dest;
        inst.src1_reg = src1;
        inst.src2_reg = src2;
        inst.imm = (iss >> imm_token) ? parseInt(imm_token) : 0;
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

InstInfo* Processor::findMutableByRobId(int rob_id) {
    for (auto& entry : inst_table) {
        if (entry.second.rob_id == rob_id) {
            return &entry.second;
        }
    }
    return nullptr;
}

void Processor::resolveOperand(int logical_reg, bool& ready, int& tag, int& val) {
    // R0 is hardwired to zero; out-of-range register ids (malformed trace) are
    // treated as a ready zero rather than indexing the ARF out of bounds.
    if (logical_reg <= 0 || logical_reg >= config.num_regs) {
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

    InstInfo* info = findMutableByRobId(result.rob_id);
    if (info != nullptr && info->is_branch) {
        ++stats.branches_resolved;
        bool actual_taken = (result.value != 0);
        uint64_t branch_target = (info->imm != 0)
            ? static_cast<uint64_t>(info->imm)
            : info->predicted_target;
        uint64_t actual_next_pc = actual_taken ? branch_target : info->fallthrough_pc;

        // A branch is mispredicted if the DIRECTION is wrong, OR it is taken but the
        // predicted TARGET differs from the real target (e.g. a stale/aliased BTB
        // entry). Checking direction alone misses target mispredictions and lets
        // wrong-path instructions commit.
        const bool dir_wrong = (actual_taken != info->predicted_taken);
        const bool target_wrong = (actual_taken && info->predicted_target != branch_target);
        if (dir_wrong || target_wrong) {
            ++stats.branch_mispredicts;
            // Recovery is deferred until the branch reaches the ROB head in
            // commitStage, so older (correct-path) in-flight work is NOT squashed.
            info->mispredicted = true;
            info->recovery_pc = actual_next_pc;
            mispredict_outstanding = true;  // freeze the front end (stop wrong-path fetch)
        }

        predictor.update(info->pc, actual_taken, actual_next_pc);
    }
}

void Processor::rebuildPipelineState() {
    rs = ReservationStation(config.rs_size);
    rob = ReorderBuffer(config.rob_size);
    lsq = LoadStoreQueue(config.lsq_size, config.cdb_width,
                         config.mem_latency, config.fwd_latency, config.store_forwarding);
    ee = ExecutionEngine(config.cdb_width, config.num_alu, config.num_mul, config.num_div,
                         config.alu_latency, config.mul_latency, config.div_latency);
    ifq.flush();
}

void Processor::performFlush() {
    ++stats.flushes;
    stats.squashed += inst_table.size();

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
    mispredict_outstanding = false;
}

// ---------------------------------------------------------------------------
// Stage 5 (last in tick): FETCH
// Reads WIDTH instructions from the I-cache, predicts branches, enqueues IFQ.
// ---------------------------------------------------------------------------
void Processor::fetchStage() {
    // Freeze the front end while a misprediction or memory-order violation is
    // pending recovery, so no further wrong-path instructions are fetched.
    if (mispredict_outstanding || mov_pending) {
        return;
    }

    for (int i = 0; i < config.width; ++i) {
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
        ++stats.fetched;

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
    if (mispredict_outstanding || mov_pending) {
        return;
    }

    for (int i = 0; i < config.width; ++i) {
        if (!rob.hasSpace()) {
            ++stats.stall_rob;
            break;
        }

        FetchedInstruction inst{};
        if (!ifq.peek(inst)) {
            // Only blame the front end while there is still code to deliver;
            // an empty IFQ during the final drain is not a real stall.
            if (icache.count(fetch_pc)) {
                ++stats.stall_front;
            }
            break;
        }

        const bool needs_mem = isMemoryOp(inst.opcode);
        if (needs_mem && !lsq.hasSpace()) {
            ++stats.stall_lsq;
            break;
        }
        if (!needs_mem && !rs.hasSpace()) {
            ++stats.stall_rs;
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
        info.is_addi = iequals(inst.opcode, "ADDI");
        info.predicted_taken = inst.predicted_taken;
        info.predicted_target = inst.predicted_target;
        info.fallthrough_pc = inst.pc + INSTR_SIZE;
        info.rat_snapshot = rat.getState();
        ras.getSnapshot(info.ras_tos, info.ras_count);

        resolveOperand(inst.src1_reg, info.src1_ready, info.src1_tag, info.src1_val);
        if (info.is_addi) {
            // ADDI: second operand is the immediate, always an available constant.
            info.src2_ready = true;
            info.src2_tag = -1;
            info.src2_val = info.imm;
        } else {
            resolveOperand(inst.src2_reg, info.src2_ready, info.src2_tag, info.src2_val);
        }

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
        ++stats.issued;
    }
}

// ---------------------------------------------------------------------------
// Stage 3: DISPATCH (Issue to Execution)
// Moves ready RS entries to ExecutionEngine; dispatches ready loads to LSQ.
// ---------------------------------------------------------------------------
void Processor::dispatchStage() {
    // NOTE: dispatch is intentionally NOT frozen on a pending branch misprediction.
    // Instructions older than the branch are already in flight and must keep
    // executing so they can drain to commit (the branch retires before recovery).
    if (mov_pending) {
        return;
    }

    // NOTE: operands are resolved once at issue and thereafter updated only by CDB
    // snooping (broadcastCDB -> rs.updateFromCDB / updateInstOperandsFromCDB). We do
    // NOT re-query the RAT here: the RAT reflects the *latest* writer of a register,
    // not the producer this consumer renamed against, so re-querying would make an
    // instruction wait on a younger (or its own) ROB tag and deadlock.

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

            // setAddress reports a memory-order violation. Ignore violations raised
            // by wrong-path memory ops while a branch misprediction is being
            // recovered — those instructions are about to be squashed anyway.
            if (lsq.setAddress(info.inst_id, info.address) && !mispredict_outstanding) {
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
                ++stats.mem_order_violations;
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
        if (issued >= config.width) {
            break;
        }

        InstInfo* info = findInstById(ready_id);
        if (info == nullptr || info->dispatched_alu) {
            rs.removeInstruction(ready_id);
            continue;
        }

        // ADDI executes on an ALU as (src1 + immediate); the immediate already
        // lives in src2_val, so it maps onto the engine's ADD path.
        const std::string ee_op = info->is_addi ? "ADD" : info->opcode;
        if (ee.issueInstruction(ee_op, info->src1_val, info->src2_val, info->rob_id)) {
            info->dispatched_alu = true;
            rs.removeInstruction(ready_id);
            issued++;
        }
    }

    for (int i = 0; i < config.width; ++i) {
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
    // Memory-order-violation recovery squashes from the offending instruction and
    // is handled up front (its recovery state was captured in dispatchStage).
    if (mov_pending) {
        performFlush();
        return;
    }

    for (int i = 0; i < config.width; ++i) {
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
            ++stats.committed_store;
        } else if (info->is_load) {
            lsq.freeLoad(info->inst_id);
            if (dest_reg > 0 && dest_reg < config.num_regs) {
                arf[dest_reg] = value;
                rat.commit(dest_reg, rob_id);
            }
            ++stats.committed_load;
        } else {
            if (dest_reg > 0 && dest_reg < config.num_regs) {
                arf[dest_reg] = value;
                rat.commit(dest_reg, rob_id);
            }
            if (info->is_branch || info->is_call || info->is_ret) {
                ++stats.committed_branch;
            } else if (iequals(info->opcode, "MUL")) {
                ++stats.committed_mul;
            } else if (iequals(info->opcode, "DIV")) {
                ++stats.committed_div;
            } else {
                ++stats.committed_alu;  // ADD / SUB / ADDI
            }
        }

        // A mispredicted branch is retired normally, then triggers recovery so that
        // only younger (wrong-path) instructions are squashed.
        const bool flush_after = info->mispredicted;
        if (flush_after) {
            flush_rat_snapshot = info->rat_snapshot;
            flush_ras_tos = info->ras_tos;
            flush_ras_count = info->ras_count;
            flush_pc = info->recovery_pc;
        }

        ++stats.committed;
        inst_table.erase(info->inst_id);

        if (flush_after) {
            performFlush();
            return;
        }
    }
}

void Processor::tick() {
    ++cycle_count;
    stats.cycles = cycle_count;

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

namespace {
double safe_div(uint64_t a, uint64_t b) {
    return (b == 0) ? 0.0 : static_cast<double>(a) / static_cast<double>(b);
}
}  // namespace

void Processor::printStats() const {
    const ProcessorStats& s = stats;
    const double ipc = safe_div(s.committed, s.cycles);
    const double mpki = safe_div(s.branch_mispredicts * 1000, s.committed);
    const double mispred_rate = safe_div(s.branch_mispredicts * 100, s.branches_resolved);
    const bool hit_cap = cycle_count >= static_cast<uint64_t>(config.max_cycles);

    std::cout << "\n================ Simulation Statistics ================\n";
    std::cout << "  Config: width=" << config.width
              << " ROB=" << config.rob_size
              << " RS=" << config.rs_size
              << " LSQ=" << config.lsq_size
              << " IFQ=" << config.ifq_size
              << " CDB=" << config.cdb_width << "\n";
    std::cout << "          ALU=" << config.num_alu << "(" << config.alu_latency << "c)"
              << " MUL=" << config.num_mul << "(" << config.mul_latency << "c)"
              << " DIV=" << config.num_div << "(" << config.div_latency << "c)"
              << " memLat=" << config.mem_latency
              << " fwd=" << (config.store_forwarding ? "on" : "off") << "\n";
    std::cout << "          PHT=" << config.pht_size << " BTB=" << config.btb_size
              << " RAS=" << config.ras_size << "\n";
    std::cout << "-------------------------------------------------------\n";
    std::cout << "  Cycles                : " << s.cycles
              << (hit_cap ? "  (HIT max_cycles cap!)" : "") << "\n";
    std::cout << "  Instructions committed: " << s.committed << "\n";
    std::cout << "  Instructions fetched  : " << s.fetched << "\n";
    std::cout << "  Instructions issued   : " << s.issued << "\n";
    std::cout << "  IPC                   : " << ipc << "\n";
    std::cout << "  Commit mix            : ALU=" << s.committed_alu
              << " MUL=" << s.committed_mul
              << " DIV=" << s.committed_div
              << " LD=" << s.committed_load
              << " ST=" << s.committed_store
              << " BR=" << s.committed_branch << "\n";
    std::cout << "  Branches resolved     : " << s.branches_resolved << "\n";
    std::cout << "  Branch mispredicts    : " << s.branch_mispredicts
              << "  (rate " << mispred_rate << "%, MPKI " << mpki << ")\n";
    std::cout << "  Pipeline flushes      : " << s.flushes << "\n";
    std::cout << "  Squashed instructions : " << s.squashed << "\n";
    std::cout << "  Memory-order violations: " << s.mem_order_violations << "\n";
    std::cout << "  Issue stall cycles    : ROB=" << s.stall_rob
              << " RS=" << s.stall_rs
              << " LSQ=" << s.stall_lsq
              << " front=" << s.stall_front << "\n";
    std::cout << "=======================================================\n";
}

void Processor::printStatsCSVHeader() {
    std::cout << "benchmark,width,rob,rs,lsq,ifq,cdb,alu,mul,div,"
              << "alu_lat,mul_lat,div_lat,mem_lat,fwd,pht,btb,ras,"
              << "cycles,committed,fetched,issued,ipc,"
              << "c_alu,c_mul,c_div,c_load,c_store,c_branch,"
              << "branches,mispredicts,mispred_rate,flushes,squashed,mov,"
              << "stall_rob,stall_rs,stall_lsq,stall_front,hit_cap\n";
}

void Processor::printStatsCSV(const std::string& label) const {
    const ProcessorStats& s = stats;
    const double ipc = safe_div(s.committed, s.cycles);
    const double mispred_rate = safe_div(s.branch_mispredicts, s.branches_resolved);
    const bool hit_cap = cycle_count >= static_cast<uint64_t>(config.max_cycles);

    std::cout << label << ','
              << config.width << ',' << config.rob_size << ',' << config.rs_size << ','
              << config.lsq_size << ',' << config.ifq_size << ',' << config.cdb_width << ','
              << config.num_alu << ',' << config.num_mul << ',' << config.num_div << ','
              << config.alu_latency << ',' << config.mul_latency << ',' << config.div_latency << ','
              << config.mem_latency << ',' << (config.store_forwarding ? 1 : 0) << ','
              << config.pht_size << ',' << config.btb_size << ',' << config.ras_size << ','
              << s.cycles << ',' << s.committed << ',' << s.fetched << ',' << s.issued << ','
              << ipc << ','
              << s.committed_alu << ',' << s.committed_mul << ',' << s.committed_div << ','
              << s.committed_load << ',' << s.committed_store << ',' << s.committed_branch << ','
              << s.branches_resolved << ',' << s.branch_mispredicts << ',' << mispred_rate << ','
              << s.flushes << ',' << s.squashed << ',' << s.mem_order_violations << ','
              << s.stall_rob << ',' << s.stall_rs << ',' << s.stall_lsq << ',' << s.stall_front << ','
              << (hit_cap ? 1 : 0) << '\n';
}

void Processor::printArchState() const {
    std::cout << "\n--------- Final Architectural State ---------\n";
    std::cout << "Registers (nonzero):\n";
    bool any = false;
    for (int r = 1; r < config.num_regs; ++r) {
        if (arf[r] != 0) {
            std::cout << "  R" << r << " = " << arf[r] << "\n";
            any = true;
        }
    }
    if (!any) std::cout << "  (all zero)\n";

    std::cout << "Memory (nonzero words):\n";
    any = false;
    for (size_t a = 0; a < MainMemory.size(); ++a) {
        if (MainMemory[a] != 0) {
            std::cout << "  MEM[" << a << "] = " << MainMemory[a] << "\n";
            any = true;
        }
    }
    if (!any) std::cout << "  (all zero)\n";
    std::cout << "--------------------------------------------\n";
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
