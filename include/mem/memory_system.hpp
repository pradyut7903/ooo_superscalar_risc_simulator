#pragma once

#include "mem/dcache.hpp"
#include "mem/dram_simple.hpp"
#include "mem/icache.hpp"
#include "mem/mem_types.hpp"
#include "types.hpp"

#include <memory>
#include <optional>
#include <vector>

// Abstract data + instruction memory services used by LSQ / fetch.
class MemorySystem {
public:
    virtual ~MemorySystem() = default;

    virtual void reset() = 0;
    virtual void tick() = 0;

    virtual bool tryDataReq(const DataMemReq& req) = 0;
    virtual std::vector<DataMemResp> popDataResps() = 0;

    virtual bool tryImemReq(uint32_t pc) = 0;
    virtual std::optional<ImemResp> popImemResp() = 0;

    virtual MemSystem kind() const = 0;
};

class IdealMemorySystem : public MemorySystem {
public:
    explicit IdealMemorySystem(int fetch_width);

    void reset() override;
    void tick() override;
    bool tryDataReq(const DataMemReq& req) override;
    std::vector<DataMemResp> popDataResps() override;
    bool tryImemReq(uint32_t pc) override;
    std::optional<ImemResp> popImemResp() override;
    MemSystem kind() const override { return MemSystem::Ideal; }

private:
    int fetch_width_;
    bool imem_pending_ = false;
    uint32_t imem_pc_ = 0;
    std::optional<ImemResp> imem_resp_;
    std::vector<DataMemResp> data_resps_;
};

struct CachedMemConfig {
    int fetch_width = 4;
    int line_bytes = 32;
    int d_sets = 16;
    int d_ways = 4;
    int d_mshr = 4;
    int d_ufp = 2;
    int d_mshr_waiters = 4;
    int i_sets = 16;
    int i_ways = 4;
    int i_mshr = 2;
    int dram_outstanding = 4;
    int dram_lat = 10;
    int load_hit_latency = 1;  // D$ load hit (0 = same cycle)
    int fetch_hit_latency = 1; // I$ hit (0 = same cycle)
};

class CachedMemorySystem : public MemorySystem {
public:
    explicit CachedMemorySystem(const CachedMemConfig& cfg);

    void setInstrMem(const std::vector<uint32_t>* imem) { dram_.setInstrMem(imem); }
    void squashFetch() { icache_.flush(); }
    // Early recovery: flush I$ only (RTL core ties D$ flush off). Stale D$ load
    // replies are rejected in LSQ via mem_req_sent + cookie (inst_id).
    void flushOnRedirect() { icache_.flush(); }
    void flushDcacheLoads() { dcache_.flushLoads(); }
    void writebackDirty() { dcache_.writebackDirtyToMemory(); }

    void reset() override;
    void tick() override;
    bool tryDataReq(const DataMemReq& req) override;
    std::vector<DataMemResp> popDataResps() override;
    bool tryImemReq(uint32_t pc) override;
    std::optional<ImemResp> popImemResp() override;
    MemSystem kind() const override { return MemSystem::Cached; }

private:
    static constexpr int kDramClientD = 0;
    static constexpr int kDramClientI = 1;

    int line_bytes_;
    DramSimple dram_;
    DCache dcache_;
    ICache icache_;

    void arbitrateToDram();
    void routeDramResps();
};
