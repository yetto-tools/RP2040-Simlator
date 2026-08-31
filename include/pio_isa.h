// pio_isa.h - RP2040 PIO instruction set: 9 instructions, 16-bit encodings.
//
// Reference: RP2040 datasheet section 3.4 "Instruction Set". The PIO is a
// co-processor, not a peripheral: each state machine has its own PC and
// executes one PIO instruction per (divided) clock, in parallel with the CPU
// and every other state machine.
#ifndef RP2040_PIO_ISA_H
#define RP2040_PIO_ISA_H

#include <cstdint>

namespace rp2040 {

enum class PioOp : std::uint8_t { JMP, WAIT, IN, OUT, PUSH, PULL, MOV, IRQ, SET };

// JMP conditions (datasheet 3.4.2).
enum PioCondition : std::uint8_t {
    kJmpAlways = 0,   // (no condition)
    kJmpNotX   = 1,   // !X   : scratch X is zero
    kJmpXDec   = 2,   // X--  : X != 0 before an unconditional decrement
    kJmpNotY   = 3,
    kJmpYDec   = 4,
    kJmpXNeY   = 5,   // X != Y
    kJmpPin    = 6,   // branch on the EXECCTRL-selected input pin
    kJmpNotOsrE = 7,  // !OSRE : output shift count < PULL threshold
};

// IN / OUT / MOV source and destination selectors (datasheet 3.4.5-3.4.7).
enum PioInSource : std::uint8_t  { kInPins = 0, kInX = 1, kInY = 2, kInNull = 3, kInIsr = 6, kInOsr = 7 };
enum PioOutDest : std::uint8_t   { kOutPins = 0, kOutX = 1, kOutY = 2, kOutNull = 3,
                                   kOutPindirs = 4, kOutPc = 5, kOutIsr = 6, kOutExec = 7 };
enum PioMovReg : std::uint8_t    { kMovPins = 0, kMovX = 1, kMovY = 2, kMovNull = 3,
                                   kMovStatus = 5, kMovIsr = 6, kMovOsr = 7 };
// MOV destination uses a different encoding than MOV source for indices 4/5
// (datasheet 3.4.6): source 4 is reserved, 5 is STATUS; destination 4 is
// EXEC, 5 is PC. destination 0/1/2/6/7 (PINS/X/Y/ISR/OSR) match the source
// enum above and reuse those names.
enum PioMovDest : std::uint8_t   { kMovDestExec = 4, kMovDestPc = 5 };
enum PioMovOp : std::uint8_t     { kMovNone = 0, kMovInvert = 1, kMovBitRev = 2 };
enum PioSetDest : std::uint8_t   { kSetPins = 0, kSetX = 1, kSetY = 2, kSetPindirs = 4 };
enum PioWaitSource : std::uint8_t{ kWaitGpio = 0, kWaitPin = 1, kWaitIrq = 2 };

struct PioInstr {
    PioOp op = PioOp::JMP;
    std::uint8_t delay_sideset = 0;  // raw bits [12:8]; split by SIDESET_COUNT

    std::uint8_t condition = 0;   // JMP
    std::uint8_t address = 0;     // JMP target (0..31)
    bool polarity = false;        // WAIT
    std::uint8_t source = 0;      // WAIT / IN / OUT(=dest) / MOV
    std::uint8_t destination = 0; // OUT / MOV / SET
    std::uint8_t index = 0;       // WAIT(IRQ) / IRQ (0..31; bit4 = MSB/rel flags)
    std::uint8_t bit_count = 32;  // IN / OUT (encoding 0 means 32)
    std::uint8_t data = 0;        // SET (0..31)
    std::uint8_t mov_op = 0;      // MOV operation
    bool if_full = false;         // PUSH iffull
    bool if_empty = false;        // PULL ifempty
    bool block = true;            // PUSH / PULL blocking
    bool clear = false;           // IRQ: clear instead of set
    bool wait = false;            // IRQ: wait for the flag to clear

    std::uint16_t raw = 0;
};

// Decode a 16-bit PIO instruction word.
PioInstr pio_decode(std::uint16_t instr);

const char* to_string(PioOp op);

}  // namespace rp2040

#endif  // RP2040_PIO_ISA_H
