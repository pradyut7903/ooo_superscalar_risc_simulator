#include "mem/dram_simple.hpp"

#include "load_store_queue.hpp"  // MainMemory

#include <cassert>
#include <cstring>

DramSimple::DramSimple(int line_bytes, int outstanding, int latency_cycles)
    : line_bytes_(line_bytes), outstanding_(outstanding),
      latency_(latency_cycles > 0 ? latency_cycles : 1) {
    assert(line_bytes_ > 0 && (line_bytes_ & (line_bytes_ - 1)) == 0);
    slots_.resize(static_cast<size_t>(outstanding_ > 0 ? outstanding_ : 1));
}

void DramSimple::reset() {
    for (auto& s : slots_) s = Slot{};
    accepted_this_cycle_ = false;
}

void DramSimple::tick() {
    accepted_this_cycle_ = false;
    for (auto& s : slots_) {
        if (s.valid && s.cycles_left > 0) {
            s.cycles_left--;
        }
        if (s.valid && s.cycles_left == 0 && s.write) {
            // Commit write to MainMemory at completion (before resp pop).
            const size_t base = static_cast<size_t>(s.line_addr >> 2);
            for (int i = 0; i < line_bytes_; i += 4) {
                const size_t wi = base + static_cast<size_t>(i >> 2);
                if (wi < MainMemory.size()) {
                    uint32_t w = 0;
                    std::memcpy(&w, s.data.data() + i, 4);
                    MainMemory[wi] = static_cast<int>(w);
                }
            }
        }
    }
}

bool DramSimple::tryAccept(bool write, uint32_t line_addr,
                           const std::vector<uint8_t>& wdata, int dram_id,
                           bool from_imem) {
    if (accepted_this_cycle_) return false;
    for (auto& s : slots_) {
        if (s.valid) continue;
        s.valid = true;
        s.write = write;
        s.line_addr = line_addr;
        s.dram_id = dram_id;
        s.cycles_left = latency_;
        s.data.assign(static_cast<size_t>(line_bytes_), 0);
        if (write) {
            assert(static_cast<int>(wdata.size()) >= line_bytes_);
            std::memcpy(s.data.data(), wdata.data(), static_cast<size_t>(line_bytes_));
        } else if (from_imem && imem_ != nullptr) {
            const size_t base = static_cast<size_t>(line_addr >> 2);
            for (int i = 0; i < line_bytes_; i += 4) {
                const size_t wi = base + static_cast<size_t>(i >> 2);
                uint32_t w = (wi < imem_->size()) ? (*imem_)[wi] : 0xFFFFFFFFu;
                std::memcpy(s.data.data() + i, &w, 4);
            }
        } else {
            const size_t base = static_cast<size_t>(line_addr >> 2);
            for (int i = 0; i < line_bytes_; i += 4) {
                const size_t wi = base + static_cast<size_t>(i >> 2);
                uint32_t w = 0;
                if (wi < MainMemory.size()) {
                    w = static_cast<uint32_t>(MainMemory[wi]);
                }
                std::memcpy(s.data.data() + i, &w, 4);
            }
        }
        accepted_this_cycle_ = true;
        return true;
    }
    return false;
}

bool DramSimple::tryPopResp(int& dram_id, std::vector<uint8_t>& rdata) {
    for (auto& s : slots_) {
        if (!s.valid || s.cycles_left != 0) continue;
        dram_id = s.dram_id;
        rdata = s.data;
        s = Slot{};
        return true;
    }
    return false;
}
