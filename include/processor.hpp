#pragma once

#include "execution_engine.hpp"
#include "gshare_predictor.hpp"
#include "instruction_fetch_queue.hpp"
#include "load_store_queue.hpp"
#include "register_alias_table.hpp"
#include "reorder_buffer.hpp"
#include "reservation_station.hpp"
#include "return_address_stack.hpp"
#include "types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Superscalar processor configuration. Every field is a runtime knob so that
// sweep experiments can vary it from the command line without recompiling.
struct ProcessorConfig {
    int width = 4;             // fetch / issue / commit instructions per cycle

    int num_regs = 16;
    int rob_size = 32;
    int rs_size = 32;
    int lsq_size = 32;
    int ifq_size = 32;
    int cdb_width = 4;         // Common Data Bus write ports (defaults to width)
    int ras_size = 16;
    int pht_size = 1024;       // generous predictor by default so branch aliasing
    int btb_size = 256;        // does not contaminate the non-branch studies

    // Execution resources (functional-unit mix and latencies)
    int num_alu = 2;
    int num_mul = 1;
    int num_div = 1;
    int alu_latency = 1;
    int mul_latency = 3;
    int div_latency = 10;

    // Memory system
    int mem_latency = 5;       // load latency on a store-forwarding miss
    int fwd_latency = 1;       // store-to-load forwarding latency
    bool store_forwarding = true;

    uint64_t initial_pc = 0x1000;
    int max_cycles = 100000;
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
    uint64_t mem_order_violations = 0;

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

    // Branch misprediction is recorded here when the branch resolves, then acted
    // on when the branch reaches the ROB head (so only younger work is squashed).
    bool mispredicted = false;
    uint64_t recovery_pc = 0;

    std::vector<int> rat_snapshot;
    int ras_tos = 0;
    int ras_count = 0;
};

class Processor {
public:
    explicit Processor(const ProcessorConfig& cfg = ProcessorConfig());

    void loadInstructionCache(const std::string& trace_path);
    void loadInstructionCacheFromVector(const std::vector<FetchedInstruction>& program);

    void tick();
    void run();

    bool isFinished() const;
    uint64_t getCycleCount() const { return cycle_count; }

    const ProcessorStats& getStats() const { return stats; }
    const ProcessorConfig& getConfig() const { return config; }

    void printState() const;
    void printArchState() const;                   // final ARF + nonzero memory (for validation)
    void printStats() const;                       // human-readable summary
    void printStatsCSV(const std::string& label) const;  // one CSV data row
    static void printStatsCSVHeader();             // matching CSV header row

private:
    ProcessorConfig config;

    InstructionFetchQueue ifq;
    RegisterAliasTable rat;
    ReservationStation rs;
    ReorderBuffer rob;
    LoadStoreQueue lsq;
    ExecutionEngine ee;
    GSharePredictor predictor;
    ReturnAddressStack ras;

    std::vector<int> arf;
    std::unordered_map<uint64_t, FetchedInstruction> icache;

    uint64_t fetch_pc;
    uint64_t cycle_count;
    uint32_t next_inst_id;

    std::unordered_map<uint32_t, InstInfo> inst_table;

    bool flush_pending;
    bool mispredict_outstanding;   // a branch has resolved mispredicted but not yet committed
    uint64_t flush_pc;
    std::vector<int> flush_rat_snapshot;
    int flush_ras_tos;
    int flush_ras_count;
    uint32_t flush_inst_id;

    bool mov_pending;

    ProcessorStats stats;

    std::unordered_set<uint64_t> conservative_load_pcs;

    bool canDispatchLoadConservative(const InstInfo& load) const;

    void resolveOperand(int logical_reg, bool& ready, int& tag, int& val);
    void broadcastCDB(const CDBResult& result);
    void updateInstOperandsFromCDB(int rob_tag, int value);

    void commitStage();
    void writebackStage();
    void dispatchStage();
    void issueStage();
    void fetchStage();

    void performFlush();
    void rebuildPipelineState();

    bool isMemoryOp(const std::string& op) const;
    bool isBranchOp(const std::string& op) const;

    InstInfo* findInstById(uint32_t inst_id);
    const InstInfo* findInstByRobId(int rob_id) const;
    InstInfo* findMutableByRobId(int rob_id);
};
