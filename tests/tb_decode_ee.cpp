// Directed smoke checks for Stage 2: RV32IM decode + executeOp semantics.
// Not wired into the cycle pipeline yet — verifies the new modules in isolation.

#include "decode.hpp"
#include "execution_engine.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

static int failures = 0;

static void expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        ++failures;
    }
}

static void expectEq(int64_t got, int64_t want, const std::string& msg) {
    if (got != want) {
        std::cerr << "FAIL: " << msg << " got=" << got << " want=" << want << "\n";
        ++failures;
    }
}

int main() {
    // addi x1, x0, 5  -> 0x00500093
    {
        Uop u = decodeInstruction(0x00500093u, 0x1000);
        expect(u.op == Op::ALU_ADD, "ADDI op");
        expect(u.fu == Fu::ALU, "ADDI fu");
        expect(u.rd_used && u.rd == 1, "ADDI rd");
        expect(u.rs1_used && u.rs1 == 0, "ADDI rs1");
        expect(u.src2_is_imm && u.imm == 5, "ADDI imm");
        expectEq(executeOp(u.op, 0, u.imm), 5, "ADDI execute");
    }

    // add x3, x1, x2  -> 0x002081B3
    {
        Uop u = decodeInstruction(0x002081B3u, 0);
        expect(u.op == Op::ALU_ADD && u.rd == 3 && u.rs1 == 1 && u.rs2 == 2, "ADD decode");
        expectEq(executeOp(Op::ALU_ADD, 7, 9), 16, "ADD execute");
    }

    // sub x4, x1, x2  -> 0x40208233
    {
        Uop u = decodeInstruction(0x40208233u, 0);
        expect(u.op == Op::ALU_SUB, "SUB decode");
        expectEq(executeOp(Op::ALU_SUB, 10, 3), 7, "SUB execute");
    }

    // sll / srl / sra / and / or / xor / slt
    expectEq(executeOp(Op::ALU_SLL, 1, 4), 16, "SLL");
    expectEq(executeOp(Op::ALU_SRL, static_cast<int32_t>(0xF0000000u), 4),
             static_cast<int32_t>(0x0F000000u), "SRL");
    expectEq(executeOp(Op::ALU_SRA, static_cast<int32_t>(0xF0000000u), 4),
             static_cast<int32_t>(0xFF000000u), "SRA");
    expectEq(executeOp(Op::ALU_AND, 0xF0, 0x0F), 0, "AND");
    expectEq(executeOp(Op::ALU_OR, 0xF0, 0x0F), 0xFF, "OR");
    expectEq(executeOp(Op::ALU_XOR, 0xFF, 0x0F), 0xF0, "XOR");
    expectEq(executeOp(Op::ALU_SLT, -1, 1), 1, "SLT");
    expectEq(executeOp(Op::ALU_SLTU, -1, 1), 0, "SLTU");

    // lui x5, 0x12345 -> 0x123452B7
    {
        Uop u = decodeInstruction(0x123452B7u, 0);
        expect(u.op == Op::ALU_LUI && u.rd == 5, "LUI decode");
        expectEq(executeOp(Op::ALU_LUI, 0, 0, u.imm), 0x12345000, "LUI execute");
    }

    // auipc x1, 1 at pc=0x1000 -> 0x00001097
    {
        Uop u = decodeInstruction(0x00001097u, 0x1000);
        expect(u.op == Op::ALU_AUIPC, "AUIPC decode");
        expectEq(executeOp(Op::ALU_AUIPC, 0, 0, u.imm, 0x1000), 0x2000, "AUIPC execute");
    }

    // mul / mulh / div / rem RV32 corner cases
    expectEq(executeOp(Op::MD_MUL, 6, 7), 42, "MUL");
    expectEq(executeOp(Op::MD_DIV, 10, 0), -1, "DIV by zero RV32");
    expectEq(executeOp(Op::MD_REM, 10, 0), 10, "REM by zero RV32");
    expectEq(executeOp(Op::MD_DIV, static_cast<int32_t>(0x80000000u), -1),
             static_cast<int32_t>(0x80000000u), "DIV overflow");

    // beq / bne / blt / jal
    {
        Uop u = decodeInstruction(0x00208463u, 0x1000);  // beq x1, x2, +8
        expect(u.op == Op::BR_EQ && u.is_branch, "BEQ decode");
        expect(branchTaken(Op::BR_EQ, 3, 3), "BEQ taken");
        expect(!branchTaken(Op::BR_EQ, 3, 4), "BEQ not taken");
        expectEq(branchTarget(Op::BR_EQ, 0x1000, 0, u.imm), 0x1008, "BEQ target");
    }
    expect(branchTaken(Op::BR_LT, -5, 1), "BLT");
    expect(branchTaken(Op::BR_LTU, 1, -1), "BLTU");

    // jal x1, +0x10 at pc=0 -> roughly; use execute link
    expectEq(executeOp(Op::BR_JAL, 0, 0, 0, 0x2000), 0x2004, "JAL link");
    expectEq(branchTarget(Op::BR_JALR, 0, 0x1000, 5), 0x1004, "JALR target align");

    // lw x2, 8(x1) -> 0x0080A103
    {
        Uop u = decodeInstruction(0x0080A103u, 0);
        expect(u.op == Op::MEM_LW && u.is_load && u.imm == 8 && u.rs1 == 1 && u.rd == 2, "LW");
    }

    // sw x2, 8(x1) -> 0x0020A423
    {
        Uop u = decodeInstruction(0x0020A423u, 0);
        expect(u.op == Op::MEM_SW && u.is_store && u.imm == 8 && u.rs1 == 1 && u.rs2 == 2, "SW");
    }

    // fence / ecall / illegal -> NOP
    {
        Uop fence = decodeInstruction(0x0000000Fu, 0);  // fence
        expect(fence.op == Op::UOP_NOP && !fence.rd_used, "FENCE->NOP");
        Uop ecall = decodeInstruction(0x00000073u, 0);
        expect(ecall.op == Op::UOP_NOP, "ECALL->NOP");
        Uop bad = decodeInstruction(0xFFFFFFFFu, 0);
        expect(bad.op == Op::UOP_NOP, "illegal->NOP");
    }

    // x0 write squashed: addi x0, x0, 1
    {
        Uop u = decodeInstruction(0x00100013u, 0);
        expect(u.op == Op::ALU_ADD && u.rd == 0 && !u.rd_used, "x0 rd_used cleared");
    }

    // EE Op issue path: pipelined MUL; freeze on ungranted CDB out
    {
        ExecutionEngine ee(/*alu*/2, /*mul*/1, /*div*/1, /*br*/1,
                           /*alat*/1, /*mlat*/3, /*dlat*/10, /*blat*/1);
        expect(ee.issueInstruction(Op::MD_MUL, 3, 5, /*rob*/1), "issue MUL");
        expect(!ee.issueInstruction(Op::MD_MUL, 1, 1, 2), "MUL stage0 occupied");
        ee.tick();  // stage0 frees; op moves deeper
        expect(ee.issueInstruction(Op::MD_MUL, 2, 4, /*rob*/2), "MUL pipelined issue");
        ee.tick();  // first MUL reaches CDB out (lat 3)
        auto prod = ee.collectProducers();
        expect(prod.size() == 1 && prod[0].rob_id == 1 && prod[0].value == 15, "MUL ready");
        ee.tick();  // CDB stall: out still valid, pipe frozen
        prod = ee.collectProducers();
        expect(prod.size() == 1 && prod[0].rob_id == 1, "MUL holds until CDB grant");
        ee.applyCdbAndAdvance({prod[0].slot});
        prod = ee.collectProducers();
        expect(prod.size() == 1 && prod[0].rob_id == 2 && prod[0].value == 8,
               "MUL1 granted; MUL2 advances to CDB");
    }

    // Branches use dedicated BR unit; cond BR is resolve-bus only (no CDB)
    {
        ExecutionEngine ee(1, 0, 0, 1, 1, 3, 10, 1);
        expect(ee.issueInstruction(Op::BR_EQ, 1, 1, 7, 0, 0, false), "issue BR");
        expect(ee.issueInstruction(Op::ALU_ADD, 2, 3, 8), "issue ALU while BR in flight");
        auto prod = ee.collectProducers();
        expect(prod.size() == 1 && prod[0].rob_id == 8, "only ALU on CDB");
        auto brs = ee.collectBranchResolves();
        expect(brs.size() == 1 && brs[0].rob_id == 7 && !brs[0].offer_cdb,
               "BR on resolve bus");
        ee.applyCdbAndAdvance({brs[0].slot});  // sideband drain (no CDB)
        expect(ee.issueInstruction(Op::BR_JAL, 0, 0, 9, 16, 0x1000, true), "issue JAL");
        brs = ee.collectBranchResolves();
        expect(brs.size() == 1 && brs[0].rob_id == 9 && brs[0].offer_cdb,
               "JAL on resolve bus");
        bool saw_link = false;
        for (const auto& p : ee.collectProducers()) {
            if (p.rob_id == 9 && p.value == 0x1004) saw_link = true;
        }
        expect(saw_link, "JAL link on CDB");
    }

    // Toy string DIV-by-zero still 0 (trace/golden contract)
    {
        ExecutionEngine ee(2, 1, 1, 1, 1, 3, 10, 1);
        expect(ee.issueInstruction("DIV", 10, 0, 3), "issue string DIV");
        for (int i = 0; i < 10; ++i) {
            auto prod = ee.collectProducers();
            if (!prod.empty()) {
                expectEq(prod[0].value, 0, "string DIV by zero -> 0");
                break;
            }
            ee.tick();
        }
    }

    if (failures == 0) {
        std::cout << "tb_decode_ee: ALL PASS\n";
        return 0;
    }
    std::cout << "tb_decode_ee: " << failures << " failure(s)\n";
    return 1;
}
