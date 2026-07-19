#include "load_store_queue.hpp"

#include "mem/memory_system.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

std::vector<int> MainMemory(65536, 0);
bool MainMemoryByteAddressed = false;

namespace {

size_t memWordIndex(int addr) {
    if (MainMemoryByteAddressed) {
        return static_cast<size_t>(static_cast<uint32_t>(addr) >> 2);
    }
    if (addr < 0) return MainMemory.size();
    return static_cast<size_t>(addr);
}

uint32_t memReadWord(int addr) {
    const size_t idx = memWordIndex(addr);
    if (idx >= MainMemory.size()) return 0;
    return static_cast<uint32_t>(MainMemory[idx]);
}

void memWriteMasked(int addr, uint32_t wdata, uint8_t wstrb) {
    const size_t idx = memWordIndex(addr);
    if (idx >= MainMemory.size()) return;
    uint32_t cur = static_cast<uint32_t>(MainMemory[idx]);
    for (int b = 0; b < 4; ++b) {
        if (wstrb & (1u << b)) {
            const uint32_t mask = 0xFFu << (8 * b);
            cur = (cur & ~mask) | (wdata & mask);
        }
    }
    MainMemory[idx] = static_cast<int>(cur);
}

int addrLow(int addr) {
    if (!MainMemoryByteAddressed) return 0;
    return static_cast<int>(static_cast<uint32_t>(addr) & 0x3u);
}

bool sameWord(int a, int b) {
    if (!MainMemoryByteAddressed) return a == b;
    return (static_cast<uint32_t>(a) >> 2) == (static_cast<uint32_t>(b) >> 2);
}

uint8_t accessMask(MemSize size, int addr) {
    if (!MainMemoryByteAddressed) return 0xF;
    const int low = addrLow(addr);
    switch (size) {
        case MemSize::B: return static_cast<uint8_t>(0x1u << low);
        case MemSize::H:
            // Within-word halfword; odd addrs use true byte lanes (low=3 truncated).
            if (low >= 3) return 0x8u;
            return static_cast<uint8_t>(0x3u << low);
        case MemSize::W:
        default:         return 0xFu;
    }
}

uint32_t storeWdata(uint32_t value, MemSize size, int addr) {
    if (!MainMemoryByteAddressed) return value;
    const int low = addrLow(addr);
    switch (size) {
        case MemSize::B: {
            const uint32_t b = value & 0xFFu;
            return b << (8 * low);
        }
        case MemSize::H: {
            const uint32_t h = value & 0xFFFFu;
            if (low >= 3) return (h & 0xFFu) << 24;
            return h << (8 * low);
        }
        case MemSize::W:
        default:
            return value;
    }
}

int32_t loadExtend(uint32_t word, MemSize size, bool is_unsigned, int addr) {
    if (!MainMemoryByteAddressed) return static_cast<int32_t>(word);
    const int low = addrLow(addr);
    switch (size) {
        case MemSize::B: {
            const uint8_t b = static_cast<uint8_t>(word >> (8 * low));
            if (is_unsigned) return static_cast<int32_t>(b);
            return static_cast<int32_t>(static_cast<int8_t>(b));
        }
        case MemSize::H: {
            uint16_t h = 0;
            if (low >= 3) {
                h = static_cast<uint16_t>((word >> 24) & 0xFFu);
            } else {
                h = static_cast<uint16_t>((word >> (8 * low)) & 0xFFFFu);
            }
            if (is_unsigned) return static_cast<int32_t>(h);
            return static_cast<int32_t>(static_cast<int16_t>(h));
        }
        case MemSize::W:
        default:
            return static_cast<int32_t>(word);
    }
}

// Cached hierarchy always uses byte addresses; toy traces use word indices.
uint32_t toMemsysAddr(int addr) {
    if (MainMemoryByteAddressed) return static_cast<uint32_t>(addr);
    return static_cast<uint32_t>(addr) * 4u;
}

uint32_t mergeBytes(uint32_t base, uint32_t overlay, uint8_t mask) {
    uint32_t out = base;
    for (int b = 0; b < 4; ++b) {
        if (mask & (1u << b)) {
            const uint32_t m = 0xFFu << (8 * b);
            out = (out & ~m) | (overlay & m);
        }
    }
    return out;
}

}  // namespace

