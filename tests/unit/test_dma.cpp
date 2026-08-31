// Unit tests for the RP2040 DMA controller (datasheet 2.5).
#include "doctest.h"

#include <array>
#include <cstdint>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "peripherals/dma.h"

using namespace rp2040;

namespace {

constexpr std::uint32_t kSrc = 0x20001000u;
constexpr std::uint32_t kDst = 0x20002000u;

// CTRL builder. `treq` defaults to PERMANENT (0x3f = unpaced, one element/clock).
constexpr std::uint32_t ctrl(unsigned data_size, bool incr_r, bool incr_w,
                             unsigned chain_to, bool bswap = false, bool quiet = false,
                             unsigned treq = 0x3Fu, bool sniff = false) {
    return 1u                                   // EN
         | (data_size << 2)
         | (incr_r ? (1u << 4) : 0u)
         | (incr_w ? (1u << 5) : 0u)
         | (chain_to << 11)
         | (treq << 15)
         | (quiet ? (1u << 21) : 0u)
         | (bswap ? (1u << 22) : 0u)
         | (sniff ? (1u << 23) : 0u);
}

struct DmaFix {
    RegisterFile regs;
    Memory mem;
    Cpu cpu{regs, mem};
    Dma dma{cpu, mem};

    DmaFix() { REQUIRE(dma.attach(mem)); }

    void wr(std::uint32_t off, std::uint32_t v) {
        REQUIRE(mem.write_word(Dma::kBase + off, v) == BusStatus::Ok);
    }
    std::uint32_t rd(std::uint32_t off) { return mem.read_word(Dma::kBase + off).value; }

    // Give the DMA enough system clocks to drain any armed (unpaced) channel.
    void pump(std::uint64_t cycles = 8192) { dma.on_cycles(cycles); }

    // Program channel `ch` via the alias-0 group, trigger with CTRL_TRIG, and
    // run the transfer to completion (unpaced channels move one element/clock).
    void program(unsigned ch, std::uint32_t read, std::uint32_t write,
                 std::uint32_t count, std::uint32_t c) {
        const std::uint32_t b = ch * 0x40u;
        wr(b + 0x00, read);
        wr(b + 0x04, write);
        wr(b + 0x08, count);
        wr(b + 0x0C, c);   // CTRL_TRIG - arms the transfer
        pump();
    }

    // Same, but leave the transfer armed (for tests that pace it by hand).
    void program_no_pump(unsigned ch, std::uint32_t read, std::uint32_t write,
                         std::uint32_t count, std::uint32_t c) {
        const std::uint32_t b = ch * 0x40u;
        wr(b + 0x00, read);
        wr(b + 0x04, write);
        wr(b + 0x08, count);
        wr(b + 0x0C, c);
    }
};

}  // namespace

TEST_CASE_FIXTURE(DmaFix, "word copy with read+write increment") {
    for (unsigned i = 0; i < 4; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, 0x1000u + i) == BusStatus::Ok);

    program(0, kSrc, kDst, 4, ctrl(/*word*/2, true, true, /*chain=self*/0));

    for (unsigned i = 0; i < 4; ++i)
        CHECK(mem.read_word(kDst + 4 * i).value == 0x1000u + i);
    CHECK(dma.trans_count(0) == 0);
    CHECK_FALSE(dma.channel_busy(0));
    CHECK((dma.intr() & 1u) != 0);
}

TEST_CASE_FIXTURE(DmaFix, "fixed source, incrementing dest (memset-style fill)") {
    REQUIRE(mem.write_word(kSrc, 0xABCDABCDu) == BusStatus::Ok);
    program(1, kSrc, kDst, 3, ctrl(2, /*incr_r=*/false, /*incr_w=*/true, 1));
    for (unsigned i = 0; i < 3; ++i)
        CHECK(mem.read_word(kDst + 4 * i).value == 0xABCDABCDu);
}

