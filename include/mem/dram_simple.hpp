#pragma once

#include "mem/mem_types.hpp"

#include <cstdint>
#include <vector>

// Fixed-latency line DRAM (rtl_v2 dram_model_simple). Backing store is MainMemory.
class DramSimple {
public:
    DramSimple(int line_bytes, int outstanding, int latency_cycles);

    void reset();
    void tick();
    // Optional separate instruction image (hex imem); D$ uses MainMemory.
    void setInstrMem(const std::vector<uint32_t>* imem) { imem_ = imem; }

    // One accept per cycle when a slot is free (arbiter serializes clients).
    // from_imem: read fill bytes from instruction image instead of MainMemory.
    bool tryAccept(bool write, uint32_t line_addr, const std::vector<uint8_t>& wdata,
                   int dram_id, bool from_imem = false);
    bool tryPopResp(int& dram_id, std::vector<uint8_t>& rdata);

private:
    const std::vector<uint32_t>* imem_ = nullptr;
    struct Slot {
        bool valid = false;
        bool write = false;
        uint32_t line_addr = 0;
        int dram_id = -1;
        int cycles_left = 0;
        std::vector<uint8_t> data;
    };

    int line_bytes_;
    int outstanding_;
    int latency_;
    std::vector<Slot> slots_;
    bool accepted_this_cycle_ = false;
};
