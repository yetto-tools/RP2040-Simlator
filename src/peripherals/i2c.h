// i2c.h - RP2040 I2C (Synopsys DesignWare APB I2C, datasheet 4.3).
//
// Functional master-mode model: a write to IC_DATA_CMD performs the byte
// transaction immediately against a registered slave callback. No bus-level
// timing, clock stretching or multi-master arbitration.
#ifndef RP2040_PERIPHERALS_I2C_H
#define RP2040_PERIPHERALS_I2C_H

#include <cstdint>
#include <deque>
#include <functional>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/memory.h"

namespace rp2040 {

class I2c : public BusPeripheral {
public:
    static constexpr std::uint32_t kI2c0Base = 0x40044000u;
    static constexpr std::uint32_t kI2c1Base = 0x40048000u;
    static constexpr std::uint32_t kSize = 0x1000u;
    static constexpr unsigned kI2c0Irq = kExcExternal0 + 23;  // I2C0_IRQ == IRQ23
    static constexpr unsigned kI2c1Irq = kExcExternal0 + 24;
    static constexpr unsigned kFifoDepth = 16;

    I2c(Cpu& cpu, std::uint32_t base, unsigned irq) : cpu_(cpu), base_(base), irq_(irq) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

    // Register a 7-bit slave. On a write transaction `byte` is the data and
    // the callback returns whether it ACKed; on a read it fills `byte` and
    // returns true if data is valid.
    using SlaveFn = std::function<bool(bool is_read, std::uint8_t& byte)>;
    void set_slave(std::uint8_t addr7, SlaveFn fn) { slave_addr_ = addr7; slave_ = std::move(fn); }

private:
    void refresh_irq();

    Cpu& cpu_;
    std::uint32_t base_;
    unsigned irq_;

    std::uint32_t con_ = 0x65;   // reset-ish: master, 7-bit, fast mode
    std::uint32_t tar_ = 0;
    bool enabled_ = false;

    std::deque<std::uint8_t> rx_;
    std::uint32_t raw_intr_ = 0;      // RX_FULL[2], TX_EMPTY[4], TX_ABRT[6], STOP_DET[9]
    std::uint32_t intr_mask_ = 0;
    std::uint32_t tx_abrt_source_ = 0;

    std::uint8_t slave_addr_ = 0xFF;
    SlaveFn slave_;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_I2C_H
