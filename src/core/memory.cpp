#include "core/memory.h"

#include <algorithm>
#include <cstring>

namespace rp2040 {

namespace {

BusWidth width_of(std::uint32_t bytes) {
    switch (bytes) {
        case 1:  return BusWidth::Byte;
        case 2:  return BusWidth::Half;
        default: return BusWidth::Word;
    }
}

}  // namespace

// Regions are value-initialised to zero. Real SRAM is undefined at power-on,
// but a deterministic zero fill is required for reproducible traces
// (DESIGN.md, determinism decision).
Memory::Memory()
    : rom_(kRomSize), flash_(kFlashSize), sram_(kSramSize) {}

// --- backing-store lookup --------------------------------------------------

const std::uint8_t* Memory::backing(std::uint32_t addr, std::uint32_t n) const {
    if (kRom.contains_span(addr, n))   return rom_.data()   + (addr - kRom.base);
    if (kFlash.contains_span(addr, n)) return flash_.data() + (addr - kFlash.base);
    if (kSram.contains_span(addr, n))  return sram_.data()  + (addr - kSram.base);
    return nullptr;
}

std::uint8_t* Memory::backing(std::uint32_t addr, std::uint32_t n, bool& writable) {
    // Only SRAM is writable from the CPU bus path; ROM and the XIP flash
    // window are read-only to direct stores (ARCHITECTURE.md 2.4).
    writable = kSram.contains_span(addr, n);
    const std::uint8_t* p = static_cast<const Memory*>(this)->backing(addr, n);
    return const_cast<std::uint8_t*>(p);
}

Memory::PeripheralMapping* Memory::find_peripheral(std::uint32_t addr, std::uint32_t n) {
    if (!kRegisterSpace.contains(addr) && !kSio.contains(addr) && !kPpb.contains(addr)) {
        return nullptr;
    }
    for (auto& m : peripherals_) {
        if (m.region.contains_span(addr, n)) return &m;
    }
    return nullptr;
}

// --- debug watchpoints ---------------------------------------------------

void Memory::add_watchpoint(std::uint32_t addr, std::uint32_t len, bool on_read, bool on_write) {
    watchpoints_.push_back({addr, len == 0 ? 1u : len, on_read, on_write});
}

void Memory::remove_watchpoint(std::uint32_t addr) {
    watchpoints_.erase(
        std::remove_if(watchpoints_.begin(), watchpoints_.end(),
                        [addr](const Watchpoint& w) { return w.addr == addr; }),
        watchpoints_.end());
}

bool Memory::take_watchpoint_hit(std::uint32_t& addr, bool& was_write) {
    if (!wp_hit_) return false;
    addr = wp_hit_addr_;
    was_write = wp_hit_was_write_;
    wp_hit_ = false;
    return true;
}

void Memory::check_watchpoints(std::uint32_t addr, std::uint32_t n, bool is_write) {
    if (suppress_wp_ || wp_hit_) return;  // one latched hit is enough to stop the debugger
    for (const auto& w : watchpoints_) {
        const bool overlaps = addr < w.addr + w.len && w.addr < addr + n;
        if (!overlaps) continue;
        if ((is_write && w.on_write) || (!is_write && w.on_read)) {
            wp_hit_ = true;
            wp_hit_addr_ = w.addr;
            wp_hit_was_write_ = is_write;
            return;
        }
    }
}

// --- scalar access templates --------------------------------------------

template <typename T>
BusResult<T> Memory::read_scalar(std::uint32_t addr) {
    constexpr auto width = static_cast<std::uint32_t>(sizeof(T));
    if constexpr (width > 1) {
        if ((addr & (width - 1u)) != 0u) {
            return BusResult<T>::fail(BusStatus::MisalignedAccess);
        }
    }

    bool writable = false;
    if (const std::uint8_t* p = backing(addr, width, writable)) {
        T value = 0;
        for (std::uint32_t i = 0; i < width; ++i) {
            value = static_cast<T>(value | (static_cast<T>(p[i]) << (8u * i)));
        }
        check_watchpoints(addr, width, /*is_write=*/false);
        return {value, BusStatus::Ok};
    }

    if (PeripheralMapping* m = find_peripheral(addr, width)) {
        BusResult<std::uint32_t> r =
            m->peripheral->bus_read(addr - m->region.base, width_of(width));
        if (r.status == BusStatus::Ok) check_watchpoints(addr, width, /*is_write=*/false);
        return {static_cast<T>(r.value), r.status};
    }

    return BusResult<T>::fail(BusStatus::InvalidAddress);
}

template <typename T>
BusStatus Memory::write_scalar(std::uint32_t addr, T value) {
    constexpr auto width = static_cast<std::uint32_t>(sizeof(T));
    if constexpr (width > 1) {
        if ((addr & (width - 1u)) != 0u) return BusStatus::MisalignedAccess;
    }

    bool writable = false;
    if (std::uint8_t* p = backing(addr, width, writable)) {
        if (!writable) return BusStatus::WriteToReadOnly;
        const auto raw = static_cast<std::uint32_t>(value);
        for (std::uint32_t i = 0; i < width; ++i) {
            p[i] = static_cast<std::uint8_t>(raw >> (8u * i));
        }
        check_watchpoints(addr, width, /*is_write=*/true);
        return BusStatus::Ok;
    }

    if (PeripheralMapping* m = find_peripheral(addr, width)) {
        const BusStatus st = m->peripheral->bus_write(addr - m->region.base,
                                        static_cast<std::uint32_t>(value),
                                        width_of(width));
        if (st == BusStatus::Ok) check_watchpoints(addr, width, /*is_write=*/true);
        return st;
    }

    // RP2040 atomic register aliasing (datasheet 2.1.3 "Atomic Register
    // Access"): within the APB/AHB peripheral register space, address bits
    // [13:12] select XOR/SET/CLEAR against the same underlying register at
    // the unaliased address, instead of a plain write - hw_xor_bits() /
    // hw_set_bits() / hw_clear_bits() (used throughout every pico-sdk
    // peripheral driver) compile straight to this. Not used by SIO or the
    // PPB, which have their own dedicated atomic registers instead of
    // address-based aliasing.
    if (kRegisterSpace.contains(addr)) {
        const std::uint32_t alias = addr & 0x3000u;
        const std::uint32_t base_addr = addr & ~0x3000u;
        if (alias != 0u) {
            if (PeripheralMapping* m = find_peripheral(base_addr, width)) {
                const std::uint32_t offset = base_addr - m->region.base;
                const BusResult<std::uint32_t> cur = m->peripheral->bus_read(offset, width_of(width));
                if (!cur.ok()) return cur.status;
                std::uint32_t next = cur.value;
                const auto v = static_cast<std::uint32_t>(value);
                switch (alias) {
                    case 0x1000u: next ^= v; break;   // XOR
                    case 0x2000u: next |= v; break;   // SET
                    case 0x3000u: next &= ~v; break;  // CLEAR
                    default: break;
                }
                const BusStatus st = m->peripheral->bus_write(offset, next, width_of(width));
                if (st == BusStatus::Ok) check_watchpoints(base_addr, width, /*is_write=*/true);
                return st;
            }
        }
    }

    return BusStatus::InvalidAddress;
}

// --- public bus path ----------------------------------------------------

BusResult<std::uint8_t> Memory::read_byte(std::uint32_t addr) { return read_scalar<std::uint8_t>(addr); }
BusResult<std::uint16_t> Memory::read_half(std::uint32_t addr) { return read_scalar<std::uint16_t>(addr); }
BusResult<std::uint32_t> Memory::read_word(std::uint32_t addr) { return read_scalar<std::uint32_t>(addr); }

BusStatus Memory::write_byte(std::uint32_t addr, std::uint8_t value) { return write_scalar<std::uint8_t>(addr, value); }
BusStatus Memory::write_half(std::uint32_t addr, std::uint16_t value) { return write_scalar<std::uint16_t>(addr, value); }
BusStatus Memory::write_word(std::uint32_t addr, std::uint32_t value) { return write_scalar<std::uint32_t>(addr, value); }

// --- backdoor path ----------------------------------------------------

bool Memory::load(std::uint32_t addr, const void* data, std::size_t size) {
    if (size == 0) return true;
    if (size > 0xFFFFFFFFu) return false;
    bool writable = false;
    std::uint8_t* p = backing(addr, static_cast<std::uint32_t>(size), writable);
    if (p == nullptr) return false;
    std::memcpy(p, data, size);
    return true;
}

bool Memory::dump(std::uint32_t addr, void* out, std::size_t size) const {
    if (size == 0) return true;
    if (size > 0xFFFFFFFFu) return false;
    const std::uint8_t* p = backing(addr, static_cast<std::uint32_t>(size));
    if (p == nullptr) return false;
    std::memcpy(out, p, size);
    return true;
}

// --- peripheral routing ------------------------------------------------

bool Memory::attach_peripheral(std::uint32_t base, std::uint32_t size, BusPeripheral* p) {
    if (size == 0 || p == nullptr) return false;
    if (base + size < base) return false;  // address wraparound
    if (!kRegisterSpace.contains_span(base, size) &&
        !kSio.contains_span(base, size) && !kPpb.contains_span(base, size)) {
        return false;
    }

    for (const auto& m : peripherals_) {
        const bool overlap =
            base < (m.region.base + m.region.size) && m.region.base < (base + size);
        if (overlap) return false;
    }

    peripherals_.push_back(PeripheralMapping{Region{base, size}, p});
    return true;
}

}  // namespace rp2040
