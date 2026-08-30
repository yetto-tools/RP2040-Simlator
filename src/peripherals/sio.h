// sio.h - Single-cycle IO block (datasheet 2.3.1). Per-core fast path to
// GPIO, the inter-core mailbox FIFO, and the 32 hardware spinlocks.
//
// There is one SIO per core in hardware; this single instance serves both by
// switching `active_core` before each core steps. Interpolators and the QSPI
// GPIO bank are out of scope.
#ifndef RP2040_PERIPHERALS_SIO_H
#define RP2040_PERIPHERALS_SIO_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/memory.h"
#include "peripherals/gpio.h"

namespace rp2040 {

class Sio : public BusPeripheral {
public:
    static constexpr std::uint32_t kBase = 0xD0000000u;
    static constexpr std::uint32_t kSize = 0x1000u;
    static constexpr unsigned kNumSpinlocks = 32;
    static constexpr unsigned kFifoDepth = 8;
    static constexpr unsigned kIrqProc0 = kExcExternal0 + 15;  // SIO_IRQ_PROC0
    static constexpr unsigned kIrqProc1 = kExcExternal0 + 16;

    explicit Sio(Gpio& gpio) : gpio_(gpio) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    // Wiring for the dual-core model.
    void connect_cores(Cpu* c0, Cpu* c1) { cpu0_ = c0; cpu1_ = c1; }
    void set_active_core(unsigned core) { active_ = core & 1u; }
    // Called with (vector_table, stack_pointer, entry) once core 0 has pushed
    // the full launch sequence through the mailbox.
    void on_core1_launch(std::function<void(std::uint32_t, std::uint32_t, std::uint32_t)> cb) {
        launch_cb_ = std::move(cb);
    }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

private:
    void mailbox_write(std::uint32_t value);
    void refresh_fifo_irqs();

    Gpio& gpio_;
    Cpu* cpu0_ = nullptr;
    Cpu* cpu1_ = nullptr;
    unsigned active_ = 0;

    std::deque<std::uint32_t> to0_;   // core1 -> core0
    std::deque<std::uint32_t> to1_;   // core0 -> core1
    std::uint32_t fifo_wof_ = 0, fifo_roe_ = 0;  // write-over / read-underflow flags

    // Core-1 launch sequence tracking (0,0,1,vtor,sp,entry).
    int launch_stage_ = -1;           // -1 idle, 0..2 collecting vtor/sp/entry
    std::array<std::uint32_t, 3> launch_words_{};
    std::function<void(std::uint32_t, std::uint32_t, std::uint32_t)> launch_cb_;

    std::array<bool, kNumSpinlocks> spinlock_{};  // true = held
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_SIO_H