LoadStoreQueue::LoadStoreQueue(int size, int num_load_cdb_ports, int store_buf_depth,
                               int memory_latency, int forward_latency,
                               bool enable_forwarding, int max_memory_ports)
    : capacity(size), num_cdb_ports(num_load_cdb_ports),
      mem_latency(memory_latency), fwd_latency(forward_latency),
      store_forwarding(enable_forwarding), max_mem_ports(max_memory_ports),
      sb_capacity(store_buf_depth > 0 ? store_buf_depth : 1) {
    queue.resize(static_cast<size_t>(capacity));
    sb.resize(static_cast<size_t>(sb_capacity));
    sb_head = sb_tail = sb_count = 0;
}

bool LoadStoreQueue::usesCachedMem() const {
    return memsys_ != nullptr && memsys_->kind() == MemSystem::Cached;
}

bool LoadStoreQueue::allocate(uint32_t id, int rob_id, bool is_load,
                              MemSize mem_size, bool mem_unsigned) {
    for (int i = 0; i < capacity; i++) {
        if (!queue[i].valid) {
            queue[i] = LSQEntry{};
            queue[i].valid = true;
            queue[i].is_load = is_load;
            queue[i].inst_id = id;
            queue[i].rob_id = rob_id;
            queue[i].age = next_age_++;
            queue[i].mem_size = mem_size;
            queue[i].mem_unsigned = mem_unsigned;
            return true;
        }
    }
    return false;
}

bool LoadStoreQueue::hasSpace() const {
    for (int i = 0; i < capacity; i++) {
        if (!queue[i].valid) return true;
    }
    return false;
}

void LoadStoreQueue::setAddress(uint32_t id, int addr) {
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].inst_id == id) {
            queue[i].address = addr;
            queue[i].addr_ready = true;
            return;
        }
    }
}

void LoadStoreQueue::setStoreData(uint32_t id, int data) {
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].inst_id == id && !queue[i].is_load) {
            queue[i].data = data;
            queue[i].data_ready = true;
            return;
        }
    }
}

