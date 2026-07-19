#pragma once

#include <cstdint>
#include <vector>

struct BTBEntry {
    bool valid = false;
    uint32_t tag = 0;       // pc[31 : BTB_IDX_W+2]
    uint64_t target_addr = 0;
};

// GShare + BTB matching ooo_rtl/rtl_v2/branch_predictor.sv indexing.
class GSharePredictor {
private:
    std::vector<int> pht;
    std::vector<BTBEntry> btb;

    uint32_t ghr = 0;
    int pht_size = 0;
    int btb_size = 0;
    int ghr_w = 0;       // clog2(PHT_SIZE)
    int btb_idx_w = 0;   // clog2(BTB_SIZE)
    uint32_t ghr_mask = 0;
    uint32_t btb_idx_mask = 0;

    int phtIndex(uint64_t pc, uint32_t hist) const;
    int btbIndex(uint64_t pc) const;
    uint32_t btbTag(uint64_t pc) const;

public:
    GSharePredictor(int p_size, int b_size);

    bool predict(uint64_t pc, uint64_t& out_target);
    void update(uint64_t pc, bool actual_taken, uint64_t actual_target);
    void printState(uint64_t pc);
};
