#pragma once

#include "types.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

struct LSQEntry {
    bool valid;
    bool is_load;
    uint32_t inst_id;
    int rob_id;

    bool addr_ready;
    int address;

    bool data_ready;
    int data;

    bool executed;

    bool is_accessing_memory;
    int cycles_remaining;
    int memory_data_buffer;
};

class LoadStoreQueue {
private:
    std::vector<LSQEntry> queue;
    int capacity;
    int cdb_bandwidth;
    int mem_latency;        // load latency when served from memory
    int fwd_latency;        // load latency when served by store-to-load forwarding
    bool store_forwarding;  // when false, every load goes to memory

public:
    LoadStoreQueue(int size, int cdb_ports,
                   int memory_latency = 5, int forward_latency = 1,
                   bool enable_forwarding = true);

    bool allocate(uint32_t id, int rob_id, bool is_load);
    bool hasSpace() const;
    bool setAddress(uint32_t id, int addr);
    void setStoreData(uint32_t id, int data);
    bool dispatchLoad(uint32_t load_id);
    std::vector<CDBResult> tick();
    void commitStore(uint32_t store_id);
    void freeLoad(uint32_t load_id);
    void printState() const;
};

// Flat main memory shared by the LSQ and trace loader
extern std::vector<int> MainMemory;
