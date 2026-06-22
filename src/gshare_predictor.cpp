#include "gshare_predictor.hpp"

#include <bitset>
#include <iostream>

GSharePredictor::GSharePredictor(int p_size, int b_size)
    : pht_size(p_size), btb_size(b_size), ghr(0) {
    pht.resize(pht_size, 1);

    btb.resize(btb_size);
    for (int i = 0; i < btb_size; i++) {
        btb[i].valid = false;
    }

    ghr_mask = static_cast<uint32_t>(pht_size - 1);
}

bool GSharePredictor::predict(uint64_t pc, uint64_t& out_target) {
    int pht_index = static_cast<int>((pc ^ ghr) & ghr_mask);

    int counter = pht[pht_index];
    bool predicted_taken = (counter >= 2);

    int btb_index = static_cast<int>(pc % btb_size);
    out_target = 0;

    if (btb[btb_index].valid && btb[btb_index].pc_tag == pc) {
        out_target = btb[btb_index].target_addr;
    } else {
        predicted_taken = false;
    }

    return predicted_taken;
}

void GSharePredictor::update(uint64_t pc, bool actual_taken, uint64_t actual_target) {
    int pht_index = static_cast<int>((pc ^ ghr) & ghr_mask);

    if (actual_taken) {
        if (pht[pht_index] < 3) {
            pht[pht_index]++;
        }
    } else {
        if (pht[pht_index] > 0) {
            pht[pht_index]--;
        }
    }

    // A BTB target is only meaningful for taken branches. Writing the fall-through
    // PC on a not-taken branch pollutes the entry, so that a later taken prediction
    // jumps to the wrong (fall-through) target. Only update the BTB on taken.
    if (actual_taken) {
        int btb_index = static_cast<int>(pc % btb_size);
        btb[btb_index].valid = true;
        btb[btb_index].pc_tag = pc;
        btb[btb_index].target_addr = actual_target;
    }

    ghr = ((ghr << 1) | (actual_taken ? 1u : 0u)) & ghr_mask;
}

void GSharePredictor::printState(uint64_t pc) {
    int pht_index = static_cast<int>((pc ^ ghr) & ghr_mask);
    std::cout << "GHR: " << std::bitset<4>(ghr)
              << " | PC XOR GHR: " << pht_index
              << " | Counter: " << pht[pht_index] << "\n";
}
