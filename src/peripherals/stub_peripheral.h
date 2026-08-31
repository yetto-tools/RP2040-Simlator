// stub_peripheral.h - a passive register block: stores 32-bit writes, reads
// them back, never faults. Used for the RP2040 APB peripherals whose exact
// behaviour the simulator does not model but whose address range must decode
// so firmware probing them does not take a bus fault:
//
//   SYSCFG, BUSCTRL, PSM, VREG_AND_CHIP_RESET, TBMAN, IO_QSPI, PADS_QSPI.
//
// A small `read_defaults` map lets a block answer a few registers with a fixed
// value (e.g. PSM.DONE = "everything powered", TBMAN.PLATFORM = ASIC).
#ifndef RP2040_PERIPHERALS_STUB_PERIPHERAL_H
#define RP2040_PERIPHERALS_STUB_PERIPHERAL_H

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/memory.h"

namespace rp2040 {

class StubPeripheral : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;

    StubPeripheral(const char* name, std::uint32_t base,
                   std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> read_defaults = {})
        : name_(name), base_(base), defaults_(read_defaults.begin(), read_defaults.end()) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(base_, kSize, this); }
    const std::string& name() const { return name_; }
    std::uint32_t base() const { return base_; }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth) override {
        const auto it = store_.find(reg);
        if (it != store_.end()) return {it->second, BusStatus::Ok};
        const auto d = defaults_.find(reg);
        return {d != defaults_.end() ? d->second : 0u, BusStatus::Ok};
    }
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth) override {
        store_[reg] = value;
        return BusStatus::Ok;
    }

private:
    std::string name_;
    std::uint32_t base_;
    std::map<std::uint32_t, std::uint32_t> defaults_;
    std::map<std::uint32_t, std::uint32_t> store_;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_STUB_PERIPHERAL_H
