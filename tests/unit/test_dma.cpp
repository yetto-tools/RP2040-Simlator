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

// CTRL builder.
constexpr std::uint32_t ctrl(unsigned data_size, bool incr_r, bool incr_w,
                             unsigned chain_to, bool bswap = false, bool quiet = false) {
    return 1u                                   // EN
         | (data_size << 2)
         | (incr_r ? (1u << 4) : 0u)
         | (incr_w ? (1u << 5) : 0u)
         | (chain_to << 11)
         | (bswap ? (1u << 22) : 0u)
         | (quiet ? (1u << 21) : 0u);
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

    // Program channel `ch` via the alias-0 group and trigger with CTRL_TRIG.
    void program(unsigned ch, std::uint32_t read, std::uint32_t write,
                 std::uint32_t count, std::uint32_t c) {
        const std::uint32_t b = ch * 0x40u;
        wr(b + 0x00, read);
        wr(b + 0x04, write);
        wr(b + 0x08, count);
        wr(b + 0x0C, c);   // CTRL_TRIG - starts the transfer
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
