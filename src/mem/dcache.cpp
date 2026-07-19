#include "mem/dcache.hpp"

#include <cassert>
#include <cstring>

DCache::DCache(int line_bytes, int sets, int ways, int mshr_count, int ufp_ports,
               int mshr_waiters, int load_hit_latency)
    : line_bytes_(line_bytes), sets_(sets), ways_(ways),
      mshr_count_(mshr_count), ufp_ports_(ufp_ports > 0 ? ufp_ports : 1),
      mshr_waiters_(mshr_waiters > 0 ? mshr_waiters : 1),
      load_hit_latency_(load_hit_latency < 0 ? 0 : load_hit_latency) {
    assert(line_bytes_ > 0 && sets_ > 0 && ways_ > 0);
    cache_.assign(static_cast<size_t>(sets_), std::vector<Way>(static_cast<size_t>(ways_)));
    for (auto& set : cache_) {
        for (auto& w : set) w.data.assign(static_cast<size_t>(line_bytes_), 0);
    }
    mshrs_.resize(static_cast<size_t>(mshr_count_ > 0 ? mshr_count_ : 1));
    pipe_.assign(static_cast<size_t>(ufp_ports_), Pipe{});
    hit_pend_.resize(static_cast<size_t>(ufp_ports_));
    if (load_hit_latency_ < 1) load_hit_latency_ = 1;
}

void DCache::reset() {
    for (auto& set : cache_) {
        for (auto& w : set) {
            w = Way{};
            w.data.assign(static_cast<size_t>(line_bytes_), 0);
        }
    }
    for (auto& m : mshrs_) m = Mshr{};
    for (auto& p : pipe_) p = Pipe{};
    for (auto& h : hit_pend_) h = HitPend{};
    resp_q_.clear();
    dram_mshr_ = -1;
    dram_is_wb_ = false;
    drain_mshr_ = -1;
    cpu_accepts_ = 0;
    lru_clock_ = 1;
}

void DCache::flushLoads() {
    for (auto& h : hit_pend_) {
        if (h.valid) h.drop = true;
    }
    for (auto& p : pipe_) {
        if (p.valid && !p.req.write) p = Pipe{};
    }
    for (auto& m : mshrs_) {
        if (!m.valid) continue;
        for (auto& w : m.waiters) {
            if (!w.req.write) w.drop = true;
        }
    }
    // Drop already-queued load replies; keep store acks.
    std::deque<DataMemResp> kept;
    while (!resp_q_.empty()) {
        auto r = resp_q_.front();
        resp_q_.pop_front();
        if (r.is_write) kept.push_back(r);
    }
    resp_q_ = std::move(kept);
}

void DCache::writebackDirtyToMemory() {
    // Arch dump helper: push dirty lines into MainMemory (byte-indexed words).
    extern std::vector<int> MainMemory;
    for (int s = 0; s < sets_; ++s) {
        for (int w = 0; w < ways_; ++w) {
            auto& way = cache_[static_cast<size_t>(s)][static_cast<size_t>(w)];
            if (!way.valid || !way.dirty) continue;
            const uint32_t laddr = lineAddrOf(s, way.tag);
            const int nwords = line_bytes_ / 4;
            for (int i = 0; i < nwords; ++i) {
                uint32_t word = 0;
                std::memcpy(&word, way.data.data() + 4 * i, 4);
                const size_t wi = static_cast<size_t>((laddr >> 2) + static_cast<uint32_t>(i));
                if (wi < MainMemory.size()) {
                    MainMemory[wi] = static_cast<int>(word);
                }
            }
            way.dirty = false;
        }
    }
}

void DCache::tickBegin() {
    cpu_accepts_ = 0;
}

int DCache::setIndex(uint32_t addr) const {
    const uint32_t line = addr / static_cast<uint32_t>(line_bytes_);
    return static_cast<int>(line % static_cast<uint32_t>(sets_));
}

uint32_t DCache::tagOf(uint32_t addr) const {
    return addr / static_cast<uint32_t>(line_bytes_ * sets_);
}

uint32_t DCache::lineAddrOf(int set, uint32_t tag) const {
    return (tag * static_cast<uint32_t>(sets_) + static_cast<uint32_t>(set)) *
           static_cast<uint32_t>(line_bytes_);
}

