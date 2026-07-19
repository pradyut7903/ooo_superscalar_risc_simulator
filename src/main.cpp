#include "processor.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <charconv>

namespace {

void printUsage(const char* prog) {
    std::cerr <<
        "Usage: " << prog << " [options] [<trace> | --imem FILE]\n"
        "\n"
        "Input (pick one):\n"
        "  <trace>          Toy opcode trace (default traces/sample.trace)\n"
        "  --imem FILE      RV32IM instruction memory hex (one word/line)\n"
        "  --dmem FILE      Optional data memory hex (with --imem)\n"
        "\n"
        "Microarchitecture knobs (all integers unless noted):\n"
        "  --width N        Superscalar fetch/issue/commit width (default 4)\n"
        "  --rob N          Reorder buffer entries (default 32)\n"
        "  --rs N           Reservation station entries (default 32)\n"
        "  --lsq N          Load/store queue entries (default 32)\n"
        "  --ifq N          Instruction fetch queue entries (default 32)\n"
        "  --cdb N          Common data bus write ports (default = width)\n"
        "  --ras N          Return address stack depth (default 16)\n"
        "  --pht N          gshare PHT entries (default 1024)\n"
        "  --btb N          BTB entries (default 256)\n"
        "  --bp MODE        Branch predictor: gshare (default) or always-taken\n"
        "                   gshare = PHT+BTB, no RAS; always-taken = PC-rel taken + RAS\n"
        "  --alu N          Number of ALU units (default 2)\n"
        "  --mul N          Number of MUL units (default 1)\n"
        "  --div N          Number of DIV units (default 1)\n"
        "  --br N           Number of dedicated branch units (default 1)\n"
        "  --lsq-cdb N      Load CDB producer ports / NUM_LSQ (default 2)\n"
        "  --sb N           Committed store buffer depth (default 8)\n"
        "  --mem-system S   ideal or cached (default cached; RTL MEM_SYSTEM)\n"
        "  --dram-lat N     DRAM line latency when cached (default 10)\n"
        "  --cache-line N   I$/D$ line bytes (default 32; best-IPC study used 64)\n"
        "  --dcache-sets N  D$ sets (default 16)\n"
        "  --dcache-ways N  D$ ways (default 4)\n"
        "  --icache-sets N  I$ sets (default 16)\n"
        "  --icache-ways N  I$ ways (default 4)\n"
        "  --dcache-mshr N  D$ MSHR count (default 4)\n"
        "  --icache-mshr N  I$ MSHR count (default 2)\n"
        "  --dcache-ufp N   D$ UFP ports 1..2 (default 2)\n"
        "  --dram-out N     DRAM outstanding slots (default 4)\n"
        "  --mshr-waiters N D$ MSHR waiter queue depth (default 4)\n"
        "  --load-hit-lat N D$ load-hit latency cycles (default 1; 0=same cycle)\n"
        "  --fetch-hit-lat N I$ hit latency cycles (default 1; 0=same cycle)\n"
        "  --alu-lat N      ALU latency in cycles (default 1)\n"
        "  --mul-lat N      MUL latency in cycles (default 3)\n"
        "  --div-lat N      DIV latency in cycles (default 10)\n"
        "  --br-lat N       Branch unit latency in cycles (default 1)\n"
        "  --mem-lat N      Ideal-path load latency in cycles (default 1)\n"
        "  --fwd-lat N      Store-to-load forwarding latency (default 1)\n"
        "  --no-forward     Disable store-to-load forwarding\n"
        "  --max-cycles N   Safety cap on simulated cycles (default 100000)\n"
        "\n"
        "Output:\n"
        "  --stats          Print a human-readable statistics summary\n"
        "  --csv LABEL      Print one CSV data row tagged with LABEL\n"
        "  --csv-header     Print the CSV header row and exit\n"
        "  --dump           Print final architectural state (regs + memory)\n"
        "  --dump-regs      Print x0..xN as xN=0x... (for golden compare)\n"
        "  --selfcheck      Assert structural invariants every cycle (slow)\n"
        "  --pipeline-trace FILE  Dump IFQ/RS/ROB/LSQ/EU state every cycle to FILE\n"
        "  --quiet          Suppress the banner / progress text\n";
}

// Pull the integer value following a flag; exits on a missing/garbage value.
int nextInt(int argc, char* argv[], int& i, const char* flag) {
    if (i + 1 >= argc) {
        std::cerr << "Error: " << flag << " requires a value\n";
        std::exit(2);
    }
    const char* s = argv[++i];
    int value = 0;
    const auto* begin = s;
    const auto* end = s + std::strlen(s);
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        std::cerr << "Error: " << flag << " expects an integer, got '" << s << "'\n";
        std::exit(2);
    }
    return value;
}

