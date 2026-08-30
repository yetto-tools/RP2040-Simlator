// pio_fifo.h - a PIO state machine's 4-deep 32-bit FIFO (datasheet 3.5.4).
//
// TX FIFO: CPU writes, SM pulls. RX FIFO: SM pushes, CPU reads. The two can be
// "joined" into a single 8-deep FIFO in one direction (SHIFTCTRL FJOIN).
#ifndef RP2040_PIO_PIO_FIFO_H
#define RP2040_PIO_PIO_FIFO_H

#include <array>
#include <cstdint>

namespace rp2040 {

class PioFifo {
public:
    static constexpr unsigned kDefaultDepth = 4;
    static constexpr unsigned kJoinedDepth = 8;

    void clear() { count_ = 0; head_ = 0; }
    void set_depth(unsigned depth) { depth_ = depth; clear(); }

    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == depth_; }
    unsigned level() const { return count_; }
    unsigned depth() const { return depth_; }

    bool push(std::uint32_t v) {
        if (full()) return false;
        buf_[(head_ + count_) % depth_] = v;
        ++count_;
        return true;
    }
    bool pop(std::uint32_t& out) {
        if (empty()) return false;
        out = buf_[head_];
        head_ = (head_ + 1) % depth_;
        --count_;
        return true;
    }
    // Non-destructive read of the front element (0 if empty).
    std::uint32_t peek() const { return empty() ? 0u : buf_[head_]; }

private:
    std::array<std::uint32_t, kJoinedDepth> buf_{};
    unsigned head_ = 0;
    unsigned count_ = 0;
    unsigned depth_ = kDefaultDepth;
};

}  // namespace rp2040

#endif  // RP2040_PIO_PIO_FIFO_H
