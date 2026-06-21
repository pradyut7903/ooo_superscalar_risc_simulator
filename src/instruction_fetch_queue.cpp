#include "instruction_fetch_queue.hpp"

InstructionFetchQueue::InstructionFetchQueue(int size)
    : capacity(size), head(0), tail(0), active_count(0) {
    queue.resize(capacity);
}

bool InstructionFetchQueue::enqueue(const FetchedInstruction& inst) {
    if (active_count == capacity) {
        return false;
    }

    queue[tail] = inst;
    tail = (tail + 1) % capacity;
    active_count++;

    return true;
}

bool InstructionFetchQueue::peek(FetchedInstruction& out_inst) const {
    if (active_count == 0) {
        return false;
    }
    out_inst = queue[head];
    return true;
}

bool InstructionFetchQueue::dequeue(FetchedInstruction& out_inst) {
    if (active_count == 0) {
        return false;
    }

    out_inst = queue[head];
    head = (head + 1) % capacity;
    active_count--;

    return true;
}

void InstructionFetchQueue::flush() {
    head = 0;
    tail = 0;
    active_count = 0;
}

bool InstructionFetchQueue::isFull() const {
    return active_count == capacity;
}

bool InstructionFetchQueue::isEmpty() const {
    return active_count == 0;
}

int InstructionFetchQueue::getOccupancy() const {
    return active_count;
}

void InstructionFetchQueue::printState() const {
    std::cout << "IFQ State (Occupancy: " << active_count << "/" << capacity << ")\n";
    int current = head;
    for (int i = 0; i < active_count; i++) {
        std::cout << "  [PC: " << queue[current].pc << "] " << queue[current].opcode << "\n";
        current = (current + 1) % capacity;
    }
    std::cout << "-----------------------\n";
}