TEST_CASE_FIXTURE(DmaFix, "byte-size transfer and byte-swap") {
    REQUIRE(mem.write_word(kSrc, 0x11223344u) == BusStatus::Ok);
    program(2, kSrc, kDst, 1, ctrl(/*word*/2, false, false, 2, /*bswap=*/true));
    CHECK(mem.read_word(kDst).value == 0x44332211u);
}

TEST_CASE_FIXTURE(DmaFix, "the alias trigger registers all start the channel") {
    REQUIRE(mem.write_word(kSrc, 0x5A5A5A5Au) == BusStatus::Ok);
    const std::uint32_t b = 3 * 0x40u;
    // Use AL3: CTRL(0x30), WRITE(0x34), COUNT(0x38), READ_ADDR_TRIG(0x3C).
    wr(b + 0x30, ctrl(2, false, false, 3));
    wr(b + 0x34, kDst);
    wr(b + 0x38, 1);
    wr(b + 0x3C, kSrc);   // READ_ADDR_TRIG
    pump();
    CHECK(mem.read_word(kDst).value == 0x5A5A5A5Au);
}

TEST_CASE_FIXTURE(DmaFix, "CHAIN_TO starts the next channel when this one finishes") {
    REQUIRE(mem.write_word(kSrc, 0xDEADu) == BusStatus::Ok);
    REQUIRE(mem.write_word(kSrc + 0x100, 0xBEEFu) == BusStatus::Ok);

    // ch4 copies one word, then chains to ch5.
    program(5, kSrc + 0x100, kDst + 0x100, 1, ctrl(2, false, false, /*chain=self*/5));
    program(4, kSrc, kDst, 1, ctrl(2, false, false, /*chain_to=*/5));

    CHECK(mem.read_word(kDst).value == 0xDEAD);
    CHECK(mem.read_word(kDst + 0x100).value == 0xBEEF);   // chained transfer ran
}

TEST_CASE_FIXTURE(DmaFix, "completion interrupt is gated by INTE0 and reaches the NVIC") {
    REQUIRE(mem.write_word(kSrc, 1u) == BusStatus::Ok);
    wr(0x404, 1u << 6);                  // INTE0: channel 6
    CHECK_FALSE(cpu.is_pending(Dma::kIrq0));

    program(6, kSrc, kDst, 1, ctrl(2, false, false, 6));
    CHECK((rd(0x400) & (1u << 6)) != 0); // INTR
    CHECK(cpu.is_pending(Dma::kIrq0));

    wr(0x400, 1u << 6);                  // INTR write-1-clear
    CHECK_FALSE(cpu.is_pending(Dma::kIrq0));
}

TEST_CASE_FIXTURE(DmaFix, "a read from unbacked memory sets READ_ERROR") {
    program(7, 0x30000000u /* unbacked */, kDst, 1, ctrl(2, true, true, 7));
    CHECK((rd(7 * 0x40u + 0x0C) & (1u << 30)) != 0);  // CTRL.READ_ERROR
}

TEST_CASE_FIXTURE(DmaFix, "ring buffer wraps the write address") {
    for (unsigned i = 0; i < 8; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, 0xC0DE00u + i) == BusStatus::Ok);
    // RING_SIZE=4 (16 bytes), RING_SEL=1 (wrap write).
    const std::uint32_t c = ctrl(2, true, true, 8) | (4u << 6) | (1u << 10);
    program(8, kSrc, kDst, 8, c);

    // 8 words into a 16-byte ring -> last 4 overwrite the first 4.
    CHECK(mem.read_word(kDst + 0).value == 0xC0DE04u);
    CHECK(mem.read_word(kDst + 12).value == 0xC0DE07u);
}

