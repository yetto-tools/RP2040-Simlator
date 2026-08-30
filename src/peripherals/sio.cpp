#include "peripherals/sio.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kCPUID       = 0x000,
    kGPIO_IN     = 0x004,
    kGPIO_HI_IN  = 0x008,
    kGPIO_OUT    = 0x010, kGPIO_OUT_SET = 0x014, kGPIO_OUT_CLR = 0x018, kGPIO_OUT_XOR = 0x01C,
    kGPIO_OE     = 0x020, kGPIO_OE_SET  = 0x024, kGPIO_OE_CLR  = 0x028, kGPIO_OE_XOR  = 0x02C,
    kFIFO_ST     = 0x050, kFIFO_WR = 0x054, kFIFO_RD = 0x058,
    kSPINLOCK0   = 0x100,   // .. 0x17C
};
constexpr std::uint32_t kFIFO_ST_VLD = 1u << 0;
constexpr std::uint32_t kFIFO_ST_RDY = 1u << 1;
constexpr std::uint32_t kFIFO_ST_WOF = 1u << 2;
constexpr std::uint32_t kFIFO_ST_ROE = 1u << 3;
}  // namespace

void Sio::refresh_fifo_irqs() {
    if (cpu0_ != nullptr) {
        if (!to0_.empty()) cpu0_->pend_exception(kIrqProc0);
        else               cpu0_->clear_pending(kIrqProc0);
    }
    if (cpu1_ != nullptr) {
        if (!to1_.empty()) cpu1_->pend_exception(kIrqProc1);
        else               cpu1_->clear_pending(kIrqProc1);
    }
}

void Sio::mailbox_write(std::uint32_t value) {
    std::deque<std::uint32_t>& out = (active_ == 0) ? to1_ : to0_;
    if (out.size() >= kFifoDepth) {
        fifo_wof_ = 1u;
        return;
    }
    out.push_back(value);

    // Core-0 side drives the core-1 launch handshake: the bootrom on core 1
    // echoes every received word back, and a leading (0,0,1) selects "launch".
    if (active_ == 0) {
        to0_.push_back(value);  // auto-echo from the (unmodelled) core-1 bootrom
        if (launch_stage_ < 0) {
            if (value == 1u) launch_stage_ = 0;  // command word seen
        } else {
            launch_words_[static_cast<unsigned>(launch_stage_)] = value;
            if (++launch_stage_ == 3) {
                launch_stage_ = -1;
                if (launch_cb_) {
                    launch_cb_(launch_words_[0], launch_words_[1], launch_words_[2]);
                }
            }
        }
    }
    refresh_fifo_irqs();
}

BusResult<std::uint32_t> Sio::bus_read(std::uint32_t offset, BusWidth) {
    if (offset >= kSPINLOCK0 && offset < kSPINLOCK0 + kNumSpinlocks * 4u) {
        const unsigned n = (offset - kSPINLOCK0) / 4u;
        if (spinlock_[n]) return {0u, BusStatus::Ok};   // already held -> 0
        spinlock_[n] = true;
        return {1u << n, BusStatus::Ok};                // acquired
    }
    switch (offset) {
        case kCPUID:     return {active_, BusStatus::Ok};
        case kGPIO_IN:    return {gpio_.input_bits(), BusStatus::Ok};
        case kGPIO_HI_IN: return {0u, BusStatus::Ok};
        case kGPIO_OUT:   return {gpio_.driver_out(Gpio::kSio), BusStatus::Ok};
        case kGPIO_OE:    return {gpio_.driver_oe(Gpio::kSio), BusStatus::Ok};
        case kFIFO_ST: {
            const std::deque<std::uint32_t>& in = (active_ == 0) ? to0_ : to1_;
            const std::deque<std::uint32_t>& out = (active_ == 0) ? to1_ : to0_;
            std::uint32_t st = 0;
            if (!in.empty()) st |= kFIFO_ST_VLD;
            if (out.size() < kFifoDepth) st |= kFIFO_ST_RDY;
            if (fifo_wof_) st |= kFIFO_ST_WOF;
            if (fifo_roe_) st |= kFIFO_ST_ROE;
            return {st, BusStatus::Ok};
        }
        case kFIFO_RD: {
            std::deque<std::uint32_t>& in = (active_ == 0) ? to0_ : to1_;
            if (in.empty()) { fifo_roe_ = 1u; return {0u, BusStatus::Ok}; }
            const std::uint32_t v = in.front();
            in.pop_front();
            refresh_fifo_irqs();
            return {v, BusStatus::Ok};
        }
        default: return {0u, BusStatus::Ok};
    }
}

BusStatus Sio::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    if (offset >= kSPINLOCK0 && offset < kSPINLOCK0 + kNumSpinlocks * 4u) {
        spinlock_[(offset - kSPINLOCK0) / 4u] = false;   // any write releases
        return BusStatus::Ok;
    }
    const std::uint32_t out = gpio_.driver_out(Gpio::kSio);
    const std::uint32_t oe = gpio_.driver_oe(Gpio::kSio);
    switch (offset) {
        case kGPIO_OUT:     gpio_.driver_set_out(Gpio::kSio, value); break;
        case kGPIO_OUT_SET: gpio_.driver_set_out(Gpio::kSio, out | value); break;
        case kGPIO_OUT_CLR: gpio_.driver_set_out(Gpio::kSio, out & ~value); break;
        case kGPIO_OUT_XOR: gpio_.driver_set_out(Gpio::kSio, out ^ value); break;
        case kGPIO_OE:      gpio_.driver_set_oe(Gpio::kSio, value); break;
        case kGPIO_OE_SET:  gpio_.driver_set_oe(Gpio::kSio, oe | value); break;
        case kGPIO_OE_CLR:  gpio_.driver_set_oe(Gpio::kSio, oe & ~value); break;
        case kGPIO_OE_XOR:  gpio_.driver_set_oe(Gpio::kSio, oe ^ value); break;
        case kFIFO_ST:      fifo_wof_ = 0; fifo_roe_ = 0; break;  // write clears the flags
        case kFIFO_WR:      mailbox_write(value); break;
        default: break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
