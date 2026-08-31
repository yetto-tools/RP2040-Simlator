#include "pio/pio_disasm.h"

#include "pio_isa.h"

namespace rp2040 {

namespace {

const char* jmp_cond(std::uint8_t c) {
    switch (c) {
        case kJmpNotX:    return "!x ";
        case kJmpXDec:    return "x-- ";
        case kJmpNotY:    return "!y ";
        case kJmpYDec:    return "y-- ";
        case kJmpXNeY:    return "x!=y ";
        case kJmpPin:     return "pin ";
        case kJmpNotOsrE: return "!osre ";
        default:          return "";
    }
}

const char* in_src(std::uint8_t s) {
    switch (s) {
        case kInPins: return "pins";  case kInX: return "x";   case kInY: return "y";
        case kInNull: return "null";  case kInIsr: return "isr"; case kInOsr: return "osr";
        default: return "?";
    }
}
const char* out_dst(std::uint8_t d) {
    switch (d) {
        case kOutPins: return "pins"; case kOutX: return "x"; case kOutY: return "y";
        case kOutNull: return "null"; case kOutPindirs: return "pindirs"; case kOutPc: return "pc";
        case kOutIsr: return "isr"; case kOutExec: return "exec"; default: return "?";
    }
}
// MOV source: 0/1/2 PINS/X/Y, 3 NULL, 5 STATUS, 6/7 ISR/OSR.
const char* mov_src(std::uint8_t r) {
    switch (r) {
        case kMovPins: return "pins"; case kMovX: return "x"; case kMovY: return "y";
        case kMovNull: return "null"; case kMovStatus: return "status"; case kMovIsr: return "isr";
        case kMovOsr: return "osr"; default: return "?";
    }
}
// MOV destination uses a different encoding for 4/5 than MOV source
// (datasheet 3.4.6): 4 is EXEC, 5 is PC (not STATUS).
const char* mov_dst(std::uint8_t r) {
    switch (r) {
        case kMovPins: return "pins"; case kMovX: return "x"; case kMovY: return "y";
        case kMovDestExec: return "exec"; case kMovDestPc: return "pc";
        case kMovIsr: return "isr"; case kMovOsr: return "osr"; default: return "?";
    }
}
const char* set_dst(std::uint8_t d) {
    switch (d) {
        case kSetPins: return "pins"; case kSetX: return "x"; case kSetY: return "y";
        case kSetPindirs: return "pindirs"; default: return "?";
    }
}

std::string dec(unsigned v) { return std::to_string(v); }

}  // namespace

std::string pio_disassemble(std::uint16_t word, unsigned sideset_count, bool sideset_opt) {
    const PioInstr in = pio_decode(word);

    std::string s;
    switch (in.op) {
        case PioOp::JMP:
            s = "jmp " + std::string(jmp_cond(in.condition)) + dec(in.address);
            break;
        case PioOp::WAIT: {
            const char* src = in.source == kWaitGpio ? "gpio"
                            : in.source == kWaitPin  ? "pin" : "irq";
            s = "wait " + dec(in.polarity ? 1u : 0u) + " " + src + " " + dec(in.index & 0x0Fu);
            if (in.source == kWaitIrq && (in.index & 0x10u)) s += " rel";
            break;
        }
        case PioOp::IN:
            s = "in " + std::string(in_src(in.source)) + ", " + dec(in.bit_count);
            break;
        case PioOp::OUT:
            s = "out " + std::string(out_dst(in.destination)) + ", " + dec(in.bit_count);
            break;
        case PioOp::PUSH:
            s = "push";
            if (in.if_full) s += " iffull";
            s += in.block ? " block" : " noblock";
            break;
        case PioOp::PULL:
            s = "pull";
            if (in.if_empty) s += " ifempty";
            s += in.block ? " block" : " noblock";
            break;
        case PioOp::MOV: {
            const char* op = in.mov_op == kMovInvert ? "~"
                           : in.mov_op == kMovBitRev ? "::" : "";
            s = "mov " + std::string(mov_dst(in.destination)) + ", " + op +
                std::string(mov_src(in.source));
            break;
        }
        case PioOp::IRQ:
            s = "irq ";
            if (in.clear)      s += "clear ";
            else if (in.wait)  s += "wait ";
            else               s += "set ";
            s += dec(in.index & 0x0Fu);
            if (in.index & 0x10u) s += " rel";
            break;
        case PioOp::SET:
            s = "set " + std::string(set_dst(in.destination)) + ", " + dec(in.data);
            break;
    }

    // Split the [12:8] field into side-set value and delay.
    const unsigned field = in.delay_sideset;
    const unsigned total_ss = sideset_count + (sideset_opt ? 1u : 0u);
    const unsigned delay_bits = total_ss <= 5u ? 5u - total_ss : 0u;
    const unsigned delay = field & ((1u << delay_bits) - 1u);

    if (total_ss > 0) {
        const unsigned ss_field = field >> delay_bits;
        if (sideset_opt) {
            if (ss_field & (1u << sideset_count)) {
                s += " side " + dec(ss_field & ((1u << sideset_count) - 1u));
            }
        } else {
            s += " side " + dec(ss_field & ((1u << sideset_count) - 1u));
        }
    }
    if (delay > 0) s += " [" + dec(delay) + "]";
    return s;
}

}  // namespace rp2040
