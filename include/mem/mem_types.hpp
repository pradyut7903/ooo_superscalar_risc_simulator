#pragma once

#include <cstdint>
#include <vector>

// Shared memory-system request/response types (RTL mem_* / imem_* contracts).

struct DataMemReq {
    bool write = false;
    uint32_t addr = 0;
    uint32_t wdata = 0;
    uint8_t wstrb = 0xF;
    int id = -1;       // LSQ entry, or LSQ_DEPTH + sb_index
    uint32_t cookie = 0;  // load inst_id (generation); ignored for stores
};

struct DataMemResp {
    int id = -1;
    uint32_t rdata = 0;
    uint32_t cookie = 0;
    bool is_write = false;
};

struct ImemReq {
    uint32_t pc = 0;
};

struct ImemResp {
    uint32_t pc = 0;
    int count = 0;                 // valid words this response
    std::vector<uint32_t> words;   // size == count (up to WIDTH)
};

inline uint32_t lineAlign(uint32_t addr, int line_bytes) {
    const uint32_t mask = ~static_cast<uint32_t>(line_bytes - 1);
    return addr & mask;
}

inline int lineOffset(uint32_t addr, int line_bytes) {
    return static_cast<int>(addr & static_cast<uint32_t>(line_bytes - 1));
}