std::string nextStr(int argc, char* argv[], int& i, const char* flag) {
    if (i + 1 >= argc) {
        std::cerr << "Error: " << flag << " requires a value\n";
        std::exit(2);
    }
    return argv[++i];
}

}  // namespace

int main(int argc, char* argv[]) {
    ProcessorConfig cfg{};
    // Toy traces historically start at 0x1000; hex mode forces PC=0 in loadHexProgram.
    cfg.initial_pc = 0x1000;

    std::string trace_path;
    std::string imem_path;
    std::string dmem_path;
    std::string csv_label;
    bool want_stats = false;
    bool want_csv = false;
    bool want_dump = false;
    bool want_dump_regs = false;
    bool quiet = false;
    bool cdb_set = false;
    bool self_check = false;
    std::string pipeline_trace;

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
        else if (a == "--bp") {
            const std::string s = nextStr(argc, argv, i, "--bp");
            if (s == "gshare" || s == "GSHARE") cfg.bp_mode = BranchPredMode::GShare;
            else if (s == "always-taken" || s == "always_taken" || s == "at")
                cfg.bp_mode = BranchPredMode::AlwaysTaken;
            else {
                std::cerr << "Error: --bp expects gshare|always-taken\n";
                return 2;
            }
        }
        else if (a == "--alu") cfg.num_alu = nextInt(argc, argv, i, "--alu");
        else if (a == "--mul") cfg.num_mul = nextInt(argc, argv, i, "--mul");
        else if (a == "--div") cfg.num_div = nextInt(argc, argv, i, "--div");
        else if (a == "--br") cfg.num_br = nextInt(argc, argv, i, "--br");
        else if (a == "--lsq-cdb") cfg.num_lsq = nextInt(argc, argv, i, "--lsq-cdb");
        else if (a == "--sb") cfg.store_buf_depth = nextInt(argc, argv, i, "--sb");
        else if (a == "--mem-system") {
            const std::string s = nextStr(argc, argv, i, "--mem-system");
            if (s == "cached" || s == "CACHE" || s == "1") cfg.mem_system = MemSystem::Cached;
            else if (s == "ideal" || s == "IDEAL" || s == "0") cfg.mem_system = MemSystem::Ideal;
            else {
                std::cerr << "Error: --mem-system expects ideal|cached\n";
                return 2;
            }
        }
        else if (a == "--dram-lat") cfg.dram_lat_cycles = nextInt(argc, argv, i, "--dram-lat");
        else if (a == "--cache-line") cfg.cache_line_bytes = nextInt(argc, argv, i, "--cache-line");
        else if (a == "--dcache-sets") cfg.dcache_sets = nextInt(argc, argv, i, "--dcache-sets");
        else if (a == "--dcache-ways") cfg.dcache_ways = nextInt(argc, argv, i, "--dcache-ways");
        else if (a == "--icache-sets") cfg.icache_sets = nextInt(argc, argv, i, "--icache-sets");
        else if (a == "--icache-ways") cfg.icache_ways = nextInt(argc, argv, i, "--icache-ways");
        else if (a == "--dcache-mshr") cfg.dcache_mshr = nextInt(argc, argv, i, "--dcache-mshr");
        else if (a == "--icache-mshr") cfg.icache_mshr = nextInt(argc, argv, i, "--icache-mshr");
        else if (a == "--dcache-ufp") cfg.dcache_ufp_ports = nextInt(argc, argv, i, "--dcache-ufp");
        else if (a == "--dram-out") cfg.dram_outstanding = nextInt(argc, argv, i, "--dram-out");
        else if (a == "--mshr-waiters") cfg.mshr_waiters = nextInt(argc, argv, i, "--mshr-waiters");
        else if (a == "--load-hit-lat") cfg.load_hit_latency = nextInt(argc, argv, i, "--load-hit-lat");
        else if (a == "--fetch-hit-lat") cfg.fetch_hit_latency = nextInt(argc, argv, i, "--fetch-hit-lat");
        else if (a == "--alu-lat") cfg.alu_latency = nextInt(argc, argv, i, "--alu-lat");
        else if (a == "--mul-lat") cfg.mul_latency = nextInt(argc, argv, i, "--mul-lat");
        else if (a == "--div-lat") cfg.div_latency = nextInt(argc, argv, i, "--div-lat");
        else if (a == "--br-lat") cfg.br_latency = nextInt(argc, argv, i, "--br-lat");
        else if (a == "--mem-lat") cfg.mem_latency = nextInt(argc, argv, i, "--mem-lat");
        else if (a == "--fwd-lat") cfg.fwd_latency = nextInt(argc, argv, i, "--fwd-lat");
        else if (a == "--no-forward") cfg.store_forwarding = false;
        else if (a == "--max-cycles") {
            cfg.max_cycles = nextInt(argc, argv, i, "--max-cycles");
            if (cfg.max_cycles <= 0) {
                std::cerr << "Error: --max-cycles must be > 0\n";
                return 2;
            }
        }
        else if (a == "--imem") imem_path = nextStr(argc, argv, i, "--imem");
        else if (a == "--dmem") dmem_path = nextStr(argc, argv, i, "--dmem");
        else if (a == "--stats") want_stats = true;
        else if (a == "--dump") want_dump = true;
        else if (a == "--dump-regs") want_dump_regs = true;
        else if (a == "--selfcheck") self_check = true;
        else if (a == "--pipeline-trace") {
            if (i + 1 >= argc) { std::cerr << "Error: --pipeline-trace requires a file\n"; return 2; }
            pipeline_trace = argv[++i];
        }
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

    if (!imem_path.empty() && !trace_path.empty()) {
        std::cerr << "Error: pass either a toy <trace> or --imem, not both\n";
        return 2;
    }
    if (imem_path.empty() && trace_path.empty()) {
        trace_path = "traces/sample.trace";
    }
    if (!cdb_set) {
        cfg.cdb_width = cfg.width;  // by default the CDB tracks issue width
    }

    Processor cpu(cfg);

    try {
        if (!imem_path.empty()) {
            cpu.loadHexProgram(imem_path, dmem_path);
        } else {
            cpu.loadInstructionCache(trace_path);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Load error: " << ex.what() << "\n";
        return 1;
    }

    if (!quiet) {
        std::cout << "Pipeline Simulator (Tomasulo + ROB, "
                  << cfg.width << "-wide superscalar)\n";
        if (!imem_path.empty()) {
            std::cout << "Loaded imem: " << imem_path;
            if (!dmem_path.empty()) std::cout << "  dmem: " << dmem_path;
            std::cout << "\n";
        } else {
            std::cout << "Loaded trace: " << trace_path << "\n";
        }
        std::cout << "Starting simulation...\n";
    }

    cpu.enableSelfCheck(self_check);

    if (!pipeline_trace.empty()) {
        // Cycle-by-cycle timing-verification dump: write every structure's state to
        // the file after each tick(), so a single instruction can be traced through
        // fetch -> RS -> execute -> CDB -> commit by hand.
        std::ofstream tf(pipeline_trace);
        if (!tf.is_open()) {
            std::cerr << "cannot open pipeline-trace file: " << pipeline_trace << "\n";
            return 1;
        }
        std::streambuf* old = std::cout.rdbuf(tf.rdbuf());
        while (!cpu.isFinished()) {
            cpu.tick();
            cpu.printState();
        }
        std::cout.rdbuf(old);
    } else {
        cpu.run();
    }

    const bool hit_cap =
        cpu.getCycleCount() >= static_cast<uint64_t>(cpu.getConfig().max_cycles);

    if (want_dump) {
        cpu.printArchState();
    }
    if (want_dump_regs) {
        cpu.printRegsDump();
    }

    if (want_csv) {
        cpu.printStatsCSV(csv_label);
    } else if (want_stats) {
        cpu.printStats();
    } else if (!quiet) {
        std::cout << "\nSimulation finished after " << cpu.getCycleCount() << " cycles.\n";
    }

    if (hit_cap) {
        // Always visible, even under --quiet --dump (harnesses key on this).
        std::cerr << "HIT max_cycles\n";
        return 1;
    }
    return 0;
}
