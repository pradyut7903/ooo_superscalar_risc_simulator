#pragma once

#include "cdb_arbiter.hpp"
#include "types.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

// RV32IM execution semantics for non-memory ops (mirrors alu/mul/div/branch_unit).
int32_t executeOp(Op op, int32_t src1, int32_t src2, int32_t imm = 0, uint32_t pc = 0);

bool branchTaken(Op op, int32_t src1, int32_t src2);
uint32_t branchTarget(Op op, uint32_t pc, int32_t src1, int32_t imm);

// Ready BR/jump at FU output for the resolve sideband (not gated on CDB).
struct BrResolve {
    int slot = -1;
    int rob_id = -1;
    int value = 0;       // link value, or toy taken bit
    bool offer_cdb = false;  // JAL/JALR with rd_used only (RTL needs_link)
};

// One FU with an RTL-style pipeline: stage[0] is after issue, stage[N-1] is CDB.
// The pipe freezes when the output stage is valid and was not released
// (CDB grant, or non-link BR resolve drain).
class ExecutionUnit {
public:
    struct Stage {
        bool valid = false;
        int rob_id = -1;
        int value = 0;
        bool is_control = false;
        bool offer_cdb = true;  // false ⇒ resolve-bus complete, no CDB producer
    };

    std::string type;
    int latency = 1;  // number of pipe stages (>= 1)
    std::vector<Stage> pipe;

    ExecutionUnit(std::string t, int l);

    // lane_ready: arbiter cdb_ready for this unit (RTL in_ready = cdb_ready).
    bool canIssue(bool lane_ready) const;
    bool issue(std::string op, int val1, int val2, int r_id);
    bool issue(Op op, int val1, int val2, int r_id, int32_t imm = 0, uint32_t pc = 0,
               bool rd_used = true);

    bool outputValid() const;
    int outputRobId() const;
    int outputValue() const;
    bool outputOfferCdb() const;
    bool outputIsControl() const;

    void consumeOutput();
    void advance(bool output_granted);

    void clear();
    bool anyValid() const;
};

class ExecutionEngine {
private:
    std::vector<ExecutionUnit> units;
    std::vector<bool> lane_ready_;  // per-unit cdb_ready from last arbiter cycle

public:
    ExecutionEngine(int n_alu, int n_mul, int n_div, int n_br,
                    int alu_lat, int mul_lat, int div_lat, int br_lat = 1);

    int numUnits() const { return static_cast<int>(units.size()); }

    bool issueInstruction(std::string op, int val1, int val2, int rob_id);
    bool issueInstruction(Op op, int val1, int val2, int rob_id,
                          int32_t imm = 0, uint32_t pc = 0, bool rd_used = true);

    // Fixed layout: one lane per FU unit (ALU…MUL…DIV…BR…). Idle ⇒ valid=false.
    std::vector<CdbProducer> collectProducersFixed() const;

    // Compact list of currently offer_cdb outputs (tests / debug).
    std::vector<CdbProducer> collectProducers() const;

    std::vector<BrResolve> collectBranchResolves() const;

    void setLaneReady(const std::vector<bool>& ready);
    void applyCdbAndAdvance(const std::unordered_set<int>& granted_slots);

    void tick();

    void releaseSlot(int unit_index);
    void squashRobTags(const std::vector<int>& rob_tags);
    void printState() const;
};