bool LoadStoreQueue::dispatchLoad(uint32_t load_id) {
    int load_index = -1;
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].inst_id == load_id) {
            load_index = i;
            break;
        }
    }

    if (load_index == -1 || !queue[load_index].addr_ready ||
        queue[load_index].is_accessing_memory || queue[load_index].executed) {
        return false;
    }

    LSQEntry& ld = queue[load_index];
    const uint32_t load_age = ld.inst_id;
    const int load_addr = ld.address;
    const uint8_t load_mask = accessMask(ld.mem_size, load_addr);

    bool blocked = false;
    bool any_overlap = false;
    uint8_t covered_mask = 0;
    uint32_t fwd_word = 0;
    uint32_t byte_age[4] = {0, 0, 0, 0};

    for (int j = 0; j < capacity; j++) {
        if (!queue[j].valid || queue[j].is_load || queue[j].inst_id >= load_age) {
            continue;
        }
        if (!queue[j].addr_ready || !queue[j].data_ready) {
            blocked = true;
            continue;
        }
        const int store_addr = queue[j].address;
        const uint8_t smask = accessMask(queue[j].mem_size, store_addr);
        const uint8_t overlap = sameWord(store_addr, load_addr) ? (smask & load_mask) : 0;
        if (overlap == 0) continue;

        any_overlap = true;
        const uint32_t swdata = storeWdata(static_cast<uint32_t>(queue[j].data),
                                           queue[j].mem_size, store_addr);
        for (int b = 0; b < 4; ++b) {
            if (!(overlap & (1u << b))) continue;
            if (!(covered_mask & (1u << b)) || queue[j].inst_id > byte_age[b]) {
                fwd_word = mergeBytes(fwd_word, swdata, static_cast<uint8_t>(1u << b));
                byte_age[b] = queue[j].inst_id;
                covered_mask = static_cast<uint8_t>(covered_mask | (1u << b));
            }
        }
    }

    if (blocked) {
        return false;
    }

    const uint8_t lsq_cover = covered_mask;

    for (int s = 0; s < sb_count; ++s) {
        const int sb_i = (sb_head + s) % sb_capacity;
        if (!sb[static_cast<size_t>(sb_i)].valid) continue;
        const auto& sbe = sb[static_cast<size_t>(sb_i)];
        const uint8_t overlap = sameWord(sbe.address, load_addr) ? (sbe.wstrb & load_mask) : 0;
        if (overlap == 0) continue;
        any_overlap = true;
        for (int b = 0; b < 4; ++b) {
            if ((overlap & (1u << b)) && !(lsq_cover & (1u << b))) {
                fwd_word = mergeBytes(fwd_word, sbe.wdata, static_cast<uint8_t>(1u << b));
                covered_mask = static_cast<uint8_t>(covered_mask | (1u << b));
            }
        }
    }

    auto start_full_fwd = [&](uint32_t word) {
        ld.is_accessing_memory = true;
        ld.cycles_remaining = fwd_latency;
        ld.needs_mem_fill = false;
        ld.partial_fwd_en = false;
        ld.awaiting_mem_resp = false;
        ld.mem_req_sent = false;
        ld.cdb_wait = (fwd_latency <= 0);  // else enter CDB_WAIT when countdown hits 0
        ld.memory_data_buffer = loadExtend(word, ld.mem_size, ld.mem_unsigned, load_addr);
    };

    auto start_mem = [&](uint32_t partial_word, uint8_t partial_mask, bool has_partial) {
        ld.is_accessing_memory = true;
        ld.memory_data_buffer = 0;
        ld.partial_fwd_en = has_partial;
        ld.partial_fwd_word = partial_word;
        ld.partial_fwd_mask = partial_mask;
        if (usesCachedMem()) {
            ld.cycles_remaining = 0;
            ld.needs_mem_fill = false;
            ld.awaiting_mem_resp = true;
            ld.mem_req_sent = false;
        } else {
            ld.cycles_remaining = mem_latency;
            ld.needs_mem_fill = true;
            ld.awaiting_mem_resp = false;
            ld.mem_req_sent = false;
        }
    };

    if (!store_forwarding) {
        for (int j = 0; j < capacity; j++) {
            if (!queue[j].valid || queue[j].is_load || queue[j].inst_id >= load_age) {
                continue;
            }
            if (!queue[j].addr_ready || sameWord(queue[j].address, load_addr)) {
                return false;
            }
        }
        covered_mask = 0;
        fwd_word = 0;
        any_overlap = false;
        for (int s = 0; s < sb_count; ++s) {
            const int sb_i = (sb_head + s) % sb_capacity;
            if (!sb[static_cast<size_t>(sb_i)].valid) continue;
            const auto& sbe = sb[static_cast<size_t>(sb_i)];
            const uint8_t overlap = sameWord(sbe.address, load_addr) ? (sbe.wstrb & load_mask) : 0;
            if (overlap == 0) continue;
            any_overlap = true;
            for (int b = 0; b < 4; ++b) {
                if (overlap & (1u << b)) {
                    fwd_word = mergeBytes(fwd_word, sbe.wdata, static_cast<uint8_t>(1u << b));
                    covered_mask = static_cast<uint8_t>(covered_mask | (1u << b));
                }
            }
        }
        if ((covered_mask & load_mask) == load_mask) {
            start_full_fwd(fwd_word);
        } else {
            start_mem(fwd_word, static_cast<uint8_t>(covered_mask & load_mask),
                      any_overlap && (covered_mask & load_mask) != 0);
        }
        return true;
    }

    if ((covered_mask & load_mask) == load_mask) {
        start_full_fwd(fwd_word);
        return true;
    }

    start_mem(fwd_word, static_cast<uint8_t>(covered_mask & load_mask), any_overlap);
    return true;
}

