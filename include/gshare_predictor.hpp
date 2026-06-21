#pragma once

#include <cstdint>
#include <vector>

struct BTBEntry {
    bool valid;
    uint64_t pc_tag;
    uint64_t target_addr;
};

class GSharePredictor {
private:
    std::vector<int> pht;
    std::vector<BTBEntry> btb;

    uint32_t ghr;

    int pht_size;
    int btb_size;
    uint32_t ghr_mask;

public:
    GSharePredictor(int p_size, int b_size);

    bool predict(uint64_t pc, uint64_t& out_target);
    void update(uint64_t pc, bool actual_taken, uint64_t actual_target);
    void printState(uint64_t pc);
};