int DCache::findHit(int set, uint32_t tag) const {
    for (int w = 0; w < ways_; ++w) {
        const auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(w)];
        if (way.valid && !way.reserved && way.tag == tag) return w;
    }
    return -1;
}

int DCache::findVictim(int set) const {
    for (int w = 0; w < ways_; ++w) {
        const auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(w)];
        if (!way.valid && !way.reserved) return w;
    }
    int victim = -1;
    uint64_t best = 0;
    for (int w = 0; w < ways_; ++w) {
        const auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(w)];
        if (way.reserved) continue;
        if (victim < 0 || way.lru < best) {
            best = way.lru;
            victim = w;
        }
    }
    return victim;
}

int DCache::allocMshr() {
    for (int i = 0; i < mshr_count_; ++i) {
        if (!mshrs_[static_cast<size_t>(i)].valid) return i;
    }
    return -1;
}

int DCache::findMshr(uint32_t line_addr) const {
    for (int i = 0; i < mshr_count_; ++i) {
        const auto& m = mshrs_[static_cast<size_t>(i)];
        if (m.valid && m.line_addr == line_addr) return i;
    }
    return -1;
}

void DCache::applyStoreToLine(std::vector<uint8_t>& line, const DataMemReq& req) {
    const int off = lineOffset(req.addr, line_bytes_) & ~3;
    for (int b = 0; b < 4; ++b) {
        if (!(req.wstrb & (1u << b))) continue;
        const int idx = off + b;
        if (idx < 0 || idx >= line_bytes_) continue;
        line[static_cast<size_t>(idx)] = static_cast<uint8_t>((req.wdata >> (8 * b)) & 0xFF);
    }
}

uint32_t DCache::loadWordFromLine(const std::vector<uint8_t>& line, uint32_t addr) const {
    const int off = lineOffset(addr, line_bytes_) & ~3;
    if (off < 0 || off + 4 > line_bytes_) return 0;
    uint32_t w = 0;
    std::memcpy(&w, line.data() + off, 4);
    return w;
}

int DCache::freePipePort() const {
    // RTL: pipe_free = !pipe_valid && !hit_pend_valid
    for (int p = 0; p < ufp_ports_; ++p) {
        if (!pipe_[static_cast<size_t>(p)].valid &&
            !hit_pend_[static_cast<size_t>(p)].valid) {
            return p;
        }
    }
    return -1;
}

bool DCache::tryCpuReq(const DataMemReq& req) {
    if (cpu_accepts_ >= ufp_ports_) return false;
    const int p = freePipePort();
    if (p < 0) return false;
    // Latch into compare pipe; tag lookup in tickEnd (RTL).
    pipe_[static_cast<size_t>(p)].valid = true;
    pipe_[static_cast<size_t>(p)].req = req;
    ++cpu_accepts_;
    return true;
}

