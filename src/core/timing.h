// timing.h - Cortex-M0+ instruction cycle counts (BACKLOG P1.5).
//
// The RP2040 core is a Cortex-M0+ with a **two-stage** pipeline (unlike the
// three-stage Cortex-M0). Values here follow the Cortex-M0+ Technical
// Reference Manual (ARM DDI 0484), Table 3-1 "Processor instruction timings",
// and the RP2040 datasheet note that the hardware multiplier is single-cycle.
//
// These are the *core* execution cycles with zero-wait-state memory. Bus wait
// states (flash XIP, contended SRAM) are added by the memory model later.
#ifndef RP2040_CORE_TIMING_H
#define RP2040_CORE_TIMING_H

#include "thumb_isa.h"

namespace rp2040 {

// Cycles for one executed instruction.
//   took_branch : for B_cond, whether the branch was taken this time.
//   reg_count   : number of registers transferred (LDM/STM/PUSH/POP), else 0.
unsigned instruction_cycles(const DecodedInstr& d, bool took_branch, unsigned reg_count);

// DDI 0484C section 3.6.1 ("Exception handling"): "The worst case interrupt
// latency, for the highest priority active interrupt in a zero wait-state
// system not using jitter suppression, is 15 cycles." That figure covers
// the whole entry sequence (recognise, prioritise, stack, vector fetch,
// pipeline refill), so it is charged in full by Cpu::take_exception().
// This TRM edition documents no separate figure for exception *return* or
// for a tail-chained (back-to-back) entry, so neither is costed here - see
// ARCHITECTURE.md 5.5 and BACKLOG.md P5.2 for the open item.
inline constexpr unsigned kExceptionEntryCycles = 15;

}  // namespace rp2040

#endif  // RP2040_CORE_TIMING_H
