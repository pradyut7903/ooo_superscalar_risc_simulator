#include "reorder_buffer.hpp"

ReorderBuffer::ReorderBuffer(int size) : capacity(size), head(0), tail(0), active_count(0) {
    buffer.resize(capacity);
    for (int i = 0; i < capacity; i++) {
        buffer[i].busy = false;
        buffer[i].done = false;
    }
}

int ReorderBuffer::dispatch(std::string operation, int logical_dest_reg) {
    if (active_count == capacity) {
        return -1;
    }

    int allocated_rob_id = tail;

    buffer[allocated_rob_id].busy = true;
    buffer[allocated_rob_id].op = operation;
    buffer[allocated_rob_id].dest_reg = logical_dest_reg;
    buffer[allocated_rob_id].done = false;

    tail = (tail + 1) % capacity;
    active_count++;

    return allocated_rob_id;
}

bool ReorderBuffer::hasSpace() const {
    return active_count < capacity;
}

void ReorderBuffer::writeResult(int rob_id, int computed_value) {
    if (buffer[rob_id].busy) {
        buffer[rob_id].value = computed_value;
        buffer[rob_id].done = true;
    }
}

bool ReorderBuffer::commit(int& out_dest_reg, int& out_value, int& out_rob_id) {
    if (active_count == 0) {
        return false;
    }

    if (buffer[head].done) {
        out_dest_reg = buffer[head].dest_reg;
        out_value = buffer[head].value;
        out_rob_id = head;

        buffer[head].busy = false;
        buffer[head].done = false;

        head = (head + 1) % capacity;
        active_count--;

        return true;
    }

    return false;
}

bool ReorderBuffer::isReady(int rob_id) const {
    return buffer[rob_id].done;
}

int ReorderBuffer::getValue(int rob_id) const {
    return buffer[rob_id].value;
}

bool ReorderBuffer::isEmpty() const {
    return active_count == 0;
}

void ReorderBuffer::printState() const {
    std::cout << "ROB State (Head: " << head << ", Tail: " << tail
              << ", Active: " << active_count << "/" << capacity << ")\n";
    for (int i = 0; i < capacity; i++) {
        if (buffer[i].busy) {
            std::cout << "  ROB" << i << " | Op: " << buffer[i].op
                      << " | Dest ARF: R" << buffer[i].dest_reg
                      << " | Value: ";
            if (buffer[i].done) std::cout << buffer[i].value;
            else std::cout << "Pending";
            std::cout << " | Done: " << (buffer[i].done ? "Yes" : "No") << "\n";
        }
    }
    std::cout << "-----------------------\n";
}
