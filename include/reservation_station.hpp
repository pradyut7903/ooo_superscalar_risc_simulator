#pragma once

#include <cstdint>
#include <iostream>
#include <vector>

struct RSEntry {
    bool valid;
    uint32_t inst_id;

    bool src1_ready;
    int src1_tag;
    int src1_value;

    bool src2_ready;
    int src2_tag;
    int src2_value;
};

class ReservationStation {
private:
    std::vector<RSEntry> stations;
    int capacity;

public:
    ReservationStation(int size);

    bool allocate(uint32_t id, bool rdy1, int tag1, int val1, bool rdy2, int tag2, int val2);
    bool hasSpace() const;
    void updateFromCDB(int broadcast_tag, int broadcast_value);
    void syncOperands(uint32_t id, bool rdy1, int tag1, int val1, bool rdy2, int tag2, int val2);
    int peekReadyInstruction() const;
    std::vector<uint32_t> collectReadyInstructions() const;
    void removeInstruction(uint32_t id);
    int issueReadyInstruction();
    // Drop any RS entries whose inst_id is in the set (early recovery).
    void squashInstIds(const std::vector<uint32_t>& ids);
    void printState() const;
};
