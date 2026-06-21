#include "return_address_stack.hpp"

ReturnAddressStack::ReturnAddressStack(int size) : capacity(size), tos(0), count(0) {
    stack.resize(capacity, 0);
}

void ReturnAddressStack::push(uint64_t return_pc) {
    stack[tos] = return_pc;
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
    predicted_target = stack[tos];
    count--;

    return true;
}

void ReturnAddressStack::restoreState(int saved_tos, int saved_count) {
    tos = saved_tos;
    count = saved_count;
}

void ReturnAddressStack::getSnapshot(int& out_tos, int& out_count) const {
    out_tos = tos;
    out_count = count;
}

void ReturnAddressStack::printState() const {
    std::cout << "RAS State (Occupancy: " << count << "/" << capacity << ")\n";

    if (count == 0) {
        std::cout << "  [Empty]\n";
    } else {
        int current = (tos - 1 + capacity) % capacity;
        for (int i = 0; i < count; i++) {
            std::cout << "  Stack Level " << i << " | Target PC: 0x"
                      << std::hex << stack[current] << std::dec << "\n";
            current = (current - 1 + capacity) % capacity;
        }
    }
    std::cout << "-----------------------\n";
}
