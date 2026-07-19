#include "dispatch_reg.hpp"

#include <cassert>

DispatchReg::DispatchReg(int width)
    : width_(width), holding_(false) {
    assert(width_ > 0);
    valid_.assign(width_, false);
    slots_.assign(width_, FetchedInstruction{});
}

void DispatchReg::flush() {
    holding_ = false;
    for (int i = 0; i < width_; ++i) {
        valid_[i] = false;
        slots_[i] = FetchedInstruction{};
    }
}

int DispatchReg::count() const {
    int n = 0;
    for (int i = 0; i < width_; ++i) {
        if (valid_[i]) ++n;
    }
    return n;
}

bool DispatchReg::canCapture() const {
    // RTL: in_ready = !holding || full_pop. After acceptPrefix emptied us,
    // holding_ is false and we can capture.
    return !holding_;
}

bool DispatchReg::laneValid(int i) const {
    return i >= 0 && i < width_ && valid_[i];
}

const FetchedInstruction& DispatchReg::lane(int i) const {
    assert(laneValid(i));
    return slots_[i];
}

void DispatchReg::acceptPrefix(int n) {
    assert(n >= 0 && n <= count());
    if (n == 0) {
        return;
    }

    std::vector<bool> next_valid(width_, false);
    std::vector<FetchedInstruction> next_slots(width_);

    for (int i = 0; i < width_; ++i) {
        const int src = i + n;
        if (src < width_ && valid_[src]) {
            next_valid[i] = true;
            next_slots[i] = slots_[src];
        }
    }

    valid_ = next_valid;
    slots_ = next_slots;
    holding_ = (count() != 0);
}

void DispatchReg::capture(const std::vector<FetchedInstruction>& bundle) {
    assert(canCapture());
    assert(static_cast<int>(bundle.size()) <= width_);

    flush();  // clear then fill (holding was false)
    for (size_t i = 0; i < bundle.size(); ++i) {
        valid_[i] = true;
        slots_[i] = bundle[i];
    }
    holding_ = !bundle.empty();
}

void DispatchReg::printState() const {
    std::cout << "DispatchReg (holding=" << (holding_ ? "yes" : "no")
              << " count=" << count() << "/" << width_ << ")\n";
    for (int i = 0; i < width_; ++i) {
        if (!valid_[i]) continue;
        std::cout << "  [" << i << "] PC=0x" << std::hex << slots_[i].pc << std::dec
                  << " " << slots_[i].opcode << "\n";
    }
    std::cout << "-----------------------\n";
}
