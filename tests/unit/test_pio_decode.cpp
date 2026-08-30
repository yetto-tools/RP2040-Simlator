// Unit tests for the RP2040 PIO instruction decoder (datasheet 3.4).
// Encodings cross-checked against the pioasm output format.
#include "doctest.h"

#include <cstdint>

#include "pio_isa.h"

using namespace rp2040;

namespace {
PioInstr d(std::uint16_t w) { return pio_decode(w); }
}  // namespace

TEST_CASE("JMP: opcode 000, condition and 5-bit target") {
    auto j = d(0x0000);                  // jmp 0 (always)
    CHECK(j.op == PioOp::JMP);
    CHECK(j.condition == kJmpAlways);
    CHECK(j.address == 0);

    auto jx = d(0x0024);                 // jmp !x, 4   -> 000 00000 001 00100
    CHECK(jx.op == PioOp::JMP);
    CHECK(jx.condition == kJmpNotX);
    CHECK(jx.address == 4);

    auto jd = d(0x0045);                 // jmp x--, 5  -> cond 010
    CHECK(jd.condition == kJmpXDec);
    CHECK(jd.address == 5);
}

TEST_CASE("delay / side-set field is bits [12:8]") {
    auto j = d(0x1F00);                  // jmp 0, [31]
    CHECK(j.op == PioOp::JMP);
    CHECK(j.delay_sideset == 0x1F);
}

TEST_CASE("WAIT: opcode 001, polarity / source / index") {
    auto w = d(0x20C0);                  // wait 1 irq 0  -> 001 00000 1 10 00000
    CHECK(w.op == PioOp::WAIT);
    CHECK(w.polarity);
    CHECK(w.source == kWaitIrq);
    CHECK(w.index == 0);

    auto w0 = d(0x2020);                 // wait 0 pin 0  -> 001 00000 0 01 00000
    CHECK_FALSE(w0.polarity);
    CHECK(w0.source == kWaitPin);
}

TEST_CASE("IN: opcode 010, source and bit count (0 => 32)") {
    auto i = d(0x4000);                  // in  pins, 32  -> 010 00000 000 00000
    CHECK(i.op == PioOp::IN);
    CHECK(i.source == kInPins);
    CHECK(i.bit_count == 32);

    auto i5 = d(0x4005);                 // in  pins, 5
    CHECK(i5.bit_count == 5);

    auto ix = d(0x4028);                 // in  x, 8      -> source 001
    CHECK(ix.source == kInX);
    CHECK(ix.bit_count == 8);
}

TEST_CASE("OUT: opcode 011, destination and bit count") {
    auto o = d(0x6001);                  // out pins, 1
    CHECK(o.op == PioOp::OUT);
    CHECK(o.destination == kOutPins);
    CHECK(o.bit_count == 1);

    auto oy = d(0x6040);                 // out y, 32     -> dest 010, count 0=>32
    CHECK(oy.destination == kOutY);
    CHECK(oy.bit_count == 32);

    auto opc = d(0x60A0);                // out pc, 32    -> dest 101
    CHECK(opc.destination == kOutPc);
}

TEST_CASE("PUSH / PULL: opcode 100, bit 7 selects, iffull/ifempty/block") {
    auto p = d(0x8000);                  // push noblock       -> 100 00000 0 0 0 00000
    CHECK(p.op == PioOp::PUSH);
    CHECK_FALSE(p.if_full);
    CHECK_FALSE(p.block);

    auto pb = d(0x8020);                 // push block
    CHECK(pb.op == PioOp::PUSH);
    CHECK(pb.block);

    auto pf = d(0x8040);                 // push iffull noblock
    CHECK(pf.if_full);

    auto pull = d(0x8080);               // pull noblock
    CHECK(pull.op == PioOp::PULL);
    CHECK_FALSE(pull.block);

    auto pullb = d(0x80A0);              // pull block
    CHECK(pullb.op == PioOp::PULL);
    CHECK(pullb.block);
    CHECK_FALSE(pullb.if_empty);

    auto pulle = d(0x80E0);              // pull ifempty block
    CHECK(pulle.if_empty);
    CHECK(pulle.block);
}

TEST_CASE("MOV: opcode 101, dest [7:5], op [4:3], source [2:0]") {
    auto m = d(0xA027);                  // mov x, osr   -> 101 00000 001 00 111
    CHECK(m.op == PioOp::MOV);
    CHECK(m.destination == kMovX);
    CHECK(m.mov_op == kMovNone);
    CHECK(m.source == kMovOsr);

    auto inv = d(0xA02F);                // mov x, ~osr  -> op 01
    CHECK(inv.mov_op == kMovInvert);

    auto rev = d(0xA037);                // mov x, ::osr -> op 10
    CHECK(rev.mov_op == kMovBitRev);

    auto me = d(0xA0E6);                 // mov osr, isr -> dest 111, src 110
    CHECK(me.destination == kMovOsr);
    CHECK(me.source == kMovIsr);
}

TEST_CASE("IRQ: opcode 110, clear / wait / index") {
    auto s = d(0xC001);                  // irq set 1
    CHECK(s.op == PioOp::IRQ);
    CHECK_FALSE(s.clear);
    CHECK_FALSE(s.wait);
    CHECK(s.index == 1);

    auto w = d(0xC021);                  // irq wait 1  -> wait bit 5
    CHECK(w.wait);

    auto c = d(0xC041);                  // irq clear 1 -> clear bit 6
    CHECK(c.clear);
}

TEST_CASE("SET: opcode 111, destination and 5-bit data") {
    auto sx = d(0xE03F);                 // set x, 31    -> 111 00000 001 11111
    CHECK(sx.op == PioOp::SET);
    CHECK(sx.destination == kSetX);
    CHECK(sx.data == 31);

    auto sp = d(0xE000);                 // set pins, 0
    CHECK(sp.destination == kSetPins);
    CHECK(sp.data == 0);

    auto sd = d(0xE081);                 // set pindirs, 1 -> dest 100
    CHECK(sd.destination == kSetPindirs);
    CHECK(sd.data == 1);
}
