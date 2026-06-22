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
    // Default constructor keeps the classic 2 ALU / 1 MUL / 1 DIV mix so existing
    // callers and tests are unaffected.
    explicit ExecutionEngine(int cdb_ports);
    // Fully parameterized constructor for functional-unit sweep experiments.
    ExecutionEngine(int cdb_ports, int n_alu, int n_mul, int n_div,
                    int alu_lat, int mul_lat, int div_lat);

    bool issueInstruction(std::string op, int val1, int val2, int rob_id);
    std::vector<CDBResult> tick();
    void printState() const;
};
