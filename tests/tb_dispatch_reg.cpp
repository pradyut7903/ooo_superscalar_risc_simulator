// Directed checks for DispatchReg prefix-accept / compact behavior (Stage 4).

#include "dispatch_reg.hpp"

#include <iostream>
#include <string>
#include <vector>

static int failures = 0;

static void expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        ++failures;
    }
}

static FetchedInstruction makeInst(uint64_t pc, const std::string& op) {
    FetchedInstruction f;
    f.pc = pc;
    f.opcode = op;
    return f;
}

int main() {
    DispatchReg dr(4);
    expect(dr.canCapture(), "empty can capture");
    expect(dr.empty(), "starts empty");

    dr.capture({makeInst(0x1000, "A"), makeInst(0x1004, "B"),
                makeInst(0x1008, "C"), makeInst(0x100c, "D")});
    expect(!dr.canCapture(), "full holding blocks capture");
    expect(dr.count() == 4, "held 4");

    dr.acceptPrefix(1);
    expect(dr.count() == 3, "after accept 1");
    expect(dr.laneValid(0) && dr.lane(0).opcode == "B", "compacted B to lane0");
    expect(!dr.canCapture(), "still holding");

    dr.acceptPrefix(3);
    expect(dr.empty(), "full pop");
    expect(dr.canCapture(), "ready after full pop");

    dr.capture({makeInst(0x2000, "X"), makeInst(0x2004, "Y")});
    expect(dr.count() == 2, "partial bundle");
    dr.acceptPrefix(2);
    expect(dr.empty() && dr.canCapture(), "accept all");

    if (failures == 0) {
        std::cout << "tb_dispatch_reg: ALL PASS\n";
        return 0;
    }
    std::cout << "tb_dispatch_reg: " << failures << " failure(s)\n";
    return 1;
}
