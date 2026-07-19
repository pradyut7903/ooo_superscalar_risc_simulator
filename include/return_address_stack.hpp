#pragma once

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>

class ReturnAddressStack {
private:
    std::vector<uint64_t> stack;
    int capacity;
    int tos;
    int count;

public:
    ReturnAddressStack(int size);

    void push(uint64_t return_pc);
    bool pop(uint64_t& predicted_target);
    bool peek(uint64_t& predicted_target) const;

    void restoreState(int saved_tos, int saved_count);
    void restoreFull(int saved_tos, int saved_count, const std::vector<uint64_t>& saved_stack);
    void getSnapshot(int& out_tos, int& out_count) const;
    void getFullSnapshot(int& out_tos, int& out_count, std::vector<uint64_t>& out_stack) const;
    void printState() const;
};
