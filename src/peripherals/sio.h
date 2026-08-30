// sio.h - Single-cycle IO block (datasheet 2.3.1). The CPU's fast path to
// GPIO: GPIO_IN / GPIO_OUT{,_SET,_CLR,_XOR} / GPIO_OE{,_SET,_CLR,_XOR}.
//
// Inter-core FIFO, spinlocks, interpolators and the QSPI (HI) GPIO bank are
// out of scope for now.
#ifndef RP2040_PERIPHERALS_SIO_H
#define RP2040_PERIPHERALS_SIO_H

#include <cstdint>

#include "core/bus.h"
#include "core/memory.h"
#include "peripherals/gpio.h"

namespace rp2040 {

class Sio : public BusPeripheral {
public:
    static constexpr std::uint32_t kBase = 0xD0000000u;
    static constexpr std::uint32_t kSize = 0x1000u;

    // `core` is the CPUID this SIO instance reports (0 or 1).
    Sio(Gpio& gpio, std::uint32_t core = 0) : gpio_(gpio), cpuid_(core) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;

private:
    Gpio& gpio_;
    std::uint32_t cpuid_;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_SIO_H
