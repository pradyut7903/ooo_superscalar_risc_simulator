#include "reorder_buffer.hpp"

#include <cassert>

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
    assert(allocated_rob_id >= 0 && allocated_rob_id < capacity);
    assert(!buffer[allocated_rob_id].busy && "dispatching into an occupied ROB slot");

    buffer[allocated_rob_id].busy = true;
    buffer[allocated_rob_id].op = operation;
    buffer[allocated_rob_id].dest_reg = logical_dest_reg;
    buffer[allocated_rob_id].done = false;

    tail = (tail + 1) % capacity;
    active_count++;
    assert(active_count >= 0 && active_count <= capacity && "ROB occupancy out of range");

    return allocated_rob_id;
}

bool ReorderBuffer::undispatch(int rob_id) {
    if (active_count == 0) return false;
    const int last = (tail - 1 + capacity) % capacity;
    if (rob_id != last || !buffer[static_cast<size_t>(rob_id)].busy) return false;
    buffer[static_cast<size_t>(rob_id)].busy = false;
    buffer[static_cast<size_t>(rob_id)].done = false;
    tail = last;
    --active_count;
    return true;
}

bool ReorderBuffer::hasSpace() const {
    return active_count < capacity;
}

void ReorderBuffer::writeResult(int rob_id, int computed_value) {
    assert(rob_id >= 0 && rob_id < capacity && "writeResult rob_id out of range");
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

int ReorderBuffer::activeCount() const {
    return active_count;
}

int ReorderBuffer::ageFromHead(int tag) const {
    assert(tag >= 0 && tag < capacity);
    return (tag - head + capacity) % capacity;
}

bool ReorderBuffer::isYounger(int tag_a, int tag_b) const {
    return ageFromHead(tag_a) > ageFromHead(tag_b);
}

int ReorderBuffer::squashYoungerThan(int squash_tag) {
    assert(squash_tag >= 0 && squash_tag < capacity);
    assert(buffer[squash_tag].busy && "squash tag must be in-flight");

    int freed = 0;
    for (int i = 0; i < capacity; ++i) {
        if (buffer[i].busy && isYounger(i, squash_tag)) {
            buffer[i].busy = false;
            buffer[i].done = false;
            ++freed;
        }
    }

    // Survivors are head .. squash_tag inclusive.
    const int survivors = ageFromHead(squash_tag) + 1;
    tail = (squash_tag + 1) % capacity;
    active_count = survivors;
    assert(active_count >= 0 && active_count <= capacity);

    int busy = 0;
    for (int i = 0; i < capacity; ++i) {
        if (buffer[i].busy) ++busy;
    }
    assert(busy == active_count && "squashYoungerThan occupancy mismatch");
    return freed;
}

void ReorderBuffer::checkInvariant() const {
    // active_count must equal the number of busy slots, and head/tail must be valid.
    assert(active_count >= 0 && active_count <= capacity);
    assert(head >= 0 && head < capacity && tail >= 0 && tail < capacity);
    int busy = 0;
    for (int i = 0; i < capacity; ++i) {
        if (buffer[i].busy) ++busy;
    }
    assert(busy == active_count && "ROB active_count disagrees with busy slot count");
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
