// rp2040.h - RP2040 memory map and top-level hardware constants.
//
// Reference: RP2040 Datasheet (rev. 2021-2024), section 2.2 "Address Map".
// This header only declares constants that are stable across the whole
// simulator; register-level definitions live in the per-peripheral headers.
#ifndef RP2040_RP2040_H
#define RP2040_RP2040_H

#include <cstddef>
#include <cstdint>

namespace rp2040 {

// --- Address map (Datasheet 2.2) --------------------------------------------
inline constexpr std::uint32_t kRomBase   = 0x00000000u;
inline constexpr std::uint32_t kRomSize   = 16u * 1024u;          // 16 KiB

inline constexpr std::uint32_t kFlashBase = 0x10000000u;
inline constexpr std::uint32_t kFlashSize = 2u * 1024u * 1024u;   // 2 MiB (XIP window)

inline constexpr std::uint32_t kSramBase  = 0x20000000u;
inline constexpr std::uint32_t kSramSize  = 264u * 1024u;         // 264 KiB

inline constexpr std::uint32_t kApbPeriphBase = 0x40000000u;
inline constexpr std::uint32_t kAhbPeriphBase = 0x50000000u;
inline constexpr std::uint32_t kSioBase       = 0xD0000000u;
inline constexpr std::uint32_t kPpbBase       = 0xE0000000u;

// --- Selected peripheral bases (Datasheet 2.2) -----------------------------
inline constexpr std::uint32_t kGpioBase = 0x40014000u;  // IO_BANK0
inline constexpr std::uint32_t kTimerBase = 0x40054000u;
inline constexpr std::uint32_t kUart0Base = 0x40034000u;
inline constexpr std::uint32_t kUart1Base = 0x40038000u;
inline constexpr std::uint32_t kSpi0Base  = 0x4003C000u;
inline constexpr std::uint32_t kSpi1Base  = 0x40040000u;
inline constexpr std::uint32_t kI2c0Base  = 0x40044000u;
inline constexpr std::uint32_t kI2c1Base  = 0x40048000u;
inline constexpr std::uint32_t kAdcBase   = 0x4004C000u;
inline constexpr std::uint32_t kPio0Base  = 0x50200000u;
inline constexpr std::uint32_t kPio1Base  = 0x50300000u;

// --- Clocks ---------------------------------------------------------------
// Default system clock after the SDK bootrom/clocks_init runs.
inline constexpr std::uint32_t kDefaultSysClkHz = 125'000'000u;
// Ring oscillator frequency the chip boots on before PLL configuration.
inline constexpr std::uint32_t kRoscBootHz = 6'500'000u;

// --- Core topology ------------------------------------------------------
inline constexpr int kNumCpuCores      = 2;   // dual Cortex-M0+
inline constexpr int kNumGpioPins      = 30;  // GPIO0..29 (26..29 ADC-capable)
inline constexpr int kNumPioBlocks     = 2;
inline constexpr int kStateMachinesPerBlock = 4;
inline constexpr int kPioInstrMemWords  = 32;
inline constexpr int kPioFifoDepth      = 4;

}  // namespace rp2040

#endif  // RP2040_RP2040_H
