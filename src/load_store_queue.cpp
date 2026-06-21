#include "load_store_queue.hpp"

std::vector<int> MainMemory(1024, 0);

LoadStoreQueue::LoadStoreQueue(int size, int cdb_ports) : capacity(size), cdb_bandwidth(cdb_ports) {
    queue.resize(capacity);
    for (int i = 0; i < capacity; i++) {
        queue[i].valid = false;
    }
}

bool LoadStoreQueue::allocate(uint32_t id, int rob_id, bool is_load) {
    for (int i = 0; i < capacity; i++) {
        if (!queue[i].valid) {
            queue[i].valid = true;
            queue[i].is_load = is_load;
            queue[i].inst_id = id;
            queue[i].rob_id = rob_id;

            queue[i].addr_ready = false;
            queue[i].address = 0;

            queue[i].data_ready = false;
            queue[i].data = 0;

            queue[i].executed = false;
            queue[i].is_accessing_memory = false;
            queue[i].cycles_remaining = 0;
            queue[i].memory_data_buffer = 0;
            return true;
        }
    }
    return false;
}

bool LoadStoreQueue::hasSpace() const {
    for (int i = 0; i < capacity; i++) {
        if (!queue[i].valid) {
            return true;
        }
    }
    return false;
}

bool LoadStoreQueue::setAddress(uint32_t id, int addr) {
    int store_index = -1;
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].inst_id == id) {
            queue[i].address = addr;
            queue[i].addr_ready = true;
            store_index = i;
            break;
        }
    }

    if (store_index != -1 && !queue[store_index].is_load) {
        for (int i = 0; i < capacity; i++) {
            if (queue[i].valid && queue[i].is_load && queue[i].inst_id > id) {
                if ((queue[i].executed || queue[i].is_accessing_memory) && queue[i].address == addr) {
                    return true;
                }
            }
        }
    }

    return false;
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

    uint32_t load_age = queue[load_index].inst_id;
    int target_addr = queue[load_index].address;

    bool found_forwarding_store = false;
    uint32_t youngest_older_store_age = 0;
    int forwarded_data = 0;

    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && !queue[i].is_load && queue[i].inst_id < load_age) {
            if (queue[i].addr_ready && queue[i].address == target_addr) {
                if (!found_forwarding_store || queue[i].inst_id > youngest_older_store_age) {
                    found_forwarding_store = true;
                    youngest_older_store_age = queue[i].inst_id;

                    if (!queue[i].data_ready) {
                        return false;
                    }
                    forwarded_data = queue[i].data;
                }
            }
        }
    }

    queue[load_index].is_accessing_memory = true;

    if (found_forwarding_store) {
        queue[load_index].cycles_remaining = 1;
        queue[load_index].memory_data_buffer = forwarded_data;
    } else {
        queue[load_index].cycles_remaining = 5;
        queue[load_index].memory_data_buffer = MainMemory[target_addr];
    }

    return true;
}

std::vector<CDBResult> LoadStoreQueue::tick() {
    std::vector<CDBResult> broadcasts;

    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].is_load && queue[i].is_accessing_memory) {
            if (queue[i].cycles_remaining > 0) {
                queue[i].cycles_remaining--;
            }
        }
    }

    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].is_load && queue[i].is_accessing_memory) {
            if (queue[i].cycles_remaining == 0) {
                if (broadcasts.size() < static_cast<size_t>(cdb_bandwidth)) {
                    broadcasts.push_back({queue[i].rob_id, queue[i].memory_data_buffer});
                    queue[i].executed = true;
                    queue[i].is_accessing_memory = false;
                }
            }
        }
    }

    return broadcasts;
}

void LoadStoreQueue::commitStore(uint32_t store_id) {
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].inst_id == store_id && !queue[i].is_load) {
            if (queue[i].addr_ready && queue[i].data_ready) {
                MainMemory[queue[i].address] = queue[i].data;
                queue[i].valid = false;
            }
            return;
        }
    }
}

void LoadStoreQueue::freeLoad(uint32_t load_id) {
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid && queue[i].inst_id == load_id && queue[i].is_load) {
            queue[i].valid = false;
            return;
        }
    }
}

void LoadStoreQueue::printState() const {
    std::cout << "LSQ State:\n";
    for (int i = 0; i < capacity; i++) {
        if (queue[i].valid) {
            std::cout << "  " << (queue[i].is_load ? "LOAD " : "STORE")
                      << " ID:" << queue[i].inst_id
                      << " | Addr: " << (queue[i].addr_ready ? std::to_string(queue[i].address) : "Pending");

            if (queue[i].is_load) {
                std::cout << " | Mem Accessing: " << (queue[i].is_accessing_memory ? "Yes" : "No")
                          << " | Cycles Left: " << queue[i].cycles_remaining;
            }
            std::cout << "\n";
        }
    }
    std::cout << "-----------------------\n";
}
