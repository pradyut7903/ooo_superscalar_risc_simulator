#include "return_address_stack.hpp"

ReturnAddressStack::ReturnAddressStack(int size) : capacity(size), tos(0), count(0) {
    stack.resize(static_cast<size_t>(capacity), 0);
}

void ReturnAddressStack::push(uint64_t return_pc) {
    stack[static_cast<size_t>(tos)] = return_pc;
    tos = (tos + 1) % capacity;

    if (count < capacity) {
        count++;
    }
}

bool ReturnAddressStack::pop(uint64_t& predicted_target) {
    if (count == 0) {
        return false;
    }

    tos = (tos - 1 + capacity) % capacity;
    predicted_target = stack[static_cast<size_t>(tos)];
    count--;

    return true;
}

bool ReturnAddressStack::peek(uint64_t& predicted_target) const {
    if (count == 0) return false;
    const int idx = (tos - 1 + capacity) % capacity;
    predicted_target = stack[static_cast<size_t>(idx)];
    return true;
}

void ReturnAddressStack::restoreState(int saved_tos, int saved_count) {
    tos = saved_tos;
    count = saved_count;
}

void ReturnAddressStack::restoreFull(int saved_tos, int saved_count,
                                     const std::vector<uint64_t>& saved_stack) {
    tos = saved_tos;
    count = saved_count;
    stack.assign(static_cast<size_t>(capacity), 0);
    const size_t n = saved_stack.size() < static_cast<size_t>(capacity)
        ? saved_stack.size() : static_cast<size_t>(capacity);
    for (size_t i = 0; i < n; ++i) {
        stack[i] = saved_stack[i];
    }
}

void ReturnAddressStack::getSnapshot(int& out_tos, int& out_count) const {
    out_tos = tos;
    out_count = count;
}

void ReturnAddressStack::getFullSnapshot(int& out_tos, int& out_count,
                                         std::vector<uint64_t>& out_stack) const {
    out_tos = tos;
    out_count = count;
    out_stack = stack;
}

void ReturnAddressStack::printState() const {
    std::cout << "RAS State (Occupancy: " << count << "/" << capacity << ")\n";

    if (count == 0) {
        std::cout << "  [Empty]\n";
    } else {
        int current = (tos - 1 + capacity) % capacity;
        for (int i = 0; i < count; i++) {
            std::cout << "  Stack Level " << i << " | Target PC: 0x"
                      << std::hex << stack[static_cast<size_t>(current)] << std::dec << "\n";
            current = (current - 1 + capacity) % capacity;
        }
    }
    std::cout << "-----------------------\n";
}
