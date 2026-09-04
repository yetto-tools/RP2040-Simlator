// bus.h - Shared types for the RP2040 system bus.
//
// The bus connects the CPU cores to memory (ROM/Flash/SRAM) and to the
// peripheral register space. Access widths and the failure model follow the
// ARMv6-M (Cortex-M0+) architecture: little-endian only, all unaligned
// accesses trapped. See ARCHITECTURE.md section 2.3-2.4.
#ifndef RP2040_CORE_BUS_H
#define RP2040_CORE_BUS_H

#include <cstdint>

namespace rp2040 {

// Access width in bytes. The numeric values are the byte counts on purpose so
// they double as the alignment mask base (mask = width - 1).
enum class BusWidth : std::uint8_t {
    Byte = 1,
    Half = 2,
    Word = 4,
};

enum class BusStatus : std::uint8_t {
    Ok,
    MisalignedAccess,  // width > 1 and address not naturally aligned
    InvalidAddress,    // unbacked address with no peripheral mapped
    WriteToReadOnly,   // store into ROM or the XIP flash window
    PeripheralError,   // a mapped peripheral rejected the access
};

// Result of a bus read. On failure `value` is zero and must not be used.
template <typename T>
struct BusResult {
    T value{};
    BusStatus status = BusStatus::Ok;

    bool ok() const { return status == BusStatus::Ok; }

    static BusResult fail(BusStatus s) { return BusResult{T{}, s}; }
};

// Interface implemented by every peripheral that occupies register space
// (0x40000000-0x5FFFFFFF). Offsets are relative to the base the peripheral was
// registered at; width/alignment have already been validated by the bus.
//
// Reads may have side effects (e.g. popping a FIFO), hence non-const.
class BusPeripheral {
public:
    virtual ~BusPeripheral() = default;

    virtual BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) = 0;
    virtual BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) = 0;

    // Restore this peripheral's register-backed state to its power-on-reset
    // defaults - the same state a freshly-constructed instance starts in.
    // Does NOT touch simulator wiring (references to Gpio/Cpu/Memory,
    // registered callbacks, clock-Hz values pushed by the clock tree): those
    // stand in for physical wires/external inputs, which a peripheral reset
    // doesn't disconnect on real hardware either. Default no-op, for
    // peripherals with no meaningful register state (or not in the RESETS
    // block's 25-bit RESET/WDSEL field at all - see resets.h) to leave
    // unoverridden. Driven by RESETS_WDSEL on a watchdog-triggered reset
    // (see Watchdog::fire_reset(), Simulator's wiring of it).
    virtual void reset() {}
};

// Human-readable status name, for logs, traces and test failure messages.
const char* to_string(BusStatus status);

}  // namespace rp2040

#endif  // RP2040_CORE_BUS_H
