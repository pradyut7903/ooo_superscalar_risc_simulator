#include "register_alias_table.hpp"

#include <unordered_set>

RegisterAliasTable::RegisterAliasTable(int logical_count) {
    num_logical_regs = logical_count;
    rat.resize(num_logical_regs, -1);
}

int RegisterAliasTable::lookup(int logical_reg) {
    if (logical_reg < 0 || logical_reg >= num_logical_regs) {
        return -1;
    }
    return rat[logical_reg];
}

void RegisterAliasTable::rename(int logical_reg, int rob_id) {
    if (logical_reg >= 0 && logical_reg < num_logical_regs) {
        rat[logical_reg] = rob_id;
    }
}

void RegisterAliasTable::commit(int logical_reg, int committing_rob_id) {
    if (logical_reg >= 0 && logical_reg < num_logical_regs) {
        if (rat[logical_reg] == committing_rob_id) {
            rat[logical_reg] = -1;
        }
    }
}

std::vector<int> RegisterAliasTable::getState() const {
    return rat;
}

void RegisterAliasTable::restoreState(const std::vector<int>& state) {
    if (static_cast<int>(state.size()) == num_logical_regs) {
        rat = state;
    }
}

void RegisterAliasTable::restoreStateScrub(const std::vector<int>& state,
                                           const std::unordered_set<int>& freed_tags) {
    if (static_cast<int>(state.size()) != num_logical_regs) return;
    rat = state;
    if (num_logical_regs > 0) rat[0] = -1;  // x0 never renamed
    for (int i = 1; i < num_logical_regs; ++i) {
        if (rat[i] >= 0 && freed_tags.count(rat[i]) > 0) {
            rat[i] = -1;
        }
    }
}

void RegisterAliasTable::printState() const {
    std::cout << "RAT Mapping:\n";
    for (int i = 0; i < num_logical_regs; i++) {
        std::cout << "  R" << i << " -> ";
        if (rat[i] == -1) {
            std::cout << "ARF (Ready)\n";
        } else {
            std::cout << "ROB" << rat[i] << " (Pending)\n";
        }
    }
    std::cout << "-----------------------\n";
}
