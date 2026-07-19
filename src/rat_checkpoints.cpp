#include "rat_checkpoints.hpp"

#include <cassert>

RatCheckpoints::RatCheckpoints(int rob_depth, int num_regs)
    : rob_depth_(rob_depth), num_regs_(num_regs) {
    reset();
}

void RatCheckpoints::reset() {
    valid_.assign(rob_depth_, false);
    maps_.assign(rob_depth_, std::vector<int>(num_regs_, -1));
}

void RatCheckpoints::save(int rob_tag, const std::vector<int>& rat_map) {
    assert(rob_tag >= 0 && rob_tag < rob_depth_);
    assert(static_cast<int>(rat_map.size()) == num_regs_);
    valid_[rob_tag] = true;
    maps_[rob_tag] = rat_map;
}

bool RatCheckpoints::restore(int rob_tag, std::vector<int>& out_map) const {
    if (rob_tag < 0 || rob_tag >= rob_depth_ || !valid_[rob_tag]) {
        return false;
    }
    out_map = maps_[rob_tag];
    return true;
}

void RatCheckpoints::onCommitClear(int logical_reg, int committing_rob_id) {
    if (logical_reg <= 0 || logical_reg >= num_regs_) {
        return;
    }
    for (int t = 0; t < rob_depth_; ++t) {
        if (!valid_[t]) continue;
        if (maps_[t][logical_reg] == committing_rob_id) {
            maps_[t][logical_reg] = -1;
        }
    }
}

void RatCheckpoints::invalidate(int rob_tag) {
    if (rob_tag >= 0 && rob_tag < rob_depth_) {
        valid_[rob_tag] = false;
    }
}

void RatCheckpoints::invalidateAll() {
    for (int t = 0; t < rob_depth_; ++t) {
        valid_[t] = false;
    }
}
