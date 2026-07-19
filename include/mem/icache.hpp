#pragma once

#include "mem/dram_simple.hpp"
#include "mem/mem_types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// Non-blocking read-only I$ with MSHRs (timing model of rtl_v2 icache).
// Hit path: accept -> pipe (1) -> hit_pend (hit_latency) -> resp.
class ICache {
public:
    ICache(int line_bytes, int sets, int ways, int mshr_count, int fetch_width,
           int hit_latency);

    void reset();
    void flush();
    void squashFetch() { flush(); }

    void tickBegin();
    void tickEnd();

    bool tryCpuReq(uint32_t pc);
    std::optional<ImemResp> popCpuResp();

    bool hasDramReq() const;
    uint32_t dramReqAddr() const;
    int dramReqId() const;
    void dramReqAccepted();
    void dramResp(int mshr_idx, const std::vector<uint8_t>& line);

private:
    struct Way {
        bool valid = false;
        bool reserved = false;
        uint32_t tag = 0;
        std::vector<uint8_t> data;
        uint64_t lru = 0;
    };
    struct Mshr {
        bool valid = false;
        bool fill_sent = false;
        bool fill_ready = false;
        bool drop = false;
        bool has_waiter = false;
        uint32_t line_addr = 0;
        uint32_t req_pc = 0;
        int set = 0;
        int way = 0;
        std::vector<uint8_t> line;
    };
    struct Pipe {
        bool valid = false;
        uint32_t pc = 0;
    };
    struct HitPend {
        bool valid = false;
        bool drop = false;
        int cycles_left = 0;
        uint32_t pc = 0;
        std::vector<uint8_t> line;
    };

    int line_bytes_;
    int sets_;
    int ways_;
    int mshr_count_;
    int fetch_width_;
    int hit_latency_;
    uint64_t lru_clock_ = 1;

    std::vector<std::vector<Way>> cache_;
    std::vector<Mshr> mshrs_;
    Pipe pipe_{};
    HitPend hit_pend_{};
    std::optional<ImemResp> resp_;
    int dram_mshr_ = -1;

    bool canAccept() const;
    int setIndex(uint32_t addr) const;
    uint32_t tagOf(uint32_t addr) const;
    int findHit(int set, uint32_t tag) const;
    int findVictim(int set) const;
    int findMshr(uint32_t line_addr) const;
    int allocMshr();
    ImemResp buildResp(uint32_t pc, const std::vector<uint8_t>& line) const;
    void completeMshr(int mi);
    void processPipe();
};