void DCache::processPipes() {
    // Same-cycle store-hit merge shadow (RTL hit_store_line).
    bool shadow_valid = false;
    int shadow_set = 0;
    int shadow_way = 0;
    std::vector<uint8_t> shadow_line;

    for (int p = 0; p < ufp_ports_; ++p) {
        auto& pipe = pipe_[static_cast<size_t>(p)];
        if (!pipe.valid) continue;
        pipe.valid = false;
        const DataMemReq& req = pipe.req;

        const uint32_t laddr = lineAlign(req.addr, line_bytes_);
        const int set = setIndex(req.addr);
        const uint32_t tag = tagOf(req.addr);

        const int existing = findMshr(laddr);
        if (existing >= 0) {
            auto& m = mshrs_[static_cast<size_t>(existing)];
            if (m.fill_ready || m.draining) {
                pipe.valid = true;  // hold (RTL)
                continue;
            }
            if (static_cast<int>(m.waiters.size()) >= mshr_waiters_) {
                pipe.valid = true;
                continue;
            }
            Waiter w;
            w.req = req;
            w.drop = false;
            m.waiters.push_back(w);
            continue;
        }

        const int hit = findHit(set, tag);
        if (hit >= 0) {
            auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(hit)];
            way.lru = lru_clock_++;
            if (req.write) {
                if (shadow_valid && shadow_set == set && shadow_way == hit) {
                    applyStoreToLine(shadow_line, req);
                    way.data = shadow_line;
                } else {
                    applyStoreToLine(way.data, req);
                    shadow_line = way.data;
                    shadow_valid = true;
                    shadow_set = set;
                    shadow_way = hit;
                }
                way.dirty = true;
                DataMemResp resp;
                resp.id = req.id;
                resp.cookie = req.cookie;
                resp.is_write = true;
                resp.rdata = 0;
                resp_q_.push_back(resp);  // store hit: resp on compare cycle
            } else {
                DataMemResp resp;
                resp.id = req.id;
                resp.cookie = req.cookie;
                resp.is_write = false;
                if (shadow_valid && shadow_set == set && shadow_way == hit) {
                    resp.rdata = loadWordFromLine(shadow_line, req.addr);
                } else {
                    resp.rdata = loadWordFromLine(way.data, req.addr);
                }
                auto& hp = hit_pend_[static_cast<size_t>(p)];
                if (hp.valid) {
                    // Should not happen: port blocked while hit_pend occupied.
                    pipe.valid = true;
                    pipe.req = req;
                } else {
                    hp.valid = true;
                    hp.drop = false;
                    hp.cycles_left = load_hit_latency_;
                    hp.resp = resp;
                }
            }
            continue;
        }

        const int mi = allocMshr();
        const int victim = findVictim(set);
        if (mi < 0 || victim < 0) {
            pipe.valid = true;
            pipe.req = req;
            continue;
        }

        auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(victim)];
        auto& m = mshrs_[static_cast<size_t>(mi)];
        m = Mshr{};
        m.valid = true;
        m.line_addr = laddr;
        m.set = set;
        m.way = victim;
        m.line.assign(static_cast<size_t>(line_bytes_), 0);
        m.need_wb = way.valid && way.dirty;
        m.wb_sent = false;
        m.wb_done = !m.need_wb;
        if (m.need_wb) {
            m.wb_addr = lineAddrOf(set, way.tag);
            m.wb_data = way.data;
        }
        m.fill_sent = false;
        m.fill_ready = false;
        Waiter w0;
        w0.req = req;
        w0.drop = false;
        m.waiters.push_back(w0);
        way.reserved = true;
    }
}

int DCache::pickDramMshr() const {
    for (int i = 0; i < mshr_count_; ++i) {
        const auto& m = mshrs_[static_cast<size_t>(i)];
        if (!m.valid) continue;
        if (m.need_wb && !m.wb_done && !m.wb_sent) return i;
    }
    for (int i = 0; i < mshr_count_; ++i) {
        const auto& m = mshrs_[static_cast<size_t>(i)];
        if (!m.valid) continue;
        if (m.wb_done && !m.fill_sent) return i;
    }
    return -1;
}

bool DCache::hasDramReq() const {
    if (dram_mshr_ >= 0) return false;
    return pickDramMshr() >= 0;
}

bool DCache::dramReqIsWrite() const {
    const int i = pickDramMshr();
    if (i < 0) return false;
    const auto& m = mshrs_[static_cast<size_t>(i)];
    return m.need_wb && !m.wb_done && !m.wb_sent;
}

uint32_t DCache::dramReqAddr() const {
    const int i = pickDramMshr();
    if (i < 0) return 0;
    const auto& m = mshrs_[static_cast<size_t>(i)];
    if (m.need_wb && !m.wb_done && !m.wb_sent) return m.wb_addr;
    return m.line_addr;
}

const std::vector<uint8_t>& DCache::dramReqData() const {
    static const std::vector<uint8_t> empty;
    const int i = pickDramMshr();
    if (i < 0) return empty;
    const auto& m = mshrs_[static_cast<size_t>(i)];
    if (m.need_wb && !m.wb_done && !m.wb_sent) return m.wb_data;
    return empty;
}

int DCache::dramReqId() const {
    const int i = pickDramMshr();
    return i >= 0 ? i : 0;
}

void DCache::dramReqAccepted() {
    const int i = pickDramMshr();
    if (i < 0) return;
    auto& m = mshrs_[static_cast<size_t>(i)];
    dram_mshr_ = i;
    if (m.need_wb && !m.wb_done && !m.wb_sent) {
        m.wb_sent = true;
        dram_is_wb_ = true;
    } else {
        m.fill_sent = true;
        dram_is_wb_ = false;
    }
}

