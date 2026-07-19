#include "processor.hpp"
#include "decode.hpp"
#include "hex_loader.hpp"
#include "trace_loader.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>
#include <unordered_set>

namespace {

constexpr uint64_t INSTR_SIZE = 4;
constexpr uint32_t INSTR_INVALID = 0xFFFFFFFFu;

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

// Short name for ROB display / toy-compat stats (not used for execute).
std::string opRobName(Op op) {
    switch (op) {
        case Op::ALU_ADD: return "ADD";
        case Op::ALU_SUB: return "SUB";
        case Op::ALU_SLL: return "SLL";
        case Op::ALU_SLT: return "SLT";
        case Op::ALU_SLTU: return "SLTU";
        case Op::ALU_XOR: return "XOR";
        case Op::ALU_SRL: return "SRL";
        case Op::ALU_SRA: return "SRA";
        case Op::ALU_OR: return "OR";
        case Op::ALU_AND: return "AND";
        case Op::ALU_LUI: return "LUI";
        case Op::ALU_AUIPC: return "AUIPC";
        case Op::MD_MUL: return "MUL";
        case Op::MD_MULH: return "MULH";
        case Op::MD_MULHSU: return "MULHSU";
        case Op::MD_MULHU: return "MULHU";
        case Op::MD_DIV: return "DIV";
        case Op::MD_DIVU: return "DIVU";
        case Op::MD_REM: return "REM";
        case Op::MD_REMU: return "REMU";
        case Op::BR_EQ: return "BEQ";
        case Op::BR_NE: return "BNE";
        case Op::BR_LT: return "BLT";
        case Op::BR_GE: return "BGE";
        case Op::BR_LTU: return "BLTU";
        case Op::BR_GEU: return "BGEU";
        case Op::BR_JAL: return "JAL";
        case Op::BR_JALR: return "JALR";
        case Op::MEM_LB: return "LB";
        case Op::MEM_LH: return "LH";
        case Op::MEM_LW: return "LW";
        case Op::MEM_LBU: return "LBU";
        case Op::MEM_LHU: return "LHU";
        case Op::MEM_SB: return "SB";
        case Op::MEM_SH: return "SH";
        case Op::MEM_SW: return "SW";
        default: return "NOP";
    }
}

}  // namespace

Processor::Processor(const ProcessorConfig& cfg)
    : config(cfg),
      ifq(cfg.ifq_size),
      dispatch_reg(cfg.width),
      rat(cfg.num_regs),
      rat_ckpts(cfg.rob_size, cfg.num_regs),
      rs(cfg.rs_size),
      rob(cfg.rob_size),
      lsq(cfg.lsq_size, cfg.num_lsq, cfg.store_buf_depth,
          cfg.mem_latency, cfg.fwd_latency, cfg.store_forwarding, cfg.width),
      ee(cfg.num_alu, cfg.num_mul, cfg.num_div, cfg.num_br,
         cfg.alu_latency, cfg.mul_latency, cfg.div_latency, cfg.br_latency),
      cdb_arbiter(cfg.cdb_width,
                  cfg.num_alu + cfg.num_mul + cfg.num_div + cfg.num_br + cfg.num_lsq),
      predictor(cfg.pht_size, cfg.btb_size),
      ras(cfg.ras_size),
      arf(cfg.num_regs, 0),
      fetch_pc(cfg.initial_pc),
      cycle_count(0),
      next_inst_id(0) {
    if (cfg.mem_system == MemSystem::Cached) {
        CachedMemConfig mc;
        mc.fetch_width = cfg.width;
        mc.line_bytes = cfg.cache_line_bytes;
        mc.d_sets = cfg.dcache_sets;
        mc.d_ways = cfg.dcache_ways;
        mc.d_mshr = cfg.dcache_mshr;
        mc.d_ufp = cfg.dcache_ufp_ports;
        mc.d_mshr_waiters = cfg.mshr_waiters;
        mc.i_sets = cfg.icache_sets;
        mc.i_ways = cfg.icache_ways;
        mc.i_mshr = cfg.icache_mshr;
        mc.dram_outstanding = cfg.dram_outstanding;
        mc.dram_lat = cfg.dram_lat_cycles;
        mc.load_hit_latency = cfg.load_hit_latency;
        mc.fetch_hit_latency = cfg.fetch_hit_latency;
        memsys = std::make_unique<CachedMemorySystem>(mc);
        lsq.setMemorySystem(memsys.get());
    } else {
        memsys = std::make_unique<IdealMemorySystem>(cfg.width);
        // Ideal LSQ keeps internal latency model (no data path through memsys).
    }
}

bool Processor::useCachedMem() const {
    return memsys && memsys->kind() == MemSystem::Cached;
}

void Processor::loadInstructionCacheFromVector(const std::vector<FetchedInstruction>& program) {
    icache.clear();
    for (const auto& inst : program) {
        icache[inst.pc] = inst;
    }
}

void Processor::loadInstructionCache(const std::string& trace_path) {
    // Parsing is shared with the golden model via trace_loader so both read the
    // input identically (see trace_loader.hpp).
    hex_mode = false;
    fetch_halted = false;
    imem.clear();
    MainMemoryByteAddressed = false;
    ifq.flush();
    dispatch_reg.flush();
    ParsedProgram prog = loadTraceFile(trace_path);
    icache = std::move(prog.icache);
    for (const auto& kv : prog.mem_init) {
        int addr = kv.first;
        if (addr >= 0 && static_cast<size_t>(addr) < MainMemory.size()) {
            MainMemory[addr] = kv.second;
        }
    }
}

