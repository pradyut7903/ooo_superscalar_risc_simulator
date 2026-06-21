#include "reservation_station.hpp"

ReservationStation::ReservationStation(int size) : capacity(size) {
    stations.resize(size);
    for (int i = 0; i < size; i++) {
        stations[i].valid = false;
    }
}

bool ReservationStation::allocate(uint32_t id, bool rdy1, int tag1, int val1, bool rdy2, int tag2, int val2) {
    for (int i = 0; i < capacity; i++) {
        if (!stations[i].valid) {
            stations[i].valid = true;
            stations[i].inst_id = id;

            stations[i].src1_ready = rdy1;
            stations[i].src1_tag = tag1;
            stations[i].src1_value = val1;

            stations[i].src2_ready = rdy2;
            stations[i].src2_tag = tag2;
            stations[i].src2_value = val2;

            return true;
        }
    }
    return false;
}

bool ReservationStation::hasSpace() const {
    for (int i = 0; i < capacity; i++) {
        if (!stations[i].valid) {
            return true;
        }
    }
    return false;
}

void ReservationStation::syncOperands(uint32_t id, bool rdy1, int tag1, int val1, bool rdy2, int tag2, int val2) {
    for (int i = 0; i < capacity; i++) {
        if (stations[i].valid && stations[i].inst_id == id) {
            stations[i].src1_ready = rdy1;
            stations[i].src1_tag = tag1;
            stations[i].src1_value = val1;
            stations[i].src2_ready = rdy2;
            stations[i].src2_tag = tag2;
            stations[i].src2_value = val2;
            return;
        }
    }
}

void ReservationStation::updateFromCDB(int broadcast_tag, int broadcast_value) {
    for (int i = 0; i < capacity; i++) {
        if (stations[i].valid) {
            if (!stations[i].src1_ready && stations[i].src1_tag == broadcast_tag) {
                stations[i].src1_ready = true;
                stations[i].src1_value = broadcast_value;
            }
            if (!stations[i].src2_ready && stations[i].src2_tag == broadcast_tag) {
                stations[i].src2_ready = true;
                stations[i].src2_value = broadcast_value;
            }
        }
    }
}

int ReservationStation::peekReadyInstruction() const {
    for (int i = 0; i < capacity; i++) {
        if (stations[i].valid && stations[i].src1_ready && stations[i].src2_ready) {
            return static_cast<int>(stations[i].inst_id);
        }
    }
    return -1;
}

std::vector<uint32_t> ReservationStation::collectReadyInstructions() const {
    std::vector<uint32_t> ready;
    for (int i = 0; i < capacity; i++) {
        if (stations[i].valid && stations[i].src1_ready && stations[i].src2_ready) {
            ready.push_back(stations[i].inst_id);
        }
    }
    return ready;
}

void ReservationStation::removeInstruction(uint32_t id) {
    for (int i = 0; i < capacity; i++) {
        if (stations[i].valid && stations[i].inst_id == id) {
            stations[i].valid = false;
            return;
        }
    }
}

int ReservationStation::issueReadyInstruction() {
    int ready_id = peekReadyInstruction();
    if (ready_id >= 0) {
        removeInstruction(static_cast<uint32_t>(ready_id));
    }
    return ready_id;
}

void ReservationStation::printState() const {
    std::cout << "RS State:\n";
    for (int i = 0; i < capacity; i++) {
        if (stations[i].valid) {
            std::cout << "Slot " << i << " | ID: " << stations[i].inst_id
                      << " | Src1 Rdy: " << stations[i].src1_ready
                      << " | Src2 Rdy: " << stations[i].src2_ready << "\n";
        }
    }
    std::cout << "-----------------------\n";
}
