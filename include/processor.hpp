#pragma once

#include "cdb_arbiter.hpp"
#include "dispatch_reg.hpp"
#include "execution_engine.hpp"
#include "gshare_predictor.hpp"
#include "instruction_fetch_queue.hpp"
#include "load_store_queue.hpp"
#include "mem/memory_system.hpp"
#include "rat_checkpoints.hpp"
#include "register_alias_table.hpp"
#include "reorder_buffer.hpp"
#include "reservation_station.hpp"
#include "return_address_stack.hpp"
#include "types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Superscalar processor configuration. Every field is a runtime knob so that
// sweep experiments can vary it from the command line without recompiling.
// Knobs marked "RTL / unused" mirror github_ooo_rv32im pkg_cpu defaults but are
// not wired into the cycle model yet (Stage 1) — changing them must not alter
// timing until a later stage consumes them.
struct ProcessorConfig {
    int width = 4;             // fetch / issue / commit (RTL WIDTH)

    // Defaults match ooo_rtl/rtl_v2 pkg_cpu.sv (+ hex forces num_regs=32, pc=0).
    int num_regs = 32;
    int rob_size = 32;
    int rs_size = 32;
    int lsq_size = 32;
    int ifq_size = 32;
    int cdb_width = 4;
    int ras_size = 16;
    int pht_size = 1024;
    int btb_size = 256;

    // Execution resources
    int num_alu = 2;
    int num_mul = 1;
    int num_div = 1;
    int alu_latency = 1;
    int mul_latency = 3;
    int div_latency = 10;

    // Ideal-path load / forward latencies (RTL dmem ≈ 1-cycle)
    int mem_latency = 1;
    int fwd_latency = 1;
    bool store_forwarding = true;

    uint64_t initial_pc = 0;
    int max_cycles = 100000;

    int num_br = 1;
    int br_latency = 1;
    int num_lsq = 2;

    int store_buf_depth = 8;

    // RTL default MEM_SYSTEM_CACHED
    MemSystem mem_system = MemSystem::Cached;

    // Cache / DRAM knobs (Stage 8)
    int cache_line_bytes = 32;
    int dcache_sets = 16;
    int dcache_ways = 4;
    int icache_sets = 16;
    int icache_ways = 4;
    int dcache_mshr = 4;
    int icache_mshr = 2;
    int dcache_ufp_ports = 2;
    int mshr_waiters = 4;      // CPU ops queued per D$ MSHR (RTL MSHR_WAITERS)
    int load_hit_latency = 1;  // D$ load-hit delay (RTL hit_pend ≈ 1)
    int fetch_hit_latency = 1; // I$ hit delay (RTL hit_pend ≈ 1)
    int dram_outstanding = 4;
    int dram_lat_cycles = 10;
    int dram_model = 0;        // 0 = simple, 1 = banked (matches pkg_cpu)
};

// Aggregate performance counters collected over a run.
struct ProcessorStats {
    uint64_t cycles = 0;
    uint64_t fetched = 0;             // instructions enqueued into the IFQ
    uint64_t issued = 0;             // instructions dispatched into the ROB
    uint64_t committed = 0;          // instructions retired (useful work)

    uint64_t committed_alu = 0;
    uint64_t committed_mul = 0;
    uint64_t committed_div = 0;
    uint64_t committed_load = 0;
    uint64_t committed_store = 0;
    uint64_t committed_branch = 0;

    uint64_t branches_resolved = 0;
    uint64_t branch_mispredicts = 0;
    uint64_t flushes = 0;
    uint64_t squashed = 0;           // in-flight instructions discarded by flushes

    // Per-cycle stall accounting: why issue could not make full progress.
    uint64_t stall_front = 0;        // IFQ empty (front-end starvation)
    uint64_t stall_rob = 0;          // ROB full
    uint64_t stall_rs = 0;           // RS full
    uint64_t stall_lsq = 0;          // LSQ full
};

// Per-instruction metadata tracked by the Processor (not stored in ROB/RS)
struct InstInfo {
    uint32_t inst_id = 0;
    int rob_id = -1;
    std::string opcode;
    uint64_t pc = 0;

    int dest_reg = -1;
    int src1_reg = -1;
    int src2_reg = -1;
    int imm = 0;

    bool is_load = false;
    bool is_store = false;
    bool is_branch = false;
    bool is_call = false;
    bool is_ret = false;
    bool is_addi = false;
    MemSize mem_size = MemSize::W;
    bool mem_unsigned = false;

    // RV32 hex path
    bool has_uop = false;
    Op op = Op::UOP_NOP;
    Fu fu = Fu::ALU;
    bool src2_is_imm = false;
    bool is_jump = false;
    bool rd_used = false;

    bool src1_ready = false;
    int src1_tag = -1;
    int src1_val = 0;

    bool src2_ready = false;
    int src2_tag = -1;
    int src2_val = 0;

    bool store_data_ready = false;
    int store_data_tag = -1;
    int store_data_val = 0;

    bool addr_computed = false;
    int address = 0;

    bool dispatched_alu = false;
    bool dispatched_load = false;

    bool predicted_taken = false;
    uint64_t predicted_target = 0;
    uint64_t fallthrough_pc = 0;

    bool completed = false;

    // Set at execute-time resolve. Early recovery runs immediately on mispredict;
    // predictor is trained later at commit from br_* fields.
    bool mispredicted = false;
    uint64_t recovery_pc = 0;
    bool br_resolved = false;
    bool br_taken = false;
    uint64_t br_next_pc = 0;

