// exceptions.h - ARMv6-M exception model constants (BACKLOG P1.4).
//
// Reference: ARMv6-M Architecture Reference Manual (DDI 0419) section B1.5
// ("Exception model") and the RP2040 datasheet section 2.3 ("Processor
// subsystem" / interrupts).
#ifndef RP2040_EXCEPTIONS_H
#define RP2040_EXCEPTIONS_H

#include <cstdint>

namespace rp2040 {

// Exception numbers (== IPSR value while the handler runs).
enum ExceptionNumber : std::uint16_t {
    kExcReset     = 1,
    kExcNMI       = 2,
    kExcHardFault = 3,
    kExcSVCall    = 11,
    kExcPendSV    = 14,
    kExcSysTick   = 15,
    kExcExternal0 = 16,   // IRQ0 .. IRQ25 -> exceptions 16 .. 41 on the RP2040
};

inline constexpr int kNumRp2040Irqs = 26;                 // IRQ0..IRQ25
inline constexpr int kMaxException   = kExcExternal0 + kNumRp2040Irqs - 1;  // 41
inline constexpr int kExceptionTableEntries = kMaxException + 1;            // 0..41

// Fixed priorities for the non-configurable exceptions (lower = more urgent).
inline constexpr int kPriorityReset     = -3;
inline constexpr int kPriorityNMI       = -2;
inline constexpr int kPriorityHardFault = -1;

// ARMv6-M implements the top two bits of the 8-bit priority field: four
// levels, 0 (highest) .. 3 (lowest). The stored byte keeps the raw value.
inline constexpr int kPriorityBits = 2;
inline constexpr std::uint8_t kPriorityMask = 0xC0;  // bits [7:6]

inline int effective_priority(std::uint8_t raw) {
    return (raw & kPriorityMask) >> (8 - kPriorityBits);  // 0..3
}

// EXC_RETURN payloads loaded into LR on exception entry / recognised in PC on
// exception return (ARMv6-M ARM B1.5.8, Table B1-8).
inline constexpr std::uint32_t kExcReturnHandlerMSP = 0xFFFFFFF1u;
inline constexpr std::uint32_t kExcReturnThreadMSP  = 0xFFFFFFF9u;
inline constexpr std::uint32_t kExcReturnThreadPSP  = 0xFFFFFFFDu;

// Any value with this prefix, met in PC in Handler mode, is an exception return.
inline constexpr std::uint32_t kExcReturnPrefix = 0xFFFFFFF0u;

inline bool is_exc_return(std::uint32_t v) {
    return (v & kExcReturnPrefix) == kExcReturnPrefix;
}

// Exception stack frame: 8 words at the frame pointer, low address first.
enum StackFrameSlot {
    kFrameR0 = 0, kFrameR1, kFrameR2, kFrameR3,
    kFrameR12, kFrameLR, kFrameReturnAddress, kFrameXPSR,
    kStackFrameWords = 8,
};
inline constexpr std::uint32_t kStackFrameBytes = kStackFrameWords * 4;  // 0x20

// Bit 9 of a stacked xPSR: the entry sequence realigned the stack to 8 bytes.
inline constexpr std::uint32_t kXpsrStackAlign = 1u << 9;

}  // namespace rp2040

#endif  // RP2040_EXCEPTIONS_H
