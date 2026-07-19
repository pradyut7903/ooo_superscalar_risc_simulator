#pragma once

#include "types.hpp"

#include <vector>

// One ready producer lane offering a result to the CDB.
struct CdbProducer {
    bool valid = false;
    int rob_id = -1;
    int value = 0;
    // Opaque handle so the owner can release the slot after a grant.
    // Convention: EE uses unit index; LSQ uses queue index.
    int source = 0;      // 0 = EE, 1 = LSQ
    int slot = -1;
};

// Fixed-lane CDB arbiter matching rtl_v2/cdb_arbiter.sv.
class CdbArbiter {
public:
    CdbArbiter(int cdb_width, int num_producers);

    void reset();
    // Drop pending beats whose rob_id is in tags (early recovery).
    void invalidateTags(const std::vector<int>& tags);

    // Select grants from pending ⊕ producers (fixed layout).
    // producer_ready[i] is the lane's cdb_ready / in_ready.
    // granted_lanes[k] is the exact payload granted (pending or live) for release.
    std::vector<CDBResult> selectGrants(const std::vector<CdbProducer>& producers,
                                        std::vector<int>& granted_indices,
                                        std::vector<CdbProducer>& granted_lanes,
                                        std::vector<bool>& producer_ready);

    // After FU/LSQ consume granted outputs, update pending from the new snapshot.
    void commitPending(const std::vector<CdbProducer>& producers_after,
                       const std::vector<int>& granted_indices);

    int numProducers() const { return num_producers_; }

private:
    int cdb_width_;
    int num_producers_;
    int rr_ptr_;
    std::vector<bool> pending_valid_;
    std::vector<CdbProducer> pending_;
    std::vector<bool> last_grant_;
};