TEST_CASE_FIXTURE(DmaFix, "an unpaced channel moves one element per system clock") {
    for (unsigned i = 0; i < 10; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, i) == BusStatus::Ok);

    const std::uint32_t b = 0;
    wr(b + 0x00, kSrc);
    wr(b + 0x04, kDst);
    wr(b + 0x08, 10);
    wr(b + 0x0C, ctrl(2, true, true, 0));   // CTRL_TRIG, TREQ = PERMANENT
    CHECK(dma.channel_busy(0));
    CHECK(dma.remaining(0) == 10u);

    dma.on_cycles(4);
    CHECK(dma.remaining(0) == 6u);          // 4 elements in 4 clocks
    CHECK(mem.read_word(kDst + 12).value == 3u);
    CHECK(mem.read_word(kDst + 16).value == 0u);   // not there yet

    dma.on_cycles(6);
    CHECK_FALSE(dma.channel_busy(0));
    CHECK(dma.trans_count(0) == 0u);
    CHECK((dma.intr() & 1u) != 0);
}

TEST_CASE_FIXTURE(DmaFix, "a DMA pacing timer throttles the transfer rate") {
    for (unsigned i = 0; i < 4; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, 0xAA00u + i) == BusStatus::Ok);

    wr(0x420, (1u << 16) | 3u);             // TIMER0: X=1, Y=3 -> 1 element / 3 clocks
    program_no_pump(1, kSrc, kDst, 4, ctrl(2, true, true, 1, false, false, /*treq=TIMER0*/0x3Bu));

    dma.on_cycles(2);
    CHECK(dma.remaining(1) == 4u);          // nothing yet (2 < 3 clocks)
    dma.on_cycles(1);
    CHECK(dma.remaining(1) == 3u);          // one element after 3 clocks
    dma.on_cycles(9);
    CHECK_FALSE(dma.channel_busy(1));       // 3 more elements over 9 clocks
}

TEST_CASE_FIXTURE(DmaFix, "a registered DREQ source paces the channel, at most one/clock") {
    for (unsigned i = 0; i < 3; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, 0x1000u + i) == BusStatus::Ok);

    bool ready = false;
    dma.set_dreq_source(20, [&] { return ready; });  // DREQ_UART0_TX == 20
    program_no_pump(0, kSrc, kDst, 3, ctrl(2, true, true, 0, false, false, /*treq=*/20));

    dma.on_cycles(5);
    CHECK(dma.remaining(0) == 3u);   // source never reports ready: no progress

    ready = true;
    dma.on_cycles(1);
    CHECK(dma.remaining(0) == 2u);   // exactly one element per clock while ready
    dma.on_cycles(1);
    CHECK(dma.remaining(0) == 1u);
    dma.on_cycles(1);
    CHECK_FALSE(dma.channel_busy(0));
    CHECK(mem.read_word(kDst + 8).value == 0x1002u);
}

TEST_CASE_FIXTURE(DmaFix, "an unregistered DREQ number still falls back to dreq_divisor()") {
    for (unsigned i = 0; i < 2; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, i) == BusStatus::Ok);

    dma.set_dreq_divisor(3);
    // DREQ 0 (DREQ_PIO0_TX0) has no registered source in this fixture.
    program_no_pump(0, kSrc, kDst, 2, ctrl(2, true, true, 0, false, false, /*treq=*/0));

    dma.on_cycles(2);
    CHECK(dma.remaining(0) == 2u);   // < 3 clocks
    dma.on_cycles(1);
    CHECK(dma.remaining(0) == 1u);   // one element every 3 clocks
}

TEST_CASE_FIXTURE(DmaFix, "sniff: CRC-32 (reversed) of \"123456789\" == 0xCBF43926") {
    const char* msg = "123456789";
    for (unsigned i = 0; i < 9; ++i)
        REQUIRE(mem.write_byte(kSrc + i, static_cast<std::uint8_t>(msg[i])) == BusStatus::Ok);

    wr(0x434, (1u << 0) | (0u << 1) | (0x1u << 5) | (1u << 11));  // EN, DMACH0, CALC=CRC32R, OUT_INV
    wr(0x438, 0xFFFFFFFFu);                                       // seed
    program(0, kSrc, kDst, 9, ctrl(/*byte*/0, true, false, 0, false, false, 0x3Fu, /*sniff=*/true));

    CHECK(rd(0x438) == 0xCBF43926u);   // == zlib crc32("123456789")
}

