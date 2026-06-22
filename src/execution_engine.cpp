#include "execution_engine.hpp"

ExecutionUnit::ExecutionUnit(std::string t, int l)
    : type(t), latency(l), busy(false), rob_id(-1), cycles_remaining(0), computed_value(0) {}

bool ExecutionUnit::issue(std::string op, int val1, int val2, int r_id) {
    if (busy) return false;

    busy = true;
    rob_id = r_id;
    cycles_remaining = latency;

    if (op == "ADD") computed_value = val1 + val2;
    else if (op == "SUB") computed_value = val1 - val2;
    else if (op == "MUL") computed_value = val1 * val2;
    else if (op == "DIV") computed_value = (val2 != 0) ? (val1 / val2) : 0;
    else if (op == "BEQ") computed_value = (val1 == val2) ? 1 : 0;
    else if (op == "BNE") computed_value = (val1 != val2) ? 1 : 0;
    else computed_value = 0;

    return true;
}

void ExecutionUnit::tick() {
    if (busy && cycles_remaining > 0) {
        cycles_remaining--;
    }
}

bool ExecutionUnit::isFinished() const {
    return busy && cycles_remaining == 0;
}

void ExecutionUnit::clear() {
    busy = false;
    rob_id = -1;
    cycles_remaining = 0;
    computed_value = 0;
}

ExecutionEngine::ExecutionEngine(int cdb_ports)
    : ExecutionEngine(cdb_ports, 2, 1, 1, 1, 3, 10) {}

ExecutionEngine::ExecutionEngine(int cdb_ports, int n_alu, int n_mul, int n_div,
                                 int alu_lat, int mul_lat, int div_lat)
    : cdb_bandwidth(cdb_ports) {
    for (int i = 0; i < n_alu; ++i) units.push_back(ExecutionUnit("ALU", alu_lat));
    for (int i = 0; i < n_mul; ++i) units.push_back(ExecutionUnit("MUL", mul_lat));
    for (int i = 0; i < n_div; ++i) units.push_back(ExecutionUnit("DIV", div_lat));
}

bool ExecutionEngine::issueInstruction(std::string op, int val1, int val2, int rob_id) {
    std::string required_type = "ALU";
    if (op == "MUL") required_type = "MUL";
    if (op == "DIV") required_type = "DIV";

    for (auto& unit : units) {
        if (unit.type == required_type && !unit.busy) {
            unit.issue(op, val1, val2, rob_id);
            return true;
        }
    }
    return false;
}

std::vector<CDBResult> ExecutionEngine::tick() {
    std::vector<CDBResult> broadcasts;

    for (auto& unit : units) {
        unit.tick();
    }

    for (auto& unit : units) {
        if (unit.isFinished()) {
            if (broadcasts.size() < static_cast<size_t>(cdb_bandwidth)) {
                broadcasts.push_back({unit.rob_id, unit.computed_value});
                unit.clear();
            }
        }
    }

    return broadcasts;
}

void ExecutionEngine::printState() const {
    std::cout << "Execution Units State:\n";
    for (size_t i = 0; i < units.size(); i++) {
        std::cout << "  Unit " << i << " (" << units[i].type << ") | ";
        if (units[i].busy) {
            std::cout << "Executing ROB" << units[i].rob_id
                      << " | Cycles Left: " << units[i].cycles_remaining;
            if (units[i].cycles_remaining == 0) {
                std::cout << " (WAITING FOR CDB)";
            }
        } else {
            std::cout << "IDLE";
        }
        std::cout << "\n";
    }
    std::cout << "-----------------------\n";
}
