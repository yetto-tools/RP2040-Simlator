// pio_assembler.h - assemble RP2040 PIO source (the "pioasm" language) into
// encoded 16-bit instruction words plus the program metadata a state machine
// needs (origin, wrap, side-set configuration, public labels).
//
// This is the language accepted by the SDK's `pioasm` tool, minus the code
// generation back-ends: the simulator only needs the binary program and its
// layout, not a C/Python/Ada header. Supported directives:
//
//   .program <name>
//   .define [PUBLIC] <SYMBOL> <expr>
//   .origin <expr>
//   .side_set <count> [opt] [pindirs]
//   .wrap_target
//   .wrap
//
// plus labels (`name:` / `PUBLIC name:`) and the nine instructions (jmp, wait,
// in, out, push, pull, mov, irq, set) and the `nop` alias. Each instruction may
// carry a `side <expr>` group and a trailing `[<expr>]` delay.
//
// Reference: RP2040 datasheet 3.4 + SDK pioasm documentation.
#ifndef RP2040_PIO_PIO_ASSEMBLER_H
#define RP2040_PIO_PIO_ASSEMBLER_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rp2040 {

struct PioAssembly {
    bool ok = false;
    std::string error;                        // "line N: ..." when ok == false
    std::string program_name;

    std::vector<std::uint16_t> instructions;  // encoded, in program order

    int origin = -1;                          // .origin, or -1 if unset
    unsigned wrap_target = 0;                 // instruction index to wrap back to
    unsigned wrap = 0;                        // index of the last looped instruction

    unsigned side_set_count = 0;              // side-set value bits (excludes the opt enable bit)
    bool side_set_opt = false;
    bool side_set_pindirs = false;

    std::map<std::string, unsigned> public_labels;  // name -> instruction index
    std::map<std::string, std::int64_t> public_defines;
};

PioAssembly assemble_pio(std::string_view source);

}  // namespace rp2040

#endif  // RP2040_PIO_PIO_ASSEMBLER_H
