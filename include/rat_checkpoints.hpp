#pragma once

#include <vector>

// Per-ROB-tag RAT snapshots for execute-time branch recovery
// (mirrors github_ooo_rv32im/rtl/rat_checkpoints.sv).
//
// Save after renaming a control op. On mispredict, restore that tag's map.
// Commit clears matching tags inside every live snapshot so a restore cannot
// revive a ROB id that already retired.
class RatCheckpoints {
public:
    RatCheckpoints(int rob_depth, int num_regs);

    void reset();

    // Save post-rename RAT map under this ROB tag.
    void save(int rob_tag, const std::vector<int>& rat_map);

    // Restore map for rob_tag. Returns false if no live checkpoint.
    bool restore(int rob_tag, std::vector<int>& out_map) const;

    // When arch reg `logical_reg` commits from `committing_rob_id`, clear that
    // mapping inside every valid checkpoint (same as RTL cm_we path).
    void onCommitClear(int logical_reg, int committing_rob_id);

    // Drop checkpoints for ROB tags that were squashed / freed.
    void invalidate(int rob_tag);
    void invalidateAll();

private:
    int rob_depth_;
    int num_regs_;
    std::vector<bool> valid_;
    std::vector<std::vector<int>> maps_;  // [rob_tag][reg] -> rob id or -1
};
