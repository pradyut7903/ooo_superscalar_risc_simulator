#pragma once

#include <iostream>
#include <string>
#include <vector>

struct ROBEntry {
    bool busy;
    std::string op;
    int dest_reg;
    int value;
    bool done;
};

class ReorderBuffer {
private:
    std::vector<ROBEntry> buffer;
    int capacity;
    int head;
    int tail;
    int active_count;

public:
    ReorderBuffer(int size);

    int dispatch(std::string operation, int logical_dest_reg);
    // Undo the most recent dispatch (rob_id must be the current tail-1).
    bool undispatch(int rob_id);
    bool hasSpace() const;
    void writeResult(int rob_id, int computed_value);
    bool commit(int& out_dest_reg, int& out_value, int& out_rob_id);
    bool isReady(int rob_id) const;
    int getValue(int rob_id) const;
    bool isEmpty() const;
    int activeCount() const;
    int getHead() const { return head; }
    int getCapacity() const { return capacity; }

    // Age of tag from head (0 = oldest in-flight).
    int ageFromHead(int tag) const;
    // True if tag_a is strictly younger than tag_b (both relative to head).
    bool isYounger(int tag_a, int tag_b) const;

    // Early recovery: free entries younger than squash_tag; keep squash_tag.
    // Sets tail to squash_tag+1. Returns number of entries freed.
    int squashYoungerThan(int squash_tag);

    void checkInvariant() const;   // assert internal consistency (self-check mode)
    void printState() const;
};
