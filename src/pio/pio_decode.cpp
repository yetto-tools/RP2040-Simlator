// pio_decode.cpp - RP2040 PIO instruction decoder (datasheet 3.4).
#include "pio_isa.h"

namespace rp2040 {

PioInstr pio_decode(std::uint16_t instr) {
    PioInstr d;
    d.raw = instr;
    d.delay_sideset = static_cast<std::uint8_t>((instr >> 8) & 0x1Fu);

    const std::uint8_t opcode = static_cast<std::uint8_t>(instr >> 13);
    const std::uint8_t lo8 = static_cast<std::uint8_t>(instr & 0xFFu);

    switch (opcode) {
        case 0b000:  // JMP
            d.op = PioOp::JMP;
            d.condition = static_cast<std::uint8_t>((lo8 >> 5) & 0x7u);
            d.address = static_cast<std::uint8_t>(lo8 & 0x1Fu);
            break;

        case 0b001:  // WAIT
            d.op = PioOp::WAIT;
            d.polarity = ((lo8 >> 7) & 1u) != 0;
            d.source = static_cast<std::uint8_t>((lo8 >> 5) & 0x3u);
            d.index = static_cast<std::uint8_t>(lo8 & 0x1Fu);
            break;

        case 0b010:  // IN
            d.op = PioOp::IN;
            d.source = static_cast<std::uint8_t>((lo8 >> 5) & 0x7u);
            d.bit_count = static_cast<std::uint8_t>((lo8 & 0x1Fu) == 0 ? 32u : (lo8 & 0x1Fu));
            break;

        case 0b011:  // OUT
            d.op = PioOp::OUT;
            d.destination = static_cast<std::uint8_t>((lo8 >> 5) & 0x7u);
            d.bit_count = static_cast<std::uint8_t>((lo8 & 0x1Fu) == 0 ? 32u : (lo8 & 0x1Fu));
            break;

        case 0b100:  // PUSH / PULL
            if (((lo8 >> 7) & 1u) == 0) {
                d.op = PioOp::PUSH;
                d.if_full = ((lo8 >> 6) & 1u) != 0;
            } else {
                d.op = PioOp::PULL;
                d.if_empty = ((lo8 >> 6) & 1u) != 0;
            }
            d.block = ((lo8 >> 5) & 1u) != 0;
            break;

        case 0b101:  // MOV
            d.op = PioOp::MOV;
            d.destination = static_cast<std::uint8_t>((lo8 >> 5) & 0x7u);
            d.mov_op = static_cast<std::uint8_t>((lo8 >> 3) & 0x3u);
            d.source = static_cast<std::uint8_t>(lo8 & 0x7u);
            break;

        case 0b110:  // IRQ
            d.op = PioOp::IRQ;
            d.clear = ((lo8 >> 6) & 1u) != 0;
            d.wait = ((lo8 >> 5) & 1u) != 0;
            d.index = static_cast<std::uint8_t>(lo8 & 0x1Fu);
            break;

        default:  // 0b111  SET
            d.op = PioOp::SET;
            d.destination = static_cast<std::uint8_t>((lo8 >> 5) & 0x7u);
            d.data = static_cast<std::uint8_t>(lo8 & 0x1Fu);
            break;
    }
    return d;
}

const char* to_string(PioOp op) {
    switch (op) {
        case PioOp::JMP:  return "JMP";
        case PioOp::WAIT: return "WAIT";
        case PioOp::IN:   return "IN";
        case PioOp::OUT:  return "OUT";
        case PioOp::PUSH: return "PUSH";
        case PioOp::PULL: return "PULL";
        case PioOp::MOV:  return "MOV";
        case PioOp::IRQ:  return "IRQ";
        case PioOp::SET:  return "SET";
    }
    return "?";
}

}  // namespace rp2040
