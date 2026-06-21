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
    bool hasSpace() const;
    void writeResult(int rob_id, int computed_value);
    bool commit(int& out_dest_reg, int& out_value, int& out_rob_id);
    bool isReady(int rob_id) const;
    int getValue(int rob_id) const;
    bool isEmpty() const;
    void printState() const;
};