void Processor::loadHexProgram(const std::string& imem_path, const std::string& dmem_path) {
    hex_mode = true;
    fetch_halted = false;
    icache.clear();
    ifq.flush();
    dispatch_reg.flush();

    // RV32 architectural file; RESET_PC = 0 in pkg_cpu.
    config.num_regs = 32;
    config.initial_pc = 0;
    fetch_pc = 0;
    arf.assign(config.num_regs, 0);
    rat = RegisterAliasTable(config.num_regs);
    rat_ckpts = RatCheckpoints(config.rob_size, config.num_regs);

    imem = loadHexWords(imem_path);
    imem_req_outstanding = false;
    fetch_buf_ = FetchBuf{};
    fetch_req_pred_taken_.clear();
    fetch_req_pred_target_.clear();
    fetch_redirect_hold_ = false;
    if (auto* cached = dynamic_cast<CachedMemorySystem*>(memsys.get())) {
        cached->setInstrMem(&imem);
        cached->reset();
    }

    MainMemoryByteAddressed = true;
    std::fill(MainMemory.begin(), MainMemory.end(), 0);
    if (!dmem_path.empty()) {
        std::vector<uint32_t> dwords = loadHexWords(dmem_path);
        for (size_t i = 0; i < dwords.size() && i < MainMemory.size(); ++i) {
            MainMemory[i] = static_cast<int>(dwords[i]);
        }
    }
}

bool Processor::imemWord(uint64_t pc, uint32_t& out_word) const {
    if ((pc & 3ull) != 0) {
        return false;
    }
    const size_t idx = static_cast<size_t>(pc >> 2);
    if (idx >= imem.size()) {
        return false;
    }
    out_word = imem[idx];
    return true;
}

bool Processor::isMemoryOp(const std::string& op) const {
    return iequals(op, "LOAD") || iequals(op, "STORE");
}

bool Processor::isBranchOp(const std::string& op) const {
    return iequals(op, "BEQ") || iequals(op, "BNE") ||
           iequals(op, "CALL") || iequals(op, "RET");
}

