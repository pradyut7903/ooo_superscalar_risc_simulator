#pragma once

#include "mem/dram_simple.hpp"
#include "mem/mem_types.hpp"

#include <cstdint>
#include <deque>
#include <vector>

// Non-blocking write-back / write-allocate D$ with MSHR waiter queues.
// Timing-oriented model of rtl_v2 dcache (not a line-by-line structural copy).
class DCache {
public:
    DCache(int line_bytes, int sets, int ways, int mshr_count, int ufp_ports,
           int mshr_waiters, int load_hit_latency);

    void reset();
    // Drop in-flight *load* CPU replies (committed store waiters kept).
    void flushLoads();
    // Write all dirty lines into MainMemory (for architectural dump).
    void writebackDirtyToMemory();

    void tickBegin();
    void tickEnd();

    bool tryCpuReq(const DataMemReq& req);
    std::vector<DataMemResp> popCpuResps();

    bool hasDramReq() const;
    bool dramReqIsWrite() const;
    uint32_t dramReqAddr() const;
    const std::vector<uint8_t>& dramReqData() const;
    int dramReqId() const;
    void dramReqAccepted();
    void dramResp(int mshr_idx, const std::vector<uint8_t>& line);

private:
    struct Way {
        bool valid = false;
        bool dirty = false;
        bool reserved = false;
        uint32_t tag = 0;
        std::vector<uint8_t> data;
        uint64_t lru = 0;
    };
    struct Waiter {
        DataMemReq req{};
        bool drop = false;  // flush: do not respond
    };
    struct Mshr {
        bool valid = false;
        uint32_t line_addr = 0;
        int set = 0;
        int way = 0;

        bool need_wb = false;
        bool wb_sent = false;
        bool wb_done = true;
        uint32_t wb_addr = 0;
        std::vector<uint8_t> wb_data;

        bool fill_sent = false;
        bool fill_ready = false;
        std::vector<uint8_t> line;

        std::vector<Waiter> waiters;

        bool draining = false;
        int drain_idx = 0;
        std::vector<uint8_t> drain_line;
    };
    struct Pipe {
        bool valid = false;
        DataMemReq req{};
    };
    struct HitPend {
        bool valid = false;
        bool drop = false;
        int cycles_left = 0;
        DataMemResp resp{};
    };

    int line_bytes_;
    int sets_;
    int ways_;
    int mshr_count_;
    int ufp_ports_;
    int mshr_waiters_;
    int load_hit_latency_;
    uint64_t lru_clock_ = 1;
    int cpu_accepts_ = 0;

    std::vector<std::vector<Way>> cache_;
    std::vector<Mshr> mshrs_;
    std::vector<Pipe> pipe_;       // per-UFP compare pipe (RTL pipe_valid)
    std::vector<HitPend> hit_pend_;
    std::deque<DataMemResp> resp_q_;

    int dram_mshr_ = -1;
    bool dram_is_wb_ = false;
    int drain_mshr_ = -1;

    int setIndex(uint32_t addr) const;
    uint32_t tagOf(uint32_t addr) const;
    uint32_t lineAddrOf(int set, uint32_t tag) const;
    int findHit(int set, uint32_t tag) const;
    int findVictim(int set) const;
    int allocMshr();
    int findMshr(uint32_t line_addr) const;
    int pickDramMshr() const;
    int freePipePort() const;
    void applyStoreToLine(std::vector<uint8_t>& line, const DataMemReq& req);
    uint32_t loadWordFromLine(const std::vector<uint8_t>& line, uint32_t addr) const;
    void processPipes();
    void startDrain(int mi);
    void stepDrain();
    void completeFill(int mi);
};