std::vector<int> LoadStoreQueue::enqueueStores(const std::vector<int>& commit_rob_tags) {
    std::vector<int> completed;
    int enqueued = 0;
    for (int rob_tag : commit_rob_tags) {
        if (enqueued >= max_mem_ports || sb_count >= sb_capacity) break;

        int idx = -1;
        for (int i = 0; i < capacity; i++) {
            if (queue[i].valid && !queue[i].is_load && queue[i].rob_id == rob_tag) {
                idx = i;
                break;
            }
        }
        // FIFO: stop at the first commit-permitted store that cannot enqueue.
        if (idx < 0 || !queue[idx].addr_ready || !queue[idx].data_ready) break;

        auto& e = sb[static_cast<size_t>(sb_tail)];
        e.valid = true;
        e.issued = false;
        e.address = queue[idx].address;
        e.wdata = storeWdata(static_cast<uint32_t>(queue[idx].data),
                             queue[idx].mem_size, queue[idx].address);
        e.wstrb = accessMask(queue[idx].mem_size, queue[idx].address);
        e.cycles_remaining = 0;

        sb_tail = (sb_tail + 1) % sb_capacity;
        ++sb_count;

        completed.push_back(rob_tag);
        queue[idx].valid = false;
        ++enqueued;
    }
    return completed;
}

void LoadStoreQueue::issueMemoryRequests() {
    const bool cached = usesCachedMem();
    int req_lane = 0;

    for (int s = 0; s < sb_count && req_lane < max_mem_ports; ++s) {
        const int sb_i = (sb_head + s) % sb_capacity;
        auto& e = sb[static_cast<size_t>(sb_i)];
        if (!e.valid || e.issued) continue;

        if (cached) {
            DataMemReq req;
            req.write = true;
            req.addr = toMemsysAddr(e.address);
            req.wdata = e.wdata;
            req.wstrb = e.wstrb;
            req.id = capacity + sb_i;
            req.cookie = 0;
            if (!memsys_->tryDataReq(req)) break;
            e.issued = true;
            e.awaiting_mem_resp = true;
        } else {
            e.issued = true;
            e.cycles_remaining = mem_latency;
        }
        ++req_lane;
    }

    if (!cached) return;

    std::vector<int> load_idxs;
    for (int i = 0; i < capacity; ++i) {
        const auto& e = queue[i];
        if (!e.valid || !e.is_load || !e.awaiting_mem_resp || e.mem_req_sent) continue;
        load_idxs.push_back(i);
    }
    std::sort(load_idxs.begin(), load_idxs.end(), [this](int a, int b) {
        return ageBefore(queue[static_cast<size_t>(a)].age,
                         queue[static_cast<size_t>(b)].age);
    });
    for (int idx : load_idxs) {
        if (req_lane >= max_mem_ports) break;
        auto& e = queue[static_cast<size_t>(idx)];
        DataMemReq req;
        req.write = false;
        req.addr = toMemsysAddr(e.address);
        req.id = idx;
        req.cookie = e.inst_id;
        if (!memsys_->tryDataReq(req)) break;
        e.mem_req_sent = true;
        e.mem_cookie = e.inst_id;
        ++req_lane;
    }
}