bool Processor::isControl(const InstInfo& info) const {
    return info.is_branch || info.is_jump || info.is_call || info.is_ret;
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

void Processor::resolveBranch(InstInfo& info, int fu_value) {
    if (info.br_resolved) return;

    ++stats.branches_resolved;

    bool actual_taken = false;
    uint64_t actual_target = 0;
    if (info.has_uop) {
        actual_taken = info.is_jump ||
                       branchTaken(info.op, info.src1_val, info.src2_val);
        actual_target = branchTarget(info.op,
                                     static_cast<uint32_t>(info.pc),
                                     info.src1_val,
                                     info.imm);
    } else if (info.is_call) {
        actual_taken = true;
        actual_target = (info.imm != 0)
            ? static_cast<uint64_t>(info.imm)
            : static_cast<uint64_t>(info.src1_reg);
    } else if (info.is_ret) {
        // Architectural return from fetch-time RAS (not the prediction alone).
        if (info.ras_had_return) {
            actual_taken = true;
            actual_target = info.ras_ret_target;
        } else {
            actual_taken = false;
            actual_target = info.fallthrough_pc;
        }
    } else {
        actual_taken = (fu_value != 0);
        // Toy BEQ/BNE target is always imm (imm==0 is a legal absolute PC).
        actual_target = static_cast<uint64_t>(info.imm);
    }
    const uint64_t actual_next_pc = actual_taken ? actual_target : info.fallthrough_pc;

    info.br_resolved = true;
    info.br_taken = actual_taken;
    info.br_next_pc = actual_next_pc;

    const bool dir_wrong = (actual_taken != info.predicted_taken);
    const bool target_wrong = (actual_taken && info.predicted_target != actual_target);
    if (dir_wrong || target_wrong) {
        ++stats.branch_mispredicts;
        info.mispredicted = true;
        info.recovery_pc = actual_next_pc;
        // RTL early_recovery: redirect + selective squash; BP trains at commit.
        earlyRecover(info);
    }
}

void Processor::broadcastCDB(const CDBResult& result) {
    // Drop grants for tags no longer in flight (squashed / not yet reincarnated).
    InstInfo* info = findMutableByRobId(result.rob_id);
    if (info == nullptr) return;

    // ROB done is visible after this stage (rename later in the tick can see it).
    rob.writeResult(result.rob_id, result.value);
    // RS / operand wakeup is registered in RTL — apply at the start of next tick.
    cdb_wakeup_pending_.emplace_back(result.rob_id, result.value);

    if (isControl(*info)) {
        // Resolve / early recovery already ran on the BR resolve bus.
        // CDB here is only the JAL/JALR link value (RTL needs_link).
        info->completed = true;
    }
}

std::vector<int> Processor::storeCommitPermissions() const {
    // Mirror RTL rob commit_store_*: walk head..width; grant not-done stores
    // while older entries are done (or also stores awaiting permission).
    std::vector<int> tags;
    const int n = rob.activeCount();
    if (n == 0) return tags;

    const int head = rob.getHead();
    const int cap = rob.getCapacity();
    for (int lane = 0; lane < config.width && lane < n; ++lane) {
        const int tag = (head + lane) % cap;
        const InstInfo* info = findInstByRobId(tag);
        if (info == nullptr) break;
        if (rob.isReady(tag)) {
            if (isControl(*info) && info->mispredicted) break;
            continue;
        }
        if (info->is_store) {
            tags.push_back(tag);
        } else {
            break;
        }
    }
    return tags;
}

void Processor::processStoreBuffer() {
    const std::vector<int> perms = storeCommitPermissions();
    const std::vector<int> done_tags = lsq.enqueueStores(perms);
    for (int tag : done_tags) {
        rob.writeResult(tag, 0);
        InstInfo* info = findMutableByRobId(tag);
        if (info != nullptr) {
            info->completed = true;
        }
    }
}

void Processor::earlyRecover(InstInfo& br) {
    // Selective squash of everything younger than the resolving control op.
    ++stats.flushes;

    const int squash_tag = br.rob_id;
    std::vector<uint32_t> kill_ids;
    std::vector<int> kill_robs;

    for (const auto& kv : inst_table) {
        const InstInfo& info = kv.second;
        if (rob.isYounger(info.rob_id, squash_tag)) {
            kill_ids.push_back(info.inst_id);
            kill_robs.push_back(info.rob_id);
        }
    }
    stats.squashed += kill_ids.size();

    // Restore RAT from the post-rename checkpoint; scrub freed ROB tags (RTL).
    std::unordered_set<int> freed = freed_rob_tags_;
    for (int tag : kill_robs) freed.insert(tag);
    std::vector<int> restored;
    if (rat_ckpts.restore(squash_tag, restored)) {
        rat.restoreStateScrub(restored, freed);
    } else {
        rat.restoreStateScrub(br.rat_snapshot, freed);
        if (br.rd_used && br.dest_reg > 0) {
            rat.rename(br.dest_reg, br.rob_id);
        }
    }
    ras.restoreFull(br.ras_tos, br.ras_count, br.ras_stack);

    rs.squashInstIds(kill_ids);
    lsq.squashInstIds(kill_ids);
    ee.squashRobTags(kill_robs);
    cdb_arbiter.invalidateTags(kill_robs);
    // Drop deferred wakeups for squashed tags.
    {
        std::unordered_set<int> kill(kill_robs.begin(), kill_robs.end());
        std::vector<std::pair<int, int>> kept;
        kept.reserve(cdb_wakeup_pending_.size());
        for (const auto& wv : cdb_wakeup_pending_) {
            if (!kill.count(wv.first)) kept.push_back(wv);
        }
        cdb_wakeup_pending_.swap(kept);
    }

    for (int tag : kill_robs) {
        rat_ckpts.invalidate(tag);
    }

    rob.squashYoungerThan(squash_tag);

    for (uint32_t id : kill_ids) {
        inst_table.erase(id);
    }

    ifq.flush();
    dispatch_reg.flush();
    fetch_pc = br.recovery_pc;
    fetch_halted = false;
    imem_req_outstanding = false;
    ideal_fetch_countdown_ = 0;
    fetch_buf_ = FetchBuf{};
    fetch_req_pred_taken_.clear();
    fetch_req_pred_target_.clear();
    fetch_redirect_hold_ = true;  // no I$ request this cycle (RTL redirect if/else)
    if (auto* cached = dynamic_cast<CachedMemorySystem*>(memsys.get())) {
        cached->flushOnRedirect();  // I$ + cancel in-flight D$ loads
    }
}

FetchedInstruction Processor::decodeForDispatch(const FetchedInstruction& fetched) const {
    // Toy traces already carry opcode/regs; hex IFQ entries carry raw_instr + pred.
    if (!hex_mode || fetched.has_uop) {
        return fetched;
    }

    FetchedInstruction inst = fetched;
    Uop uop = decodeInstruction(fetched.raw_instr, static_cast<uint64_t>(fetched.pc));
    // Preserve fetch-time prediction onto the uop / rename view.
    uop.pred_taken = fetched.predicted_taken;
    uop.pred_target = fetched.predicted_target;

    inst.has_uop = true;
    inst.uop = uop;
    inst.opcode = opRobName(uop.op);
    inst.dest_reg = uop.rd_used ? uop.rd : 0;
    inst.src1_reg = uop.rs1;
    inst.src2_reg = uop.rs2;
    inst.imm = uop.imm;
    return inst;
}

bool Processor::tryRenameOne(const FetchedInstruction& inst) {
    // RTL lane_is_nop: UOP_NOP or ALU/MUL/DIV with !rd_used — accept, no allocate.
    if (inst.has_uop) {
        const Uop& u = inst.uop;
        const bool is_nop = (u.op == Op::UOP_NOP) ||
            (!u.rd_used && (u.fu == Fu::ALU || u.fu == Fu::MUL || u.fu == Fu::DIV));
        if (is_nop) {
            return true;
        }
    }

    const bool needs_mem = inst.has_uop
        ? (inst.uop.is_load || inst.uop.is_store)
        : isMemoryOp(inst.opcode);

    if (!rob.hasSpace()) {
        ++stats.stall_rob;
        return false;
    }
    if (needs_mem && !lsq.hasSpace()) {
        ++stats.stall_lsq;
        return false;
    }
    if (!needs_mem && !rs.hasSpace()) {
        ++stats.stall_rs;
        return false;
    }

    const int dest_for_rob = inst.has_uop
        ? (inst.uop.rd_used ? inst.uop.rd : 0)
        : inst.dest_reg;
    int rob_id = rob.dispatch(inst.opcode, dest_for_rob);
    if (rob_id < 0) {
        ++stats.stall_rob;
        return false;
    }

    InstInfo info{};
    info.inst_id = next_inst_id++;
    info.rob_id = rob_id;
    info.opcode = inst.opcode;
    info.pc = inst.pc;
    info.dest_reg = dest_for_rob;
    info.src1_reg = inst.src1_reg;
    info.src2_reg = inst.src2_reg;
    info.imm = inst.imm;
    info.predicted_taken = inst.predicted_taken;
    info.predicted_target = inst.predicted_target;
    info.fallthrough_pc = inst.pc + INSTR_SIZE;
    info.rat_snapshot = rat.getState();
    // Prefer fetch-time RAS checkpoint (toy); else live RAS (hex has no RAS fetch).
    if (!inst.ras_stack.empty() || inst.ras_had_return ||
        inst.ras_count != 0 || inst.ras_tos != 0) {
        info.ras_tos = inst.ras_tos;
        info.ras_count = inst.ras_count;
        info.ras_stack = inst.ras_stack;
        info.ras_had_return = inst.ras_had_return;
        info.ras_ret_target = inst.ras_ret_target;
    } else {
        ras.getFullSnapshot(info.ras_tos, info.ras_count, info.ras_stack);
    }

    if (inst.has_uop) {
        const Uop& u = inst.uop;
        info.has_uop = true;
        info.op = u.op;
        info.fu = u.fu;
        info.src2_is_imm = u.src2_is_imm;
        info.is_load = u.is_load;
        info.is_store = u.is_store;
        info.is_branch = u.is_branch;
        info.is_jump = u.is_jump;
        info.rd_used = u.rd_used;
        info.mem_size = u.mem_size;
        info.mem_unsigned = u.mem_unsigned;
        info.is_call = false;
        info.is_ret = false;
        info.is_addi = false;

        if (u.rs1_used) {
            resolveOperand(u.rs1, info.src1_ready, info.src1_tag, info.src1_val);
        } else {
            info.src1_ready = true;
            info.src1_tag = -1;
            info.src1_val = 0;
        }
        if (u.src2_is_imm) {
            info.src2_ready = true;
            info.src2_tag = -1;
            info.src2_val = u.imm;
        } else if (u.rs2_used && !u.is_store) {
            resolveOperand(u.rs2, info.src2_ready, info.src2_tag, info.src2_val);
        } else if (u.rs2_used && u.is_store) {
            info.src2_ready = true;
            info.src2_tag = -1;
            info.src2_val = 0;
        } else {
            info.src2_ready = true;
            info.src2_tag = -1;
            info.src2_val = 0;
        }
    } else {
        info.is_load = iequals(inst.opcode, "LOAD");
        info.is_store = iequals(inst.opcode, "STORE");
        info.is_branch = iequals(inst.opcode, "BEQ") || iequals(inst.opcode, "BNE");
        info.is_call = iequals(inst.opcode, "CALL");
        info.is_ret = iequals(inst.opcode, "RET");
        info.is_addi = iequals(inst.opcode, "ADDI");
        info.mem_size = MemSize::W;
        info.mem_unsigned = false;

        resolveOperand(inst.src1_reg, info.src1_ready, info.src1_tag, info.src1_val);
        if (info.is_addi) {
            info.src2_ready = true;
            info.src2_tag = -1;
            info.src2_val = info.imm;
        } else {
            resolveOperand(inst.src2_reg, info.src2_ready, info.src2_tag, info.src2_val);
        }
    }

    if (info.is_load || info.is_store) {
        if (!lsq.allocate(info.inst_id, rob_id, info.is_load,
                          info.mem_size, info.mem_unsigned)) {
            rob.undispatch(rob_id);
            return false;
        }
    } else {
        if (!rs.allocate(info.inst_id,
                          info.src1_ready, info.src1_tag, info.src1_val,
                          info.src2_ready, info.src2_tag, info.src2_val)) {
            rob.undispatch(rob_id);
            return false;
        }
    }

    if (info.has_uop) {
        if (info.rd_used) {
            rat.rename(info.dest_reg, rob_id);
        }
        if (info.is_store) {
            resolveOperand(info.src2_reg, info.store_data_ready,
                           info.store_data_tag, info.store_data_val);
            if (info.store_data_ready) {
                lsq.setStoreData(info.inst_id, info.store_data_val);
            }
        }
    } else {
        if (info.dest_reg > 0 && !info.is_store) {
            rat.rename(info.dest_reg, rob_id);
        }
        if (info.is_store && info.dest_reg > 0) {
            resolveOperand(info.dest_reg, info.store_data_ready,
                           info.store_data_tag, info.store_data_val);
            if (info.store_data_ready) {
                lsq.setStoreData(info.inst_id, info.store_data_val);
            }
        }
    }

    // Post-rename RAT checkpoint for control ops (RTL rat_checkpoints save).
    if (isControl(info)) {
        rat_ckpts.save(rob_id, rat.getState());
    }

    inst_table[info.inst_id] = info;
    ++stats.issued;
    return true;
}

// ---------------------------------------------------------------------------
// Stage 5 (last in tick): FETCH
// Reads WIDTH instructions from icache/imem, predicts branches, enqueues IFQ.
// Hex: IFQ holds raw_instr + prediction only; decode fills Uop later.
// ---------------------------------------------------------------------------
void Processor::fetchStage() {
    if (hex_mode && fetch_halted) {
        return;
    }

    // Cached hex fetch: RTL fetch.sv skid buffer + can_request=!buf_valid.
    // Sample can_request from *pre-update* buf (NBA-style): drain and request
    // are mutually exclusive in the same cycle.
    if (hex_mode && useCachedMem() && memsys) {
        const bool skip_req = fetch_redirect_hold_;
        fetch_redirect_hold_ = false;
        const bool buf_was_valid = fetch_buf_.valid;
        const bool can_request =
            !skip_req && !fetch_halted && !imem_req_outstanding && !buf_was_valid;

        // Capture I$ response into skid (PC advances here, not at IFQ push).
        // Only when buf empty at cycle start (cannot have outstanding+buf).
        if (imem_req_outstanding && !buf_was_valid) {
            if (auto resp = memsys->popImemResp()) {
                imem_req_outstanding = false;
                FetchBuf buf;
                buf.valid = true;
                buf.eop = false;
                buf.count = 0;
                buf.words.reserve(static_cast<size_t>(resp->count));
                buf.pcs.reserve(static_cast<size_t>(resp->count));
                buf.pred_taken.reserve(static_cast<size_t>(resp->count));
                buf.pred_target.reserve(static_cast<size_t>(resp->count));

                uint64_t next_pc = fetch_pc;
                bool taken = false;
                for (int i = 0; i < resp->count; ++i) {
                    const uint32_t word = resp->words[static_cast<size_t>(i)];
                    const uint64_t pc = resp->pc + static_cast<uint64_t>(4 * i);
                    if (word == INSTR_INVALID) {
                        buf.eop = true;
                        break;
                    }
                    const bool pt = (i < static_cast<int>(fetch_req_pred_taken_.size()))
                        ? fetch_req_pred_taken_[static_cast<size_t>(i)] : false;
                    const uint64_t ptarget =
                        (i < static_cast<int>(fetch_req_pred_target_.size()))
                        ? fetch_req_pred_target_[static_cast<size_t>(i)]
                        : (pc + INSTR_SIZE);
                    buf.words.push_back(word);
                    buf.pcs.push_back(pc);
                    buf.pred_taken.push_back(pt);
                    buf.pred_target.push_back(ptarget);
                    ++buf.count;
                    if (pt) {
                        next_pc = ptarget;
                        taken = true;
                        break;
                    }
                }
                if (!buf.eop && !taken) {
                    next_pc = resp->pc + static_cast<uint64_t>(4 * buf.count);
                }
                fetch_pc = next_pc;
                fetch_buf_ = std::move(buf);
                fetch_req_pred_taken_.clear();
                fetch_req_pred_target_.clear();
            }
        }

        // Drain skid → IFQ when there is room for the whole bundle (push_ready).
        if (buf_was_valid &&
            (fetch_buf_.count == 0 ||
             ifq.getOccupancy() + fetch_buf_.count <= config.ifq_size)) {
            for (int i = 0; i < fetch_buf_.count; ++i) {
                FetchedInstruction inst{};
                inst.pc = fetch_buf_.pcs[static_cast<size_t>(i)];
                inst.raw_instr = fetch_buf_.words[static_cast<size_t>(i)];
                inst.has_uop = false;
                inst.predicted_taken = fetch_buf_.pred_taken[static_cast<size_t>(i)];
                inst.predicted_target = fetch_buf_.pred_target[static_cast<size_t>(i)];
                if (!ifq.enqueue(inst)) break;
                ++stats.fetched;
            }
            const bool eop = fetch_buf_.eop;
            fetch_buf_ = FetchBuf{};
            if (eop) fetch_halted = true;
        }

        if (can_request) {
            fetch_req_pred_taken_.assign(static_cast<size_t>(config.width), false);
            fetch_req_pred_target_.assign(static_cast<size_t>(config.width), 0);
            for (int i = 0; i < config.width; ++i) {
                const uint64_t pc = fetch_pc + static_cast<uint64_t>(4 * i);
                fetch_req_pred_target_[static_cast<size_t>(i)] = pc + INSTR_SIZE;
                uint32_t word = 0;
                if (imemWord(pc, word) && word != INSTR_INVALID) {
                    const Uop peek = decodeInstruction(word, pc);
                    if (peek.is_branch || peek.is_jump) {
                        uint64_t target = 0;
                        const bool pt = predictor.predict(pc, target);
                        fetch_req_pred_taken_[static_cast<size_t>(i)] = pt;
                        fetch_req_pred_target_[static_cast<size_t>(i)] =
                            pt ? target : (pc + INSTR_SIZE);
                    }
                }
            }
            if (memsys->tryImemReq(static_cast<uint32_t>(fetch_pc))) {
                imem_req_outstanding = true;
            }
        }
        return;
    }

    // Ideal hex: 1-cycle registered imem (RTL ideal_imem_bridge).
    if (hex_mode && !useCachedMem()) {
        if (imem_req_outstanding && ideal_fetch_countdown_ == 0) {
            for (int i = 0; i < config.width; ++i) {
                const uint64_t pc = ideal_fetch_pc_ + static_cast<uint64_t>(4 * i);
                uint32_t word = 0;
                if (!imemWord(pc, word) || word == INSTR_INVALID) {
                    fetch_halted = true;
                    break;
                }
                bool redirected = false;
                if (!fetchEnqueueHexWord(word, pc, &redirected)) {
                    break;
                }
                if (redirected) break;
            }
            imem_req_outstanding = false;
        }
        // Only request a full WIDTH bundle when IFQ can absorb it (RTL push_ready).
        if (!imem_req_outstanding && !fetch_halted &&
            (ifq.getOccupancy() + config.width <= config.ifq_size)) {
            uint32_t word = 0;
            if (!imemWord(fetch_pc, word) || word == INSTR_INVALID) {
                fetch_halted = true;
            } else {
                imem_req_outstanding = true;
                ideal_fetch_pc_ = fetch_pc;
                ideal_fetch_countdown_ = 1;
            }
        }
        if (imem_req_outstanding && ideal_fetch_countdown_ > 0) {
            --ideal_fetch_countdown_;
        }
        return;
    }

    for (int i = 0; i < config.width; ++i) {
        if (ifq.isFull()) {
            break;
        }

        if (hex_mode) {
            // Cached path handled above; this is fallthrough for non-ideal hex.
            uint32_t word = 0;
            if (!imemWord(fetch_pc, word) || word == INSTR_INVALID) {
                fetch_halted = true;
                break;
            }
            bool redirected = false;
            if (!fetchEnqueueHexWord(word, fetch_pc, &redirected)) {
                break;
            }
            if (redirected) break;
            continue;
        }

        FetchedInstruction inst{};
        auto cache_it = icache.find(fetch_pc);
        if (cache_it == icache.end()) {
            break;
        }

        inst = cache_it->second;
        inst.predicted_taken = false;
        inst.predicted_target = fetch_pc + INSTR_SIZE;
        // Checkpoint RAS *before* speculative push/pop for this instruction.
        ras.getFullSnapshot(inst.ras_tos, inst.ras_count, inst.ras_stack);
        inst.ras_had_return = false;
        inst.ras_ret_target = 0;

        if (iequals(inst.opcode, "CALL")) {
            ras.push(fetch_pc + INSTR_SIZE);
            inst.predicted_taken = true;
            inst.predicted_target = (inst.imm != 0)
                ? static_cast<uint64_t>(inst.imm)
                : static_cast<uint64_t>(inst.src1_reg);
        } else if (iequals(inst.opcode, "RET")) {
            uint64_t ret_target = 0;
            if (ras.pop(ret_target)) {
                inst.ras_had_return = true;
                inst.ras_ret_target = ret_target;
                inst.predicted_taken = true;
                inst.predicted_target = ret_target;
            }
        } else if (isBranchOp(inst.opcode)) {
            uint64_t target = 0;
            inst.predicted_taken = predictor.predict(fetch_pc, target);
            inst.predicted_target = inst.predicted_taken ? target : (fetch_pc + INSTR_SIZE);
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
// DECODE: IFQ -> decoded bundle -> dispatch_reg (when it can capture).
// ---------------------------------------------------------------------------
void Processor::decodeStage() {
    if (!dispatch_reg.canCapture()) {
        return;
    }

    std::vector<FetchedInstruction> bundle;
    bundle.reserve(static_cast<size_t>(config.width));

    for (int i = 0; i < config.width; ++i) {
        FetchedInstruction fetched{};
        if (!ifq.peek(fetched)) {
            break;
        }
        ifq.dequeue(fetched);
        bundle.push_back(decodeForDispatch(fetched));
    }

    dispatch_reg.capture(bundle);
}

// ---------------------------------------------------------------------------
// ISSUE / RENAME: take a program-order prefix from dispatch_reg into ROB/RS/LSQ.
// ---------------------------------------------------------------------------
void Processor::issueStage() {
    int accept = 0;
    const int held = dispatch_reg.count();

    for (int i = 0; i < held; ++i) {
        if (!dispatch_reg.laneValid(i)) {
            break;
        }
        const FetchedInstruction& inst = dispatch_reg.lane(i);
        if (!tryRenameOne(inst)) {
            break;
        }
        ++accept;
    }

    if (accept > 0) {
        dispatch_reg.acceptPrefix(accept);
    }

    // Front-end starvation: rename saw nothing and upstream still has work.
    if (accept == 0 && held == 0) {
        bool upstream = !ifq.isEmpty();
        if (!upstream) {
            if (hex_mode) {
                uint32_t w = 0;
                upstream = !fetch_halted && imemWord(fetch_pc, w) && w != INSTR_INVALID;
            } else {
                upstream = icache.count(fetch_pc) != 0;
            }
        }
        if (upstream) {
            ++stats.stall_front;
        }
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
            info.address = static_cast<int>(static_cast<uint32_t>(info.src1_val) +
                                            static_cast<uint32_t>(info.imm));
            info.addr_computed = true;
            lsq.setAddress(info.inst_id, info.address);

            if (info.is_store && info.store_data_ready) {
                lsq.setStoreData(info.inst_id, info.store_data_val);
            }
        }

        // Stores complete only when enqueued into the committed SB (processStoreBuffer).
        if (info.is_store && info.addr_computed && info.store_data_ready) {
            lsq.setStoreData(info.inst_id, info.store_data_val);
        }
    }

    // RTL RS: issue per FU class (NUM_ALU / NUM_MUL / NUM_DIV / NUM_BR), not width.
    int issued_alu = 0, issued_mul = 0, issued_div = 0, issued_br = 0;
    const std::vector<uint32_t> ready_insts = rs.collectReadyInstructions();
    for (uint32_t ready_id : ready_insts) {
        InstInfo* info = findInstById(ready_id);
        if (info == nullptr || info->dispatched_alu) {
            rs.removeInstruction(ready_id);
            continue;
        }

        bool room = true;
        if (info->has_uop) {
            if (info->fu == Fu::MUL) room = issued_mul < config.num_mul;
            else if (info->fu == Fu::DIV) room = issued_div < config.num_div;
            else if (info->fu == Fu::BR) room = issued_br < config.num_br;
            else room = issued_alu < config.num_alu;
        } else {
            if (iequals(info->opcode, "MUL")) room = issued_mul < config.num_mul;
            else if (iequals(info->opcode, "DIV")) room = issued_div < config.num_div;
            else if (info->is_branch || info->is_call || info->is_ret)
                room = issued_br < config.num_br;
            else room = issued_alu < config.num_alu;
        }
        if (!room) continue;

        bool issued_ok = false;
        if (info->has_uop) {
            issued_ok = ee.issueInstruction(info->op, info->src1_val, info->src2_val,
                                            info->rob_id, info->imm,
                                            static_cast<uint32_t>(info->pc),
                                            info->rd_used);
        } else {
            const std::string ee_op = info->is_addi ? "ADD" : info->opcode;
            issued_ok = ee.issueInstruction(ee_op, info->src1_val, info->src2_val, info->rob_id);
        }
        if (issued_ok) {
            info->dispatched_alu = true;
            rs.removeInstruction(ready_id);
            if (info->has_uop) {
                if (info->fu == Fu::MUL) ++issued_mul;
                else if (info->fu == Fu::DIV) ++issued_div;
                else if (info->fu == Fu::BR) ++issued_br;
                else ++issued_alu;
            } else if (iequals(info->opcode, "MUL")) {
                ++issued_mul;
            } else if (iequals(info->opcode, "DIV")) {
                ++issued_div;
            } else if (info->is_branch || info->is_call || info->is_ret) {
                ++issued_br;
            } else {
                ++issued_alu;
            }
        }
    }

    // Age-ordered load dispatch up to WIDTH (oldest first).
    std::vector<InstInfo*> load_cands;
    for (auto& entry : inst_table) {
        InstInfo& info = entry.second;
        if (!info.is_load || info.dispatched_load || !info.addr_computed || info.completed) {
            continue;
        }
        load_cands.push_back(&info);
    }
    std::sort(load_cands.begin(), load_cands.end(),
              [](const InstInfo* a, const InstInfo* b) {
                  return a->inst_id < b->inst_id;
              });
    int loads_dispatched = 0;
    for (InstInfo* info : load_cands) {
        if (loads_dispatched >= config.width) break;
        if (lsq.dispatchLoad(info->inst_id)) {
            info->dispatched_load = true;
            ++loads_dispatched;
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 2: WRITEBACK
// BR resolve bus (independent of CDB), then CDB arbitrate/broadcast, then
// advance FU pipes. Ungranted CDB outs freeze (RTL in_ready = cdb_ready).
// ---------------------------------------------------------------------------
void Processor::writebackStage() {
    // Prior-cycle ST_CDB_WAIT → offerable before arbitration.
    lsq.promoteCdbWaits();

    if (useCachedMem() && memsys) {
        memsys->tick();  // pipe/hit_pend/DRAM → resp_q
        lsq.applyDataResps(memsys->popDataResps());  // → ST_CDB_WAIT (not same-cycle CDB)
        lsq.tick();  // fwd countdown + issue new D$ reqs into compare pipe
    } else {
        lsq.tick();
    }

    std::unordered_set<int> granted_ee_slots;

    // Resolve bus: oldest BR output first (single recovery if several mispredict).
    std::vector<BrResolve> resolves = ee.collectBranchResolves();
    std::sort(resolves.begin(), resolves.end(),
              [this](const BrResolve& a, const BrResolve& b) {
                  return rob.ageFromHead(a.rob_id) < rob.ageFromHead(b.rob_id);
              });
    for (const BrResolve& r : resolves) {
        InstInfo* info = findMutableByRobId(r.rob_id);
        if (info == nullptr) continue;

        const bool first = !info->br_resolved;
        if (first) {
            resolveBranch(*info, r.value);
        }

        // Non-link BR: ROB complete via sideband (RTL complete2), drain FU.
        if (!r.offer_cdb) {
            if (first || !info->completed) {
                rob.writeResult(r.rob_id, r.value);
                info->completed = true;
            }
            granted_ee_slots.insert(r.slot);
        }
    }

    // Fixed producer layout: [ALU…][MUL…][DIV…][BR…][LSQ…]
    std::vector<CdbProducer> producers = ee.collectProducersFixed();
    const std::vector<CdbProducer> lsq_prod = lsq.collectProducersFixed();
    producers.insert(producers.end(), lsq_prod.begin(), lsq_prod.end());

    std::vector<int> granted_idx;
    std::vector<CdbProducer> granted_lanes;
    std::vector<bool> producer_ready;
    const std::vector<CDBResult> grants =
        cdb_arbiter.selectGrants(producers, granted_idx, granted_lanes, producer_ready);

    // EE lane ready = arbiter producer_ready for FU lanes (RTL in_ready=cdb_ready).
    std::vector<bool> ee_ready(producer_ready.begin(),
                               producer_ready.begin() + ee.numUnits());
    ee.setLaneReady(ee_ready);

    for (size_t g = 0; g < grants.size(); ++g) {
        const CDBResult& result = grants[g];
        broadcastCDB(result);

        const InstInfo* info = findInstByRobId(result.rob_id);
        if (info != nullptr) {
            InstInfo* mutable_info = findInstById(info->inst_id);
            if (mutable_info != nullptr) {
                mutable_info->completed = true;
            }
        }

        // Release the granted lane payload (pending ⊕ live), not the live snapshot.
        assert(g < granted_lanes.size());
        const CdbProducer& lane = granted_lanes[g];
        if (lane.source == 0) {
            granted_ee_slots.insert(lane.slot);
        } else {
            lsq.releaseSlot(lane.slot);
        }
    }

    ee.applyCdbAndAdvance(granted_ee_slots);

    // Pending update uses post-consume producer snapshot.
    std::vector<CdbProducer> after = ee.collectProducersFixed();
    const std::vector<CdbProducer> lsq_after = lsq.collectProducersFixed();
    after.insert(after.end(), lsq_after.begin(), lsq_after.end());
    cdb_arbiter.commitPending(after, granted_idx);
}

bool Processor::fetchEnqueueHexWord(uint32_t word, uint64_t pc, bool* redirected) {
    if (redirected) *redirected = false;
    if (word == INSTR_INVALID) {
        fetch_halted = true;
        return false;
    }
    if (ifq.isFull()) {
        return false;
    }

    FetchedInstruction inst{};
    const Uop peek = decodeInstruction(word, pc);
    inst.pc = pc;
    inst.raw_instr = word;
    inst.has_uop = false;
    inst.predicted_taken = false;
    inst.predicted_target = pc + INSTR_SIZE;

    if (peek.is_branch || peek.is_jump) {
        // RTL: all control (incl. JAL) go through gshare+BTB; no hard-take.
        uint64_t target = 0;
        inst.predicted_taken = predictor.predict(pc, target);
        inst.predicted_target = inst.predicted_taken ? target : (pc + INSTR_SIZE);
    }

    if (!ifq.enqueue(inst)) {
        return false;
    }
    ++stats.fetched;
    if (inst.predicted_taken) {
        fetch_pc = inst.predicted_target;
        if (redirected) *redirected = true;
    } else {
        fetch_pc = pc + INSTR_SIZE;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Stage 1 (first in tick): COMMIT
// Store→SB complete, then in-order ROB commit / ARF writeback.
// ---------------------------------------------------------------------------
void Processor::commitStage() {
    // Stores become ROB-done when they enter the committed SB (not at mem write).
    processStoreBuffer();

    for (int i = 0; i < config.width; ++i) {
        int dest_reg = 0;
        int value = 0;
        int rob_id = -1;

        if (!rob.commit(dest_reg, value, rob_id)) {
            break;
        }
        freed_rob_tags_.insert(rob_id);

        const InstInfo* info = findInstByRobId(rob_id);
        if (info == nullptr) {
            continue;
        }

        if (info->is_store) {
            // Memory write is asynchronous via SB; LSQ entry already freed on enqueue.
            ++stats.committed_store;
        } else if (info->is_load) {
            lsq.freeLoad(info->inst_id);
            if (dest_reg > 0 && dest_reg < config.num_regs) {
                arf[dest_reg] = value;
                rat.commit(dest_reg, rob_id);
                rat_ckpts.onCommitClear(dest_reg, rob_id);
            }
            ++stats.committed_load;
        } else {
            if (dest_reg > 0 && dest_reg < config.num_regs) {
                arf[dest_reg] = value;
                rat.commit(dest_reg, rob_id);
                rat_ckpts.onCommitClear(dest_reg, rob_id);
            }
            if (isControl(*info)) {
                ++stats.committed_branch;
                if (info->br_resolved) {
                    predictor.update(info->pc, info->br_taken, info->br_next_pc);
                }
                rat_ckpts.invalidate(rob_id);
            } else if (info->has_uop) {
                if (info->fu == Fu::MUL) ++stats.committed_mul;
                else if (info->fu == Fu::DIV) ++stats.committed_div;
                else ++stats.committed_alu;
            } else if (iequals(info->opcode, "MUL")) {
                ++stats.committed_mul;
            } else if (iequals(info->opcode, "DIV")) {
                ++stats.committed_div;
            } else {
                ++stats.committed_alu;
            }
        }

        ++stats.committed;
        inst_table.erase(info->inst_id);
    }
}

void Processor::checkInvariants() const {
    // ROB self-consistency (active_count matches busy slots; head/tail valid).
    rob.checkInvariant();

    // Every in-flight instruction has a valid, unique ROB id, and there cannot be
    // more in-flight instructions than ROB slots.
    assert(static_cast<int>(inst_table.size()) <= rob.activeCount());
    std::unordered_set<int> seen_rob_ids;
    for (const auto& kv : inst_table) {
        const InstInfo& info = kv.second;
        assert(info.rob_id >= 0 && info.rob_id < config.rob_size && "InstInfo rob_id out of range");
        assert(seen_rob_ids.insert(info.rob_id).second && "two in-flight instructions share a ROB id");
        // Register fields must stay within the architected file.
        assert(info.dest_reg < config.num_regs);
        assert(info.src1_reg < config.num_regs && info.src2_reg < config.num_regs);
    }

    // Recovery flags are mutually consistent with cycle bookkeeping.
    assert(cycle_count <= static_cast<uint64_t>(config.max_cycles));
}

void Processor::tick() {
    ++cycle_count;
    stats.cycles = cycle_count;

    // Apply registered CDB wakeups from prior cycle (RTL RS always_ff).
    for (const auto& wv : cdb_wakeup_pending_) {
        rs.updateFromCDB(wv.first, wv.second);
        updateInstOperandsFromCDB(wv.first, wv.second);
    }
    cdb_wakeup_pending_.clear();
    freed_rob_tags_.clear();

    // Reverse latch order: rename pops dispatch_reg, then decode may capture,
    // then fetch fills IFQ — matching RTL pipeline-register timing.
    commitStage();
    writebackStage();
    dispatchStage();
    // Same-cycle mem issue for loads dispatched this cycle (RTL LSQ comb issue).
    if (useCachedMem()) {
        lsq.issueMemoryRequests();
    }
    issueStage();
    decodeStage();
    fetchStage();

    if (self_check) {
        checkInvariants();
    }
}

bool Processor::isFinished() const {
    if (cycle_count >= static_cast<uint64_t>(config.max_cycles)) {
        return true;
    }

    bool fetch_done = false;
    if (hex_mode) {
        // Halt only after eop drained through the skid (not merely PC off image).
        fetch_done = fetch_halted && !imem_req_outstanding && !fetch_buf_.valid;
    } else {
        fetch_done = icache.find(fetch_pc) == icache.end();
    }
    return fetch_done && !fetch_buf_.valid && ifq.isEmpty() && dispatch_reg.empty() &&
           inst_table.empty() && rob.isEmpty() && lsq.isDrained();
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
              << " BR=" << config.num_br << "(" << config.br_latency << "c)"
              << " lsqCdb=" << config.num_lsq
              << " SB=" << config.store_buf_depth
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
              << "branches,mispredicts,mispred_rate,flushes,squashed,"
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
              << s.flushes << ',' << s.squashed << ','
              << s.stall_rob << ',' << s.stall_rs << ',' << s.stall_lsq << ',' << s.stall_front << ','
              << (hit_cap ? 1 : 0) << '\n';
}

void Processor::printArchState() {
    if (auto* cached = dynamic_cast<CachedMemorySystem*>(memsys.get())) {
        cached->writebackDirty();
    }

    std::cout << "\n--------- Final Architectural State ---------\n";
    std::cout << "Registers (nonzero):\n";
    bool any = false;
    for (int r = 0; r < config.num_regs; ++r) {
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
            // Always decimal index: word index (toy) or byte address (hex).
            const size_t idx = MainMemoryByteAddressed ? (a * 4) : a;
            std::cout << "  MEM[" << idx << "] = " << MainMemory[a] << "\n";
            any = true;
        }
    }
    if (!any) std::cout << "  (all zero)\n";
    std::cout << "--------------------------------------------\n";
}

void Processor::printRegsDump() const {
    // Machine-readable dump for golden co-check (one register per line).
    for (int r = 0; r < config.num_regs; ++r) {
        const uint32_t v = static_cast<uint32_t>(arf[r]);
        std::cout << "x" << r << "=0x" << std::hex << v << std::dec << "\n";
    }
}

void Processor::printState() const {
    std::cout << "\n=== Cycle " << cycle_count << " | PC=0x" << std::hex << fetch_pc << std::dec << " ===\n";
    ifq.printState();
    dispatch_reg.printState();
    rob.printState();
    rs.printState();
    lsq.printState();
    ee.printState();
    rat.printState();
    ras.printState();
}
