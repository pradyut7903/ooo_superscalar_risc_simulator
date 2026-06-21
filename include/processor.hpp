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

// Superscalar processor configuration
struct ProcessorConfig {
    static constexpr int WIDTH = 4;

    int num_regs = 16;
    int rob_size = 32;
    int rs_size = 32;
    int lsq_size = 32;
    int ifq_size = 32;
    int cdb_width = WIDTH;
    int ras_size = 16;
    int pht_size = 16;
    int btb_size = 16;
    uint64_t initial_pc = 0x1000;
    int max_cycles = 10000;
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

    void printState() const;

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
    uint64_t flush_pc;
    std::vector<int> flush_rat_snapshot;
    int flush_ras_tos;
    int flush_ras_count;
    uint32_t flush_inst_id;

    bool mov_pending;

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
};
