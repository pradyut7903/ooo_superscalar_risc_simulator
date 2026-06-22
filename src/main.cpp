#include "processor.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

void printUsage(const char* prog) {
    std::cerr <<
        "Usage: " << prog << " [options] <trace>\n"
        "\n"
        "Microarchitecture knobs (all integers unless noted):\n"
        "  --width N        Superscalar fetch/issue/commit width (default 4)\n"
        "  --rob N          Reorder buffer entries (default 32)\n"
        "  --rs N           Reservation station entries (default 32)\n"
        "  --lsq N          Load/store queue entries (default 32)\n"
        "  --ifq N          Instruction fetch queue entries (default 32)\n"
        "  --cdb N          Common data bus write ports (default = width)\n"
        "  --ras N          Return address stack depth (default 16)\n"
        "  --pht N          gshare PHT entries (default 16)\n"
        "  --btb N          BTB entries (default 16)\n"
        "  --alu N          Number of ALU units (default 2)\n"
        "  --mul N          Number of MUL units (default 1)\n"
        "  --div N          Number of DIV units (default 1)\n"
        "  --alu-lat N      ALU latency in cycles (default 1)\n"
        "  --mul-lat N      MUL latency in cycles (default 3)\n"
        "  --div-lat N      DIV latency in cycles (default 10)\n"
        "  --mem-lat N      Load (memory) latency in cycles (default 5)\n"
        "  --fwd-lat N      Store-to-load forwarding latency (default 1)\n"
        "  --no-forward     Disable store-to-load forwarding\n"
        "  --max-cycles N   Safety cap on simulated cycles (default 100000)\n"
        "\n"
        "Output:\n"
        "  --stats          Print a human-readable statistics summary\n"
        "  --csv LABEL      Print one CSV data row tagged with LABEL\n"
        "  --csv-header     Print the CSV header row and exit\n"
        "  --dump           Print final architectural state (regs + memory)\n"
        "  --quiet          Suppress the banner / progress text\n";
}

// Pull the integer value following a flag; exits on a missing/garbage value.
int nextInt(int argc, char* argv[], int& i, const char* flag) {
    if (i + 1 >= argc) {
        std::cerr << "Error: " << flag << " requires a value\n";
        std::exit(2);
    }
    return std::atoi(argv[++i]);
}

}  // namespace

int main(int argc, char* argv[]) {
    ProcessorConfig cfg{};
    cfg.initial_pc = 0x1000;

    std::string trace_path;
    std::string csv_label;
    bool want_stats = false;
    bool want_csv = false;
    bool want_dump = false;
    bool quiet = false;
    bool cdb_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--width") cfg.width = nextInt(argc, argv, i, "--width");
        else if (a == "--rob") cfg.rob_size = nextInt(argc, argv, i, "--rob");
        else if (a == "--rs") cfg.rs_size = nextInt(argc, argv, i, "--rs");
        else if (a == "--lsq") cfg.lsq_size = nextInt(argc, argv, i, "--lsq");
        else if (a == "--ifq") cfg.ifq_size = nextInt(argc, argv, i, "--ifq");
        else if (a == "--cdb") { cfg.cdb_width = nextInt(argc, argv, i, "--cdb"); cdb_set = true; }
        else if (a == "--ras") cfg.ras_size = nextInt(argc, argv, i, "--ras");
        else if (a == "--pht") cfg.pht_size = nextInt(argc, argv, i, "--pht");
        else if (a == "--btb") cfg.btb_size = nextInt(argc, argv, i, "--btb");
        else if (a == "--alu") cfg.num_alu = nextInt(argc, argv, i, "--alu");
        else if (a == "--mul") cfg.num_mul = nextInt(argc, argv, i, "--mul");
        else if (a == "--div") cfg.num_div = nextInt(argc, argv, i, "--div");
        else if (a == "--alu-lat") cfg.alu_latency = nextInt(argc, argv, i, "--alu-lat");
        else if (a == "--mul-lat") cfg.mul_latency = nextInt(argc, argv, i, "--mul-lat");
        else if (a == "--div-lat") cfg.div_latency = nextInt(argc, argv, i, "--div-lat");
        else if (a == "--mem-lat") cfg.mem_latency = nextInt(argc, argv, i, "--mem-lat");
        else if (a == "--fwd-lat") cfg.fwd_latency = nextInt(argc, argv, i, "--fwd-lat");
        else if (a == "--no-forward") cfg.store_forwarding = false;
        else if (a == "--max-cycles") cfg.max_cycles = nextInt(argc, argv, i, "--max-cycles");
        else if (a == "--stats") want_stats = true;
        else if (a == "--dump") want_dump = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--csv-header") { Processor::printStatsCSVHeader(); return 0; }
        else if (a == "--csv") {
            want_csv = true;
            if (i + 1 < argc) csv_label = argv[++i];
            else csv_label = "run";
        }
        else if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            printUsage(argv[0]);
            return 2;
        }
        else trace_path = a;
    }

    if (trace_path.empty()) {
        trace_path = "traces/sample.trace";
    }
    if (!cdb_set) {
        cfg.cdb_width = cfg.width;  // by default the CDB tracks issue width
    }

    Processor cpu(cfg);

    try {
        cpu.loadInstructionCache(trace_path);
    } catch (const std::exception& ex) {
        std::cerr << "Trace load error: " << ex.what() << "\n";
        return 1;
    }

    if (!quiet) {
        std::cout << "Pipeline Simulator (Tomasulo + ROB, "
                  << cfg.width << "-wide superscalar)\n";
        std::cout << "Loaded trace: " << trace_path << "\n";
        std::cout << "Starting simulation...\n";
    }

    cpu.run();

    if (want_dump) {
        cpu.printArchState();
    }

    if (want_csv) {
        cpu.printStatsCSV(csv_label);
    } else if (want_stats) {
        cpu.printStats();
    } else if (!quiet) {
        std::cout << "\nSimulation finished after " << cpu.getCycleCount() << " cycles.\n";
    }

    return 0;
}
