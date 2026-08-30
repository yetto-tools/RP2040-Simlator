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

}  // namespace rp2040

#endif  // RP2040_CORE_TIMING_H