void DCache::dramResp(int mshr_idx, const std::vector<uint8_t>& line) {
    if (mshr_idx < 0 || mshr_idx >= mshr_count_) return;
    auto& m = mshrs_[static_cast<size_t>(mshr_idx)];
    if (!m.valid) return;

    if (dram_mshr_ == mshr_idx) {
        const bool was_wb = dram_is_wb_;
        dram_mshr_ = -1;
        dram_is_wb_ = false;
        if (was_wb) {
            m.wb_done = true;
            return;
        }
    } else if (m.wb_sent && !m.wb_done) {
        m.wb_done = true;
        return;
    }

    assert(static_cast<int>(line.size()) >= line_bytes_);
    m.line.assign(line.begin(), line.begin() + line_bytes_);
    m.fill_ready = true;
}

void DCache::completeFill(int mi) {
    auto& m = mshrs_[static_cast<size_t>(mi)];
    if (!m.fill_ready || m.draining) return;

    // Apply stores in waiter order (younger later → wins on overlap).
    std::vector<uint8_t> line = m.line;
    bool any_store = false;
    for (const auto& w : m.waiters) {
        if (w.req.write) {
            applyStoreToLine(line, w.req);
            any_store = true;
        }
    }

    auto& way = cache_[static_cast<size_t>(m.set)][static_cast<size_t>(m.way)];
    way.valid = true;
    way.reserved = false;
    way.tag = tagOf(m.line_addr);
    way.data = line;
    way.dirty = any_store;
    way.lru = lru_clock_++;

    m.fill_ready = false;
    m.drain_line = std::move(line);
    startDrain(mi);
}

void DCache::startDrain(int mi) {
    auto& m = mshrs_[static_cast<size_t>(mi)];
    m.draining = true;
    m.drain_idx = 0;
    if (drain_mshr_ < 0) {
        drain_mshr_ = mi;
    }
}

void DCache::stepDrain() {
    if (drain_mshr_ < 0) {
        for (int i = 0; i < mshr_count_; ++i) {
            if (mshrs_[static_cast<size_t>(i)].valid &&
                mshrs_[static_cast<size_t>(i)].draining) {
                drain_mshr_ = i;
                break;
            }
        }
    }
    if (drain_mshr_ < 0) return;

    auto& m = mshrs_[static_cast<size_t>(drain_mshr_)];
    if (!m.valid || !m.draining) {
        drain_mshr_ = -1;
        return;
    }

    // One waiter response per cycle (RTL single-flight drain).
    while (m.drain_idx < static_cast<int>(m.waiters.size())) {
        const Waiter& w = m.waiters[static_cast<size_t>(m.drain_idx)];
        ++m.drain_idx;
        if (w.drop) {
            continue;  // flushed load: no CPU resp
        }
        DataMemResp resp;
        resp.id = w.req.id;
        resp.cookie = w.req.cookie;
        resp.is_write = w.req.write;
        if (w.req.write) {
            resp.rdata = 0;
        } else {
            // Miss drain: respond this cycle (RTL drain path, not hit_pend).
            resp.rdata = loadWordFromLine(m.drain_line, w.req.addr);
        }
        resp_q_.push_back(resp);
        break;
    }

    if (m.drain_idx >= static_cast<int>(m.waiters.size())) {
        m = Mshr{};
        drain_mshr_ = -1;
    }
}

void DCache::tickEnd() {
    // RTL order: emit hit_pend, process pipes, then fills/drain.
    for (auto& h : hit_pend_) {
        if (!h.valid) continue;
        if (h.cycles_left > 0) h.cycles_left--;
        if (h.cycles_left == 0) {
            if (!h.drop) resp_q_.push_back(h.resp);
            h = HitPend{};
        }
    }

    processPipes();

    for (int i = 0; i < mshr_count_; ++i) {
        if (mshrs_[static_cast<size_t>(i)].valid &&
            mshrs_[static_cast<size_t>(i)].fill_ready) {
            completeFill(i);
        }
    }
    stepDrain();
}

std::vector<DataMemResp> DCache::popCpuResps() {
    // RTL: up to WIDTH resp lanes/cycle. Cap via ufp_ports_*WIDTH proxy:
    // use a soft cap of max(ufp_ports_*2, 4) matching WIDTH default when WIDTH=4.
    const int max_resp = (ufp_ports_ <= 2) ? 4 : ufp_ports_;
    std::vector<DataMemResp> out;
    while (!resp_q_.empty() && static_cast<int>(out.size()) < max_resp) {
        out.push_back(resp_q_.front());
        resp_q_.pop_front();
    }
    return out;
}
