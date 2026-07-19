#include "trace_loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

uint64_t parseUint64(const std::string& token) {
    if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
        return std::stoull(token, nullptr, 16);
    }
    return std::stoull(token, nullptr, 10);
}

// Signed immediate that may be hex (0x..), negative decimal (-5), or negative hex.
int parseInt(const std::string& token) {
    if (token.empty()) return 0;
    bool neg = false;
    std::string t = token;
    if (t[0] == '-') { neg = true; t = t.substr(1); }
    else if (t[0] == '+') { t = t.substr(1); }
    long long value = 0;
    if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) {
        value = std::stoll(t, nullptr, 16);
    } else {
        value = std::stoll(t, nullptr, 10);
    }
    return static_cast<int>(neg ? -value : value);
}

}  // namespace

ParsedProgram loadTraceFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open trace file: " + path);
    }

    ParsedProgram prog;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string first;
        if (!(iss >> first)) {
            continue;
        }

        // Data-segment directive: ".mem ADDR VALUE" preloads main memory.
        if (first == ".mem") {
            std::string addr_tok, val_tok;
            if (iss >> addr_tok >> val_tok) {
                prog.mem_init.emplace_back(parseInt(addr_tok), parseInt(val_tok));
            }
            continue;
        }

        std::string opcode;
        int dest = 0, src1 = 0, src2 = 0;
        std::string imm_token;
        if (!(iss >> opcode >> dest >> src1 >> src2)) {
            continue;
        }

        FetchedInstruction inst{};
        inst.pc = parseUint64(first);
        inst.opcode = opcode;
        inst.dest_reg = dest;
        inst.src1_reg = src1;
        inst.src2_reg = src2;
        inst.imm = (iss >> imm_token) ? parseInt(imm_token) : 0;
        inst.predicted_taken = false;
        inst.predicted_target = 0;

        prog.icache[inst.pc] = inst;
    }
    return prog;
}
