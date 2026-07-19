#pragma once

#include "cdb_arbiter.hpp"
#include "mem/mem_types.hpp"
#include "types.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

class MemorySystem;

struct LSQEntry {
    bool valid = false;
    bool is_load = false;
    uint32_t inst_id = 0;
    int rob_id = -1;
    uint16_t age = 0;  // RTL age_q (monotonic)

    MemSize mem_size = MemSize::W;
    bool mem_unsigned = false;

    bool addr_ready = false;
    int address = 0;

    bool data_ready = false;
    int data = 0;

    bool executed = false;

    bool is_accessing_memory = false;
    int cycles_remaining = 0;
    int memory_data_buffer = 0;

    bool needs_mem_fill = false;
    bool partial_fwd_en = false;
    uint32_t partial_fwd_word = 0;
    uint8_t partial_fwd_mask = 0;

    // Cached path: waiting on tagged D$ response (id == queue index).
    bool awaiting_mem_resp = false;
    bool mem_req_sent = false;
    uint32_t mem_cookie = 0;  // inst_id latched at issue; must match resp

    // RTL ST_CDB_WAIT: data ready, offer CDB next cycle (not same cycle as resp/fwd).
    bool cdb_wait = false;
};

struct StoreBufferEntry {
    bool valid = false;
    bool issued = false;
    int address = 0;
    uint32_t wdata = 0;
    uint8_t wstrb = 0;
    int cycles_remaining = 0;
    bool awaiting_mem_resp = false;  // cached: wait for write ack
};

class LoadStoreQueue {
private:
    std::vector<LSQEntry> queue;
    int capacity;
    int num_cdb_ports;
    int mem_latency;
    int fwd_latency;
    bool store_forwarding;
    int max_mem_ports;

    std::vector<StoreBufferEntry> sb;
    int sb_capacity = 0;
    int sb_head = 0;
    int sb_tail = 0;
    int sb_count = 0;
    uint16_t next_age_ = 0;

    MemorySystem* memsys_ = nullptr;  // nullptr / Ideal → internal latency model

    static bool ageBefore(uint16_t a, uint16_t b) {
        // RTL: ((b - a) < (1 << (AGE_W-1))) with AGE_W=16
        return static_cast<uint16_t>(b - a) < 0x8000u;
    }

public:
    LoadStoreQueue(int size, int num_load_cdb_ports, int store_buf_depth,
                   int memory_latency = 5, int forward_latency = 1,
                   bool enable_forwarding = true, int max_memory_ports = 4);

    void setMemorySystem(MemorySystem* mem) { memsys_ = mem; }
    bool usesCachedMem() const;

    bool allocate(uint32_t id, int rob_id, bool is_load,
                  MemSize mem_size = MemSize::W, bool mem_unsigned = false);
    bool hasSpace() const;
    void setAddress(uint32_t id, int addr);
    void setStoreData(uint32_t id, int data);
    bool dispatchLoad(uint32_t load_id);

    std::vector<int> enqueueStores(const std::vector<int>& commit_rob_tags);

    void tick();
    // Issue SB drains + load D$ requests (also called after dispatch for same-cycle issue).
    void issueMemoryRequests();
    void applyDataResps(const std::vector<DataMemResp>& resps);
    // Promote ST_CDB_WAIT → offerable (call once per cycle before collectProducers).
    void promoteCdbWaits();

    // Fixed NUM_LSQ lanes, oldest CDB-ready loads first (valid=false if unused).
    std::vector<CdbProducer> collectProducersFixed() const;
    std::vector<CdbProducer> collectProducers() const;
    void releaseSlot(int queue_index);

    void freeLoad(uint32_t load_id);
    void squashInstIds(const std::vector<uint32_t>& ids);
    void flushSpeculative();

    bool isDrained() const;
    int storeBufOccupancy() const { return sb_count; }
    void printState() const;
};

extern std::vector<int> MainMemory;
extern bool MainMemoryByteAddressed;