void LoadStoreQueue::tick() {
    const bool cached = usesCachedMem();

    // Ideal loads: countdown, then mem fill + partial merge.
    if (!cached) {
        for (int i = 0; i < capacity; i++) {
            auto& e = queue[i];
            if (!e.valid || !e.is_load || !e.is_accessing_memory) continue;
            if (e.cycles_remaining > 0) {
                e.cycles_remaining--;
            }
            assert(e.cycles_remaining >= 0);

            if (e.cycles_remaining == 0 && e.needs_mem_fill && !e.executed) {
                uint32_t word = memReadWord(e.address);
                if (e.partial_fwd_en) {
                    word = mergeBytes(word, e.partial_fwd_word, e.partial_fwd_mask);
                }
                e.memory_data_buffer = loadExtend(word, e.mem_size, e.mem_unsigned, e.address);
                e.needs_mem_fill = false;
                e.partial_fwd_en = false;
            }
        }
    } else {
        // Cached: countdown store-forward latency → ST_CDB_WAIT.
        for (int i = 0; i < capacity; i++) {
            auto& e = queue[i];
            if (!e.valid || !e.is_load || !e.is_accessing_memory) continue;
            if (e.awaiting_mem_resp || e.cdb_wait) continue;
            if (e.cycles_remaining > 0) {
                e.cycles_remaining--;
                if (e.cycles_remaining == 0) e.cdb_wait = true;
            }
        }
    }

    issueMemoryRequests();

    if (!cached) {
        // Complete in head→tail order so overlapping stores keep commit order.
        for (int s = 0; s < sb_count; ++s) {
            const int sb_i = (sb_head + s) % sb_capacity;
            auto& e = sb[static_cast<size_t>(sb_i)];
            if (!e.valid || !e.issued) continue;
            if (e.cycles_remaining > 0) {
                e.cycles_remaining--;
            }
            if (e.cycles_remaining == 0) {
                memWriteMasked(e.address, e.wdata, e.wstrb);
                e = StoreBufferEntry{};
            }
        }
    }

    for (int k = 0; k < sb_capacity; ++k) {
        if (sb_count == 0) break;
        if (!sb[static_cast<size_t>(sb_head)].valid) {
            sb_head = (sb_head + 1) % sb_capacity;
            --sb_count;
        } else {
            break;
        }
    }
}

void LoadStoreQueue::applyDataResps(const std::vector<DataMemResp>& resps) {
    for (const auto& r : resps) {
        if (r.id >= 0 && r.id < capacity) {
            auto& e = queue[static_cast<size_t>(r.id)];
            // Require issued request + cookie match (reject stale after squash/reuse).
            if (!e.valid || !e.is_load || !e.awaiting_mem_resp || !e.mem_req_sent) continue;
            if (r.cookie != e.mem_cookie) continue;
            uint32_t word = r.rdata;
            if (e.partial_fwd_en) {
                word = mergeBytes(word, e.partial_fwd_word, e.partial_fwd_mask);
            }
            e.memory_data_buffer = loadExtend(word, e.mem_size, e.mem_unsigned, e.address);
            e.awaiting_mem_resp = false;
            e.mem_req_sent = false;
            e.partial_fwd_en = false;
            e.needs_mem_fill = false;
            e.cycles_remaining = 0;
            e.cdb_wait = true;  // RTL ST_CDB_WAIT (offer next cycle)
        } else if (r.id >= capacity && r.id < capacity + sb_capacity) {
            const int sb_i = r.id - capacity;
            auto& e = sb[static_cast<size_t>(sb_i)];
            if (e.valid && e.awaiting_mem_resp) {
                e = StoreBufferEntry{};
            }
        }
    }
}

void LoadStoreQueue::promoteCdbWaits() {
    for (int i = 0; i < capacity; ++i) {
        auto& e = queue[static_cast<size_t>(i)];
        if (e.valid && e.is_load && e.cdb_wait) {
            e.cdb_wait = false;
        }
    }
}

