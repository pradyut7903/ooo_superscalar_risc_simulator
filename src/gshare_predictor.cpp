#include "gshare_predictor.hpp"

#include <bitset>
#include <iostream>

namespace {

int clog2_pow2(int n) {
    int w = 0;
    int v = n;
    while (v > 1) {
        v >>= 1;
        ++w;
    }
    return w;
}

}  // namespace

GSharePredictor::GSharePredictor(int p_size, int b_size)
    : pht_size(p_size), btb_size(b_size), ghr(0) {
    // RTL: PHT init 2'b01, GHR 0.
    pht.assign(static_cast<size_t>(pht_size), 1);
    btb.assign(static_cast<size_t>(btb_size), BTBEntry{});
    ghr_w = clog2_pow2(pht_size);
    btb_idx_w = clog2_pow2(btb_size);
    ghr_mask = (ghr_w >= 32) ? 0xFFFFFFFFu : ((1u << ghr_w) - 1u);
    btb_idx_mask = (btb_idx_w >= 32) ? 0xFFFFFFFFu : ((1u << btb_idx_w) - 1u);
}

int GSharePredictor::phtIndex(uint64_t pc, uint32_t hist) const {
    // RTL: pc[GHR_W+1:2] ^ hist
    const uint32_t pc_bits = static_cast<uint32_t>((pc >> 2) & ghr_mask);
    return static_cast<int>(pc_bits ^ (hist & ghr_mask));
}

int GSharePredictor::btbIndex(uint64_t pc) const {
    // RTL: pc[BTB_IDX_W+1:2]
    return static_cast<int>((pc >> 2) & btb_idx_mask);
}

uint32_t GSharePredictor::btbTag(uint64_t pc) const {
    // RTL: pc[PC_W-1 : BTB_IDX_W+2]
    return static_cast<uint32_t>(pc >> (btb_idx_w + 2));
}

bool GSharePredictor::predict(uint64_t pc, uint64_t& out_target) {
    const int pi = phtIndex(pc, ghr);
    const bool pht_taken = (pht[static_cast<size_t>(pi)] >= 2);

    const int bi = btbIndex(pc);
    const uint32_t tag = btbTag(pc);
    const bool btb_hit = btb[static_cast<size_t>(bi)].valid &&
                         btb[static_cast<size_t>(bi)].tag == tag;

    const bool predicted_taken = pht_taken && btb_hit;
    out_target = predicted_taken ? btb[static_cast<size_t>(bi)].target_addr
                                 : (pc + 4);
    return predicted_taken;
}

void GSharePredictor::update(uint64_t pc, bool actual_taken, uint64_t actual_target) {
    const int pi = phtIndex(pc, ghr);
    if (actual_taken) {
        if (pht[static_cast<size_t>(pi)] < 3) {
            pht[static_cast<size_t>(pi)]++;
        }
    } else {
        if (pht[static_cast<size_t>(pi)] > 0) {
            pht[static_cast<size_t>(pi)]--;
        }
    }

    if (actual_taken) {
        const int bi = btbIndex(pc);
        btb[static_cast<size_t>(bi)].valid = true;
        btb[static_cast<size_t>(bi)].tag = btbTag(pc);
        btb[static_cast<size_t>(bi)].target_addr = actual_target;
    }

    ghr = ((ghr << 1) | (actual_taken ? 1u : 0u)) & ghr_mask;
}

void GSharePredictor::printState(uint64_t pc) {
    const int pht_index = phtIndex(pc, ghr);
    std::cout << "GHR: " << std::bitset<10>(ghr)
              << " | PHT idx: " << pht_index
              << " | Counter: " << pht[static_cast<size_t>(pht_index)] << "\n";
}
