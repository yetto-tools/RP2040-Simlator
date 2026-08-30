// pio_disasm.h - render a 16-bit PIO instruction word as pioasm text.
//
// The inverse of pio_assembler for a single instruction: used by the PIO
// debugger's disassembly view and by instruction traces. Side-set / delay
// splitting needs the state machine's SIDE_SET configuration; pass 0 / false
// when it is unknown (the whole [12:8] field is then shown as the delay).
#ifndef RP2040_PIO_PIO_DISASM_H
#define RP2040_PIO_PIO_DISASM_H

#include <cstdint>
#include <string>

namespace rp2040 {

std::string pio_disassemble(std::uint16_t word, unsigned sideset_count = 0,
                            bool sideset_opt = false);

}  // namespace rp2040

#endif  // RP2040_PIO_PIO_DISASM_H
