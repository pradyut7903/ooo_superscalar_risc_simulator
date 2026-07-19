#include "golden_model.hpp"

#include <iostream>
#include <string>

// Driver for the golden reference model. Mirrors pipeline_sim's relevant CLI so the
// verification harness can run both the same way and diff their --dump output.
//
//   golden_sim [--dump] [--quiet] [--max-steps N] [--initial-pc HEX] <trace>
int main(int argc, char* argv[]) {
    std::string trace_path;
    bool want_dump = false;
    bool quiet = false;
    uint64_t max_steps = 200000000ULL;
    uint64_t initial_pc = 0x1000;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dump") want_dump = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--max-steps" && i + 1 < argc) max_steps = std::stoull(argv[++i]);
        else if (a == "--initial-pc" && i + 1 < argc) initial_pc = std::stoull(argv[++i], nullptr, 0);
        else if (!a.empty() && a[0] == '-') { std::cerr << "unknown option: " << a << "\n"; return 2; }
        else trace_path = a;
    }
    if (trace_path.empty()) trace_path = "traces/sample.trace";

    GoldenModel gm;
    try {
        gm.loadTrace(trace_path);
    } catch (const std::exception& ex) {
        std::cerr << "Trace load error: " << ex.what() << "\n";
        return 1;
    }

    uint64_t steps = gm.run(initial_pc, max_steps);
    if (gm.hitStepLimit()) {
        std::cerr << "WARNING: golden model hit step limit (" << max_steps
                  << ") -- program may not terminate.\n";
    }
    if (!quiet) {
        std::cout << "Golden model: retired " << steps << " instructions.\n";
    }
    if (want_dump) {
        gm.printArchState();
    }
    return 0;
}
