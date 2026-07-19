#include "mem/memory_system.hpp"

#include "load_store_queue.hpp"

#include <cassert>
#include <cstring>

// ---------------------------------------------------------------------------
// Ideal
// ---------------------------------------------------------------------------

IdealMemorySystem::IdealMemorySystem(int fetch_width)
    : fetch_width_(fetch_width > 0 ? fetch_width : 1) {}

void IdealMemorySystem::reset() {
    imem_pending_ = false;
    imem_resp_.reset();
    data_resps_.clear();
}

void IdealMemorySystem::tick() {
    if (imem_pending_ && !imem_resp_) {
        ImemResp r;
        r.pc = imem_pc_;
        r.count = 0;
        r.words.reserve(static_cast<size_t>(fetch_width_));
        for (int i = 0; i < fetch_width_; ++i) {
            const uint32_t addr = imem_pc_ + static_cast<uint32_t>(4 * i);
            const size_t wi = static_cast<size_t>(addr >> 2);
            // Ideal imem for hex uses Processor::imem, not MainMemory — unused
            // when Processor bypasses Ideal for fetch. Keep stub empty.
            (void)wi;
        }
        imem_pending_ = false;
    }
}

bool IdealMemorySystem::tryDataReq(const DataMemReq& req) {
    DataMemResp resp;
    resp.id = req.id;
    resp.cookie = req.cookie;
    resp.is_write = req.write;
    if (req.write) {
        // Byte merge into MainMemory
        const size_t idx = static_cast<size_t>(req.addr >> 2);
        if (idx < MainMemory.size()) {
            uint32_t cur = static_cast<uint32_t>(MainMemory[idx]);
            for (int b = 0; b < 4; ++b) {
                if (req.wstrb & (1u << b)) {
                    const uint32_t mask = 0xFFu << (8 * b);
                    cur = (cur & ~mask) | (req.wdata & mask);
                }
            }
            MainMemory[idx] = static_cast<int>(cur);
        }
        resp.rdata = 0;
    } else {
        const size_t idx = static_cast<size_t>(req.addr >> 2);
        resp.rdata = (idx < MainMemory.size())
            ? static_cast<uint32_t>(MainMemory[idx]) : 0;
    }
    data_resps_.push_back(resp);
    return true;
}

std::vector<DataMemResp> IdealMemorySystem::popDataResps() {
    std::vector<DataMemResp> out;
    out.swap(data_resps_);
    return out;
}

bool IdealMemorySystem::tryImemReq(uint32_t pc) {
    if (imem_pending_ || imem_resp_) return false;
    imem_pending_ = true;
    imem_pc_ = pc;
    return true;
}

std::optional<ImemResp> IdealMemorySystem::popImemResp() {
    if (!imem_resp_) return std::nullopt;
    auto r = *imem_resp_;
    imem_resp_.reset();
    return r;
}

// ---------------------------------------------------------------------------
// Cached
// ---------------------------------------------------------------------------

CachedMemorySystem::CachedMemorySystem(const CachedMemConfig& cfg)
    : line_bytes_(cfg.line_bytes),
      dram_(cfg.line_bytes, cfg.dram_outstanding, cfg.dram_lat),
      dcache_(cfg.line_bytes, cfg.d_sets, cfg.d_ways, cfg.d_mshr, cfg.d_ufp,
              cfg.d_mshr_waiters, cfg.load_hit_latency),
      icache_(cfg.line_bytes, cfg.i_sets, cfg.i_ways, cfg.i_mshr, cfg.fetch_width,
              cfg.fetch_hit_latency) {}

void CachedMemorySystem::reset() {
    dram_.reset();
    dcache_.reset();
    icache_.reset();
}

void CachedMemorySystem::arbitrateToDram() {
    // D$ priority over I$ (rtl mem_arbiter).
    if (dcache_.hasDramReq()) {
        const bool wr = dcache_.dramReqIsWrite();
        const uint32_t addr = dcache_.dramReqAddr();
        const int mid = dcache_.dramReqId();
        const int dram_id = (kDramClientD << 16) | (mid & 0xFFFF);
        if (dram_.tryAccept(wr, addr, dcache_.dramReqData(), dram_id, false)) {
            dcache_.dramReqAccepted();
        }
        return;
    }
    if (icache_.hasDramReq()) {
        const uint32_t addr = icache_.dramReqAddr();
        const int mid = icache_.dramReqId();
        const int dram_id = (kDramClientI << 16) | (mid & 0xFFFF);
        static const std::vector<uint8_t> empty;
        if (dram_.tryAccept(false, addr, empty, dram_id, true)) {
            icache_.dramReqAccepted();
        }
    }
}

void CachedMemorySystem::routeDramResps() {
    int dram_id = 0;
    std::vector<uint8_t> line;
    if (!dram_.tryPopResp(dram_id, line)) return;
    const int client = (dram_id >> 16) & 0xFFFF;
    const int mid = dram_id & 0xFFFF;
    if (client == kDramClientD) {
        dcache_.dramResp(mid, line);
    } else {
        icache_.dramResp(mid, line);
    }
}

void CachedMemorySystem::tick() {
    dcache_.tickBegin();
    icache_.tickBegin();
    dram_.tick();
    routeDramResps();
    arbitrateToDram();
    dcache_.tickEnd();
    icache_.tickEnd();
}

bool CachedMemorySystem::tryDataReq(const DataMemReq& req) {
    return dcache_.tryCpuReq(req);
}

std::vector<DataMemResp> CachedMemorySystem::popDataResps() {
    return dcache_.popCpuResps();
}

bool CachedMemorySystem::tryImemReq(uint32_t pc) {
    return icache_.tryCpuReq(pc);
}

std::optional<ImemResp> CachedMemorySystem::popImemResp() {
    return icache_.popCpuResp();
}
