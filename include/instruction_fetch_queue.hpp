#pragma once

#include "types.hpp"

#include <iostream>
#include <vector>

class InstructionFetchQueue {
private:
    std::vector<FetchedInstruction> queue;
    int capacity;
    int head;
    int tail;
    int active_count;

public:
    InstructionFetchQueue(int size);

    bool enqueue(const FetchedInstruction& inst);
    bool peek(FetchedInstruction& out_inst) const;
    bool dequeue(FetchedInstruction& out_inst);
    void flush();
    bool isFull() const;
    bool isEmpty() const;
    int getOccupancy() const;
    void printState() const;
};
