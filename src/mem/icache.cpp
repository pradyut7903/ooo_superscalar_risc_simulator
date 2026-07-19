#include "mem/icache.hpp"

#include <cassert>
#include <cstring>

ICache::ICache(int line_bytes, int sets, int ways, int mshr_count, int fetch_width,
               int hit_latency)
    : line_bytes_(line_bytes), sets_(sets), ways_(ways),
      mshr_count_(mshr_count), fetch_width_(fetch_width > 0 ? fetch_width : 1),
      hit_latency_(hit_latency < 1 ? 1 : hit_latency) {
    cache_.assign(static_cast<size_t>(sets_), std::vector<Way>(static_cast<size_t>(ways_)));
    for (auto& set : cache_) {
        for (auto& w : set) w.data.assign(static_cast<size_t>(line_bytes_), 0);
    }
    mshrs_.resize(static_cast<size_t>(mshr_count_ > 0 ? mshr_count_ : 1));
}

void ICache::reset() {
    for (auto& set : cache_) {
        for (auto& w : set) {
            w = Way{};
            w.data.assign(static_cast<size_t>(line_bytes_), 0);
        }
    }
    for (auto& m : mshrs_) m = Mshr{};
    pipe_ = Pipe{};
    hit_pend_ = HitPend{};
    resp_.reset();
    dram_mshr_ = -1;
    lru_clock_ = 1;
}

void ICache::flush() {
    if (hit_pend_.valid) hit_pend_.drop = true;
    pipe_ = Pipe{};
    resp_.reset();
    for (auto& m : mshrs_) {
        if (!m.valid) continue;
        m.drop = true;
        m.has_waiter = false;
    }
}

void ICache::tickBegin() {}

bool ICache::canAccept() const {
    // RTL: req_ready = !pipe_valid && !hit_pend
    return !pipe_.valid && !hit_pend_.valid && !resp_;
}

int ICache::setIndex(uint32_t addr) const {
    const uint32_t line = addr / static_cast<uint32_t>(line_bytes_);
    return static_cast<int>(line % static_cast<uint32_t>(sets_));
}

uint32_t ICache::tagOf(uint32_t addr) const {
    return addr / static_cast<uint32_t>(line_bytes_ * sets_);
}

int ICache::findHit(int set, uint32_t tag) const {
    for (int w = 0; w < ways_; ++w) {
        const auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(w)];
        if (way.valid && !way.reserved && way.tag == tag) return w;
    }
    return -1;
}

int ICache::findVictim(int set) const {
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

int ICache::findMshr(uint32_t line_addr) const {
    for (int i = 0; i < mshr_count_; ++i) {
        const auto& m = mshrs_[static_cast<size_t>(i)];
        if (m.valid && m.line_addr == line_addr) return i;
    }
    return -1;
}

int ICache::allocMshr() {
    for (int i = 0; i < mshr_count_; ++i) {
        if (!mshrs_[static_cast<size_t>(i)].valid) return i;
    }
    return -1;
}

ImemResp ICache::buildResp(uint32_t pc, const std::vector<uint8_t>& line) const {
    ImemResp r;
    r.pc = pc;
    const uint32_t laddr = lineAlign(pc, line_bytes_);
    const int off = static_cast<int>(pc - laddr);
    const int max_words = (line_bytes_ - off) / 4;
    r.count = max_words < fetch_width_ ? max_words : fetch_width_;
    if (r.count < 1) r.count = 1;
    r.words.resize(static_cast<size_t>(r.count));
    for (int i = 0; i < r.count; ++i) {
        uint32_t w = 0;
        std::memcpy(&w, line.data() + off + 4 * i, 4);
        r.words[static_cast<size_t>(i)] = w;
    }
    return r;
}

bool ICache::tryCpuReq(uint32_t pc) {
    if (!canAccept()) return false;
    // Latch into compare pipe (RTL pipe_valid); lookup next tickEnd.
    pipe_.valid = true;
    pipe_.pc = pc;
    return true;
}

void ICache::processPipe() {
    if (!pipe_.valid) return;
    const uint32_t pc = pipe_.pc;
    pipe_ = Pipe{};

    const int set = setIndex(pc);
    const uint32_t tag = tagOf(pc);
    const uint32_t laddr = lineAlign(pc, line_bytes_);

    const int hit = findHit(set, tag);
    if (hit >= 0) {
        auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(hit)];
        way.lru = lru_clock_++;
        hit_pend_.valid = true;
        hit_pend_.drop = false;
        hit_pend_.cycles_left = hit_latency_;
        hit_pend_.pc = pc;
        hit_pend_.line = way.data;
        return;
    }

    const int existing = findMshr(laddr);
    if (existing >= 0) {
        auto& m = mshrs_[static_cast<size_t>(existing)];
        if (m.fill_ready) {
            // Fill completing: re-hold (RTL holds pipe).
            pipe_.valid = true;
            pipe_.pc = pc;
            return;
        }
        m.has_waiter = true;
        m.req_pc = pc;
        m.drop = false;
        return;
    }

    const int mi = allocMshr();
    const int victim = findVictim(set);
    if (mi < 0 || victim < 0) {
        pipe_.valid = true;
        pipe_.pc = pc;
        return;
    }

    auto& way = cache_[static_cast<size_t>(set)][static_cast<size_t>(victim)];
    auto& m = mshrs_[static_cast<size_t>(mi)];
    m = Mshr{};
    m.valid = true;
    m.line_addr = laddr;
    m.req_pc = pc;
    m.set = set;
    m.way = victim;
    m.has_waiter = true;
    m.drop = false;
    m.fill_sent = false;
    m.fill_ready = false;
    m.line.assign(static_cast<size_t>(line_bytes_), 0);
    way.reserved = true;
}