std::vector<CdbProducer> LoadStoreQueue::collectProducersFixed() const {
    std::vector<int> ready;
    for (int i = 0; i < capacity; i++) {
        const auto& e = queue[i];
        if (e.valid && e.is_load && e.is_accessing_memory &&
            e.cycles_remaining == 0 && !e.executed && !e.needs_mem_fill &&
            !e.awaiting_mem_resp && !e.cdb_wait) {
            ready.push_back(i);
        }
    }
    std::sort(ready.begin(), ready.end(), [this](int a, int b) {
        return ageBefore(queue[static_cast<size_t>(a)].age,
                         queue[static_cast<size_t>(b)].age);
    });

    std::vector<CdbProducer> out(static_cast<size_t>(num_cdb_ports));
    for (int lane = 0; lane < num_cdb_ports; ++lane) {
        out[static_cast<size_t>(lane)].source = 1;
        if (lane < static_cast<int>(ready.size())) {
            const int idx = ready[static_cast<size_t>(lane)];
            out[static_cast<size_t>(lane)].valid = true;
            out[static_cast<size_t>(lane)].rob_id = queue[static_cast<size_t>(idx)].rob_id;
            out[static_cast<size_t>(lane)].value =
                queue[static_cast<size_t>(idx)].memory_data_buffer;
            out[static_cast<size_t>(lane)].slot = idx;
        }
    }
    return out;
}

std::vector<CdbProducer> LoadStoreQueue::collectProducers() const {
    std::vector<CdbProducer> out;
    for (const auto& p : collectProducersFixed()) {
        if (p.valid) out.push_back(p);
    }
    return out;
}

void LoadStoreQueue::releaseSlot(int queue_index) {
    assert(queue_index >= 0 && queue_index < capacity);
    // Entry may already be gone if a prior CDB grant in this cycle triggered
    // early recovery that squashed younger loads.
    if (!queue[queue_index].valid || !queue[queue_index].is_load) {
        return;
    }
    queue[queue_index].executed = true;
    queue[queue_index].is_accessing_memory = false;
}

void LoadStoreQueue::freeLoad(uint32_t load_id) {
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].inst_id == load_id && queue[i].is_load) {
            queue[i].valid = false;
            return;
        }
    }
}

void LoadStoreQueue::squashInstIds(const std::vector<uint32_t>& ids) {
    for (uint32_t id : ids) {
        for (int i = 0; i < capacity; i++) {
            if (queue[i].valid && queue[i].inst_id == id) {
                queue[i].valid = false;
                break;
            }
        }
    }
}

void LoadStoreQueue::flushSpeculative() {
    for (int i = 0; i < capacity; i++) {
        queue[i].valid = false;
    }
}

bool LoadStoreQueue::isDrained() const {
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid) return false;
    }
    return sb_count == 0;
}

void LoadStoreQueue::printState() const {
    std::cout << "LSQ State:\n";
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid) {
            std::cout << "  " << (queue[i].is_load ? "LOAD " : "STORE")
                      << " ID:" << queue[i].inst_id
                      << " | Addr: "
                      << (queue[i].addr_ready ? std::to_string(queue[i].address) : "Pending");
            if (queue[i].is_load) {
                std::cout << " | Mem: " << (queue[i].is_accessing_memory ? "Yes" : "No")
                          << " | Cycles: " << queue[i].cycles_remaining;
            }
            std::cout << "\n";
        }
    }
    std::cout << "SB (" << sb_count << "/" << sb_capacity << "):\n";
    for (int s = 0; s < sb_count; ++s) {
        const int sb_i = (sb_head + s) % sb_capacity;
        const auto& e = sb[static_cast<size_t>(sb_i)];
        if (!e.valid) continue;
        std::cout << "  SB[" << sb_i << "] addr=" << e.address
                  << " wstrb=0x" << std::hex << static_cast<int>(e.wstrb) << std::dec
                  << (e.issued ? " issued" : " pending")
                  << " cyc=" << e.cycles_remaining << "\n";
    }
    std::cout << "-----------------------\n";
}
