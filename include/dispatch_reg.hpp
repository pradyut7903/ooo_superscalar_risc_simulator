#pragma once

#include "types.hpp"

#include <iostream>
#include <vector>

// Decode-to-rename bundle register (mirrors github_ooo_rv32im/rtl/dispatch_reg.sv).
// Holds up to `width` decoded instructions. Rename accepts a program-order
// prefix; remaining lanes compact toward lane 0. A new bundle is captured only
// when empty (or after a full pop).
class DispatchReg {
public:
    explicit DispatchReg(int width);

    void flush();

    // True when a new decoded bundle may be written this cycle.
    bool canCapture() const;

    int width() const { return width_; }
    int count() const;
    bool empty() const { return count() == 0; }

    bool laneValid(int i) const;
    const FetchedInstruction& lane(int i) const;

    // Pop lanes [0 .. n). Compact survivors to lane 0.
    void acceptPrefix(int n);

    // Load a new bundle (only when canCapture()). `bundle` is a dense prefix.
    void capture(const std::vector<FetchedInstruction>& bundle);

    void printState() const;

private:
    int width_;
    bool holding_;
    std::vector<bool> valid_;
    std::vector<FetchedInstruction> slots_;
};