TEST_CASE_FIXTURE(DmaFix, "sniff: CRC-16-CCITT of \"123456789\" == 0x29B1") {
    const char* msg = "123456789";
    for (unsigned i = 0; i < 9; ++i)
        REQUIRE(mem.write_byte(kSrc + i, static_cast<std::uint8_t>(msg[i])) == BusStatus::Ok);

    wr(0x434, (1u << 0) | (0x2u << 5));   // EN, DMACH0, CALC = CRC-16-CCITT
    wr(0x438, 0x0000FFFFu);               // init 0xFFFF
    program(0, kSrc, kDst, 9, ctrl(0, true, false, 0, false, false, 0x3Fu, true));

    CHECK((rd(0x438) & 0xFFFFu) == 0x29B1u);
}

TEST_CASE_FIXTURE(DmaFix, "sniff: sum and XOR reduction modes") {
    for (unsigned i = 0; i < 4; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, 0x10u + i) == BusStatus::Ok);

    wr(0x434, (1u << 0) | (0xFu << 5));   // CALC = sum
    wr(0x438, 0u);
    program(0, kSrc, kDst, 4, ctrl(2, true, false, 0, false, false, 0x3Fu, true));
    CHECK(rd(0x438) == (0x10u + 0x11u + 0x12u + 0x13u));

    wr(0x434, (1u << 0) | (0xEu << 5));   // CALC = XOR reduction
    wr(0x438, 0u);
    program(1, kSrc, kDst, 4, ctrl(2, true, false, 1, false, false, 0x3Fu, true) | 0u);
    // note: SNIFF_CTRL.DMACH still 0 -> channel 1 is NOT sniffed
    CHECK(rd(0x438) == 0u);

    wr(0x434, (1u << 0) | (1u << 1) | (0xEu << 5));   // DMACH = 1
    wr(0x438, 0u);
    program(1, kSrc, kDst, 4, ctrl(2, true, false, 1, false, false, 0x3Fu, true));
    CHECK(rd(0x438) == (0x10u ^ 0x11u ^ 0x12u ^ 0x13u));
}

TEST_CASE_FIXTURE(DmaFix, "sniff: OUT_REV bit-reverses the read-back value") {
    REQUIRE(mem.write_word(kSrc, 0x12345678u) == BusStatus::Ok);
    wr(0x434, (1u << 0) | (0xFu << 5) | (1u << 10));   // sum + OUT_REV
    wr(0x438, 0u);
    program(0, kSrc, kDst, 1, ctrl(2, true, false, 0, false, false, 0x3Fu, true));
    // accumulator holds 0x12345678; read-back is its bit-reversal.
    CHECK(rd(0x438) == 0x1E6A2C48u);
}

TEST_CASE_FIXTURE(DmaFix, "CHAN_ABORT stops a paced transfer mid-flight") {
    for (unsigned i = 0; i < 20; ++i)
        REQUIRE(mem.write_word(kSrc + 4 * i, i) == BusStatus::Ok);

    program_no_pump(2, kSrc, kDst, 20, ctrl(2, true, true, 2));
    dma.on_cycles(5);
    CHECK(dma.remaining(2) == 15u);

    wr(0x444, 1u << 2);                     // CHAN_ABORT channel 2
    CHECK_FALSE(dma.channel_busy(2));
    CHECK(dma.remaining(2) == 0u);
    dma.on_cycles(100);                     // stays aborted
    CHECK(mem.read_word(kDst + 4 * 5).value == 0u);   // element 5 never copied
}