    std::vector<int> rat_snapshot;  // pre-rename fallback if RAT checkpoint miss
    int ras_tos = 0;
    int ras_count = 0;
    std::vector<uint64_t> ras_stack;
    bool ras_had_return = false;
    uint64_t ras_ret_target = 0;
};

class Processor {
public:
    explicit Processor(const ProcessorConfig& cfg = ProcessorConfig());

    void loadInstructionCache(const std::string& trace_path);
    void loadInstructionCacheFromVector(const std::vector<FetchedInstruction>& program);
    // Load RV32IM imem.hex (+ optional dmem.hex). Forces 32 regs, PC=0, byte mem.
    void loadHexProgram(const std::string& imem_path, const std::string& dmem_path = "");

    void tick();
    void run();

    bool isFinished() const;
    uint64_t getCycleCount() const { return cycle_count; }

    const ProcessorStats& getStats() const { return stats; }
    const ProcessorConfig& getConfig() const { return config; }
    const std::vector<int>& getArf() const { return arf; }
    bool isHexMode() const { return hex_mode; }

    // When enabled, assert structural invariants at the end of every tick (slow;
    // used by the verification harness, off for performance sweeps).
    void enableSelfCheck(bool b) { self_check = b; }

    void printState() const;
    void printArchState();                         // final ARF + nonzero memory (for validation)
    void printRegsDump() const;                    // x0..xN one-per-line for golden compare
    void printStats() const;                       // human-readable summary
    void printStatsCSV(const std::string& label) const;  // one CSV data row
    static void printStatsCSVHeader();             // matching CSV header row

private:
    ProcessorConfig config;

    InstructionFetchQueue ifq;
    DispatchReg dispatch_reg;
    RegisterAliasTable rat;
    RatCheckpoints rat_ckpts;
    ReservationStation rs;
    ReorderBuffer rob;
    LoadStoreQueue lsq;
    ExecutionEngine ee;
    CdbArbiter cdb_arbiter;
    GSharePredictor predictor;
    ReturnAddressStack ras;
    std::unique_ptr<MemorySystem> memsys;

    std::vector<int> arf;
    std::unordered_map<uint64_t, FetchedInstruction> icache;

    // Hex / imem path (Stage 3)
    bool hex_mode = false;
    bool fetch_halted = false;
    bool imem_req_outstanding = false;
    // Ideal imem: 1-cycle registered response (RTL ideal_imem_bridge).
    int ideal_fetch_countdown_ = 0;
    uint64_t ideal_fetch_pc_ = 0;
    std::vector<uint32_t> imem;

    // Cached fetch skid buffer (RTL fetch.sv buf_*): 1 bundle, can_request=!buf.
    struct FetchBuf {
        bool valid = false;
        bool eop = false;
        int count = 0;
        std::vector<uint32_t> words;
        std::vector<uint64_t> pcs;
        std::vector<bool> pred_taken;
        std::vector<uint64_t> pred_target;
    };
    FetchBuf fetch_buf_{};
    // Predictions captured at I$ request (RTL req_pred_*).
    std::vector<bool> fetch_req_pred_taken_;
    std::vector<uint64_t> fetch_req_pred_target_;
    // No I$ request the cycle of earlyRecover (RTL redirect if/else).
    bool fetch_redirect_hold_ = false;

    // Freed ROB tags this cycle (commit + squash) for RAT restore scrub.
    std::unordered_set<int> freed_rob_tags_;

    // RTL RS: CDB wakeup is registered — apply to RS/operands next tick.
    std::vector<std::pair<int, int>> cdb_wakeup_pending_;

    uint64_t fetch_pc;
    uint64_t cycle_count;
    uint32_t next_inst_id;

    std::unordered_map<uint32_t, InstInfo> inst_table;

    bool self_check = false;

    ProcessorStats stats;

    void resolveOperand(int logical_reg, bool& ready, int& tag, int& val);
    void broadcastCDB(const CDBResult& result);
    void updateInstOperandsFromCDB(int rob_tag, int value);
    // RTL resolve bus: taken/target/mispredict + earlyRecover (not gated on CDB).
    void resolveBranch(InstInfo& info, int fu_value);

    void commitStage();
    void writebackStage();
    void dispatchStage();
    void issueStage();     // rename: accept prefix from dispatch_reg
    void decodeStage();    // IFQ -> decode -> capture into dispatch_reg
    void fetchStage();     // imem/icache -> IFQ (raw + prediction)

    void earlyRecover(InstInfo& br);  // execute-time selective squash + redirect
    void processStoreBuffer();        // ROB store permissions → SB enqueue
    std::vector<int> storeCommitPermissions() const;
    void checkInvariants() const;   // structural assertions (self-check mode)

    bool isMemoryOp(const std::string& op) const;
    bool isBranchOp(const std::string& op) const;
    bool isControl(const InstInfo& info) const;
    bool imemWord(uint64_t pc, uint32_t& out_word) const;
    bool useCachedMem() const;
    // Enqueue one hex word into IFQ with prediction; advances/redirects fetch_pc.
    // Returns false if IFQ full or halt word. Sets *redirected if fetch_pc jumped.
    bool fetchEnqueueHexWord(uint32_t word, uint64_t pc, bool* redirected = nullptr);
    FetchedInstruction decodeForDispatch(const FetchedInstruction& fetched) const;
    bool tryRenameOne(const FetchedInstruction& inst);

    InstInfo* findInstById(uint32_t inst_id);
    const InstInfo* findInstByRobId(int rob_id) const;
    InstInfo* findMutableByRobId(int rob_id);
};
