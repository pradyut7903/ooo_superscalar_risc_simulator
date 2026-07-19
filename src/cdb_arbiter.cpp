#include "cdb_arbiter.hpp"

#include <cassert>
#include <unordered_set>

CdbArbiter::CdbArbiter(int cdb_width, int num_producers)
    : cdb_width_(cdb_width), num_producers_(num_producers), rr_ptr_(0) {
    assert(cdb_width_ > 0);
    assert(num_producers_ > 0);
    pending_valid_.assign(static_cast<size_t>(num_producers_), false);
    pending_.assign(static_cast<size_t>(num_producers_), CdbProducer{});
    last_grant_.assign(static_cast<size_t>(num_producers_), false);
}

void CdbArbiter::reset() {
    rr_ptr_ = 0;
    pending_valid_.assign(static_cast<size_t>(num_producers_), false);
    pending_.assign(static_cast<size_t>(num_producers_), CdbProducer{});
    last_grant_.assign(static_cast<size_t>(num_producers_), false);
}

void CdbArbiter::invalidateTags(const std::vector<int>& tags) {
    std::unordered_set<int> kill(tags.begin(), tags.end());
    for (int i = 0; i < num_producers_; ++i) {
        if (!pending_valid_[static_cast<size_t>(i)]) continue;
        if (kill.count(pending_[static_cast<size_t>(i)].rob_id)) {
            pending_valid_[static_cast<size_t>(i)] = false;
            pending_[static_cast<size_t>(i)] = CdbProducer{};
        }
    }
}

std::vector<CDBResult> CdbArbiter::selectGrants(const std::vector<CdbProducer>& producers,
                                                std::vector<int>& granted_indices,
                                                std::vector<CdbProducer>& granted_lanes,
                                                std::vector<bool>& producer_ready) {
    granted_indices.clear();
    granted_lanes.clear();
    producer_ready.assign(static_cast<size_t>(num_producers_), false);
    last_grant_.assign(static_cast<size_t>(num_producers_), false);
    std::vector<CDBResult> out;

    assert(static_cast<int>(producers.size()) == num_producers_);
    if (cdb_width_ <= 0) return out;

    std::vector<CdbProducer> lane(static_cast<size_t>(num_producers_));
    std::vector<bool> request(static_cast<size_t>(num_producers_), false);
    for (int i = 0; i < num_producers_; ++i) {
        if (pending_valid_[static_cast<size_t>(i)]) {
            lane[static_cast<size_t>(i)] = pending_[static_cast<size_t>(i)];
            request[static_cast<size_t>(i)] = true;
        } else if (producers[static_cast<size_t>(i)].valid) {
            lane[static_cast<size_t>(i)] = producers[static_cast<size_t>(i)];
            request[static_cast<size_t>(i)] = true;
        }
    }

    if (rr_ptr_ >= num_producers_) rr_ptr_ = 0;

    std::vector<bool> selected(static_cast<size_t>(num_producers_), false);
    for (int slot = 0; slot < cdb_width_; ++slot) {
        bool found = false;
        for (int offset = 0; offset < num_producers_; ++offset) {
            const int idx = (rr_ptr_ + offset) % num_producers_;
            if (request[static_cast<size_t>(idx)] && !selected[static_cast<size_t>(idx)]) {
                selected[static_cast<size_t>(idx)] = true;
                last_grant_[static_cast<size_t>(idx)] = true;
                granted_indices.push_back(idx);
                granted_lanes.push_back(lane[static_cast<size_t>(idx)]);
                out.push_back({lane[static_cast<size_t>(idx)].rob_id,
                               lane[static_cast<size_t>(idx)].value});
                rr_ptr_ = (idx + 1) % num_producers_;
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    for (int i = 0; i < num_producers_; ++i) {
        producer_ready[static_cast<size_t>(i)] = !pending_valid_[static_cast<size_t>(i)];
        if (last_grant_[static_cast<size_t>(i)]) {
            producer_ready[static_cast<size_t>(i)] = true;
        }
    }
    return out;
}

void CdbArbiter::commitPending(const std::vector<CdbProducer>& producers_after,
                               const std::vector<int>& /*granted_indices*/) {
    assert(static_cast<int>(producers_after.size()) == num_producers_);
    for (int i = 0; i < num_producers_; ++i) {
        const bool granted = last_grant_[static_cast<size_t>(i)];
        const bool prod_valid = producers_after[static_cast<size_t>(i)].valid;
        if (pending_valid_[static_cast<size_t>(i)]) {
            if (granted) {
                if (prod_valid) {
                    pending_[static_cast<size_t>(i)] = producers_after[static_cast<size_t>(i)];
                    pending_valid_[static_cast<size_t>(i)] = true;
                } else {
                    pending_valid_[static_cast<size_t>(i)] = false;
                    pending_[static_cast<size_t>(i)] = CdbProducer{};
                }
            }
        } else if (prod_valid && !granted) {
            pending_valid_[static_cast<size_t>(i)] = true;
            pending_[static_cast<size_t>(i)] = producers_after[static_cast<size_t>(i)];
        }
    }
}
