// memory.h - RP2040 unified memory + address decoder (BACKLOG P1.3).
//
// Owns the ROM/Flash/SRAM backing stores and a small routing table for the
// peripheral register space. Every CPU load/store goes through here.
//
// Design: DESIGN.md Decision 6 ("unified memory with peripheral dispatch")
// and Decision 13 ("support all sizes, check alignment per ARM spec").
// Behaviour reference: ARCHITECTURE.md section 2.
#ifndef RP2040_CORE_MEMORY_H
#define RP2040_CORE_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/bus.h"
#include "rp2040.h"

namespace rp2040 {

class Memory {
public:
    // Contiguous address range [base, base + size).
    struct Region {
        std::uint32_t base;
        std::uint32_t size;

        bool contains(std::uint32_t addr) const {
            return addr >= base && addr - base < size;
        }
        // True if the whole [addr, addr + n) span is inside the region.
        bool contains_span(std::uint32_t addr, std::uint32_t n) const {
            return n <= size && addr >= base && (addr - base) <= (size - n);
        }
    };

    static constexpr Region kRom{kRomBase, kRomSize};
    static constexpr Region kFlash{kFlashBase, kFlashSize};
    static constexpr Region kSram{kSramBase, kSramSize};
    static constexpr Region kRegisterSpace{0x40000000u, 0x20000000u};

    Memory();

    // --- CPU bus path: little-endian, alignment-checked ---------------------
    BusResult<std::uint8_t> read_byte(std::uint32_t addr);
    BusResult<std::uint16_t> read_half(std::uint32_t addr);
    BusResult<std::uint32_t> read_word(std::uint32_t addr);

    BusStatus write_byte(std::uint32_t addr, std::uint8_t value);
    BusStatus write_half(std::uint32_t addr, std::uint16_t value);
    BusStatus write_word(std::uint32_t addr, std::uint32_t value);

    // --- Backdoor path: no protection, no alignment rule, no side effects ---
    // For ELF/UF2 loaders and tests. Returns false unless [addr, addr+size) is
    // fully backed by a single ROM/Flash/SRAM region.
    bool load(std::uint32_t addr, const void* data, std::size_t size);
    bool dump(std::uint32_t addr, void* out, std::size_t size) const;

    // --- Peripheral routing ----------------------------------------------
    // `base`/`size` must lie within the register space and not overlap an
    // existing mapping. Returns false otherwise. `p` is borrowed, not owned.
    bool attach_peripheral(std::uint32_t base, std::uint32_t size, BusPeripheral* p);

private:
    struct PeripheralMapping {
        Region region;
        BusPeripheral* peripheral;
    };

    // Returns the backing byte span for [addr, addr+n) if fully inside one
    // writable-or-not RAM/ROM/Flash region, else nullptr. `writable` reports
    // whether the CPU bus path is allowed to store there.
    std::uint8_t* backing(std::uint32_t addr, std::uint32_t n, bool& writable);
    const std::uint8_t* backing(std::uint32_t addr, std::uint32_t n) const;

    PeripheralMapping* find_peripheral(std::uint32_t addr, std::uint32_t n);

    template <typename T>
    BusResult<T> read_scalar(std::uint32_t addr);
    template <typename T>
    BusStatus write_scalar(std::uint32_t addr, T value);

    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> flash_;
    std::vector<std::uint8_t> sram_;
    std::vector<PeripheralMapping> peripherals_;
};

}  // namespace rp2040

#endif  // RP2040_CORE_MEMORY_H
