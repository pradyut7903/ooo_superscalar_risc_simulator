#pragma once

#include <iostream>
#include <vector>

class RegisterAliasTable {
private:
    std::vector<int> rat;
    int num_logical_regs;

public:
    RegisterAliasTable(int logical_count);

    int lookup(int logical_reg);
    void rename(int logical_reg, int rob_id);
    void commit(int logical_reg, int committing_rob_id);
    std::vector<int> getState() const;
    void restoreState(const std::vector<int>& state);
    void printState() const;
};
