#pragma once

#include "types.hpp"

#include <iostream>
#include <string>
#include <vector>

class ExecutionUnit {
public:
    std::string type;
    int latency;
    bool busy;
    int rob_id;
    int cycles_remaining;
    int computed_value;

    ExecutionUnit(std::string t, int l);

    bool issue(std::string op, int val1, int val2, int r_id);
    void tick();
    bool isFinished() const;
    void clear();
};

class ExecutionEngine {
private:
    std::vector<ExecutionUnit> units;
    int cdb_bandwidth;

public:
    ExecutionEngine(int cdb_ports);

    bool issueInstruction(std::string op, int val1, int val2, int rob_id);
    std::vector<CDBResult> tick();
    void printState() const;
};
