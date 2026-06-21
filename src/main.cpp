#include "processor.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string trace_path = "traces/sample.trace";
    if (argc > 1) {
        trace_path = argv[1];
    }

    ProcessorConfig cfg{};
    cfg.initial_pc = 0x1000;
    cfg.max_cycles = 5000;

    Processor cpu(cfg);

    try {
        cpu.loadInstructionCache(trace_path);
    } catch (const std::exception& ex) {
        std::cerr << "Trace load error: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "Pipeline Simulator (Tomasulo + ROB, "
              << ProcessorConfig::WIDTH << "-wide superscalar)\n";
    std::cout << "Loaded trace: " << trace_path << "\n";
    std::cout << "Starting simulation...\n\n";

    cpu.run();

    std::cout << "Simulation finished after " << cpu.getCycleCount() << " cycles.\n";
    return 0;
}