bool ICache::hasDramReq() const {
    if (dram_mshr_ >= 0) return false;
    for (int i = 0; i < mshr_count_; ++i) {
        const auto& m = mshrs_[static_cast<size_t>(i)];
        if (m.valid && !m.fill_sent) return true;
    }
    return false;
}

uint32_t ICache::dramReqAddr() const {
    for (int i = 0; i < mshr_count_; ++i) {
        const auto& m = mshrs_[static_cast<size_t>(i)];
        if (m.valid && !m.fill_sent) return m.line_addr;
    }
    return 0;
}

int ICache::dramReqId() const {
    for (int i = 0; i < mshr_count_; ++i) {
        const auto& m = mshrs_[static_cast<size_t>(i)];
        if (m.valid && !m.fill_sent) return i;
    }
    return 0;
}

void ICache::dramReqAccepted() {
    for (int i = 0; i < mshr_count_; ++i) {
        auto& m = mshrs_[static_cast<size_t>(i)];
        if (m.valid && !m.fill_sent) {
            m.fill_sent = true;
            dram_mshr_ = i;
            return;
        }
    }
}

void ICache::dramResp(int mshr_idx, const std::vector<uint8_t>& line) {
    if (mshr_idx < 0 || mshr_idx >= mshr_count_) return;
    auto& m = mshrs_[static_cast<size_t>(mshr_idx)];
    if (!m.valid) return;
    assert(static_cast<int>(line.size()) >= line_bytes_);
    m.line.assign(line.begin(), line.begin() + line_bytes_);
    m.fill_ready = true;
    if (dram_mshr_ == mshr_idx) dram_mshr_ = -1;
}

void ICache::completeMshr(int mi) {
    auto& m = mshrs_[static_cast<size_t>(mi)];
    if (!m.fill_ready) return;

    auto& w = cache_[static_cast<size_t>(m.set)][static_cast<size_t>(m.way)];
    // Install once (may retry deliver across cycles if resp_ is busy).
    if (w.reserved || !w.valid || w.tag != tagOf(m.line_addr)) {
        w.valid = true;
        w.reserved = false;
        w.tag = tagOf(m.line_addr);
        w.data = m.line;
        w.lru = lru_clock_++;
    } else {
        w.reserved = false;
    }

    if (m.drop || !m.has_waiter) {
        m = Mshr{};
        return;
    }
    // Never drop a fill silently: hold MSHR until resp_ is free.
    if (resp_ || hit_pend_.valid) return;
    resp_ = buildResp(m.req_pc, m.line);
    m = Mshr{};
}

void ICache::tickEnd() {
    // RTL order: emit hit_pend, then compare pipe, then fills.
    if (hit_pend_.valid) {
        if (hit_pend_.cycles_left > 0) hit_pend_.cycles_left--;
        if (hit_pend_.cycles_left == 0) {
            if (hit_pend_.drop) {
                hit_pend_ = HitPend{};
            } else if (!resp_) {
                resp_ = buildResp(hit_pend_.pc, hit_pend_.line);
                hit_pend_ = HitPend{};
            }
            // else hold hit_pend until resp_ is popped
        }
    }

    processPipe();

    for (int i = 0; i < mshr_count_; ++i) {
        if (mshrs_[static_cast<size_t>(i)].valid &&
            mshrs_[static_cast<size_t>(i)].fill_ready) {
            completeMshr(i);
        }
    }
}

std::optional<ImemResp> ICache::popCpuResp() {
    if (!resp_) return std::nullopt;
    auto r = *resp_;
    resp_.reset();
    return r;
}
