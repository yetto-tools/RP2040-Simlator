// Unit tests for the UF2 firmware-container loader (BACKLOG P7.2).
#include "doctest.h"

#include <array>
#include <cstdint>
#include <vector>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "loaders/uf2_loader.h"

using namespace rp2040;

namespace {

constexpr std::uint32_t kMagic0 = 0x0A324655u;
constexpr std::uint32_t kMagic1 = 0x9E5D5157u;
constexpr std::uint32_t kMagicEnd = 0x0AB16F30u;
constexpr std::uint32_t kFamilyPresent = 0x00002000u;
constexpr std::uint32_t kNotMainFlash = 0x00000001u;

void wr32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
    b[off + 0] = static_cast<std::uint8_t>(v);
    b[off + 1] = static_cast<std::uint8_t>(v >> 8);
    b[off + 2] = static_cast<std::uint8_t>(v >> 16);
    b[off + 3] = static_cast<std::uint8_t>(v >> 24);
}

struct BlockSpec {
    std::uint32_t addr;
    std::vector<std::uint8_t> payload;
    std::uint32_t flags = kFamilyPresent;
    std::uint32_t family = kUf2FamilyRp2040;
};

// Assemble a well-formed UF2 stream from a list of block specs.
std::vector<std::uint8_t> build_uf2(const std::vector<BlockSpec>& blocks) {
    std::vector<std::uint8_t> out(blocks.size() * 512u, 0u);
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const BlockSpec& s = blocks[i];
        const std::size_t base = i * 512u;
        wr32(out, base + 0, kMagic0);
        wr32(out, base + 4, kMagic1);
        wr32(out, base + 8, s.flags);
        wr32(out, base + 12, s.addr);
        wr32(out, base + 16, static_cast<std::uint32_t>(s.payload.size()));
        wr32(out, base + 20, static_cast<std::uint32_t>(i));
        wr32(out, base + 24, static_cast<std::uint32_t>(blocks.size()));
        wr32(out, base + 28, s.family);
        for (std::size_t k = 0; k < s.payload.size(); ++k) out[base + 32 + k] = s.payload[k];
        wr32(out, base + 508, kMagicEnd);
    }
    return out;
}

}  // namespace

TEST_CASE("loads a two-block flash image and reports its span") {
    Memory mem;
    const std::vector<std::uint8_t> a(256, 0xAAu);
    const std::vector<std::uint8_t> b(256, 0xBBu);
    const auto uf2 = build_uf2({
        {0x10000000u, a},
        {0x10000100u, b},
    });

    const Uf2Image img = load_uf2(mem, uf2.data(), uf2.size());
    REQUIRE(img.ok);
    CHECK(img.blocks_loaded == 2);
    CHECK(img.blocks_skipped == 0);
    CHECK(img.family_id == kUf2FamilyRp2040);
    CHECK(img.lowest_addr == 0x10000000u);
    CHECK(img.highest_addr == 0x10000200u);
    CHECK(mem.read_word(0x10000000u).value == 0xAAAAAAAAu);
    CHECK(mem.read_word(0x100000FCu).value == 0xAAAAAAAAu);
    CHECK(mem.read_word(0x10000100u).value == 0xBBBBBBBBu);
}

TEST_CASE("skips blocks flagged not-main-flash") {
    Memory mem;
    const auto uf2 = build_uf2({
        {0x20000000u, std::vector<std::uint8_t>(16, 0x01u)},
        {0xE0000000u, std::vector<std::uint8_t>(16, 0x02u), kFamilyPresent | kNotMainFlash},
    });
    const Uf2Image img = load_uf2(mem, uf2.data(), uf2.size());
    REQUIRE(img.ok);
    CHECK(img.blocks_loaded == 1);
    CHECK(img.blocks_skipped == 1);
    CHECK(img.highest_addr == 0x20000010u);
}

TEST_CASE("accepts a block with no family ID") {
    Memory mem;
    const auto uf2 = build_uf2({{0x20000000u, std::vector<std::uint8_t>(4, 0x7Fu), 0u, 0u}});
    const Uf2Image img = load_uf2(mem, uf2.data(), uf2.size());
    REQUIRE(img.ok);
    CHECK(img.family_id == 0u);
}

TEST_CASE("rejects malformed UF2 streams") {
    Memory mem;
    const auto good = build_uf2({{0x20000000u, std::vector<std::uint8_t>(4, 0u)}});

    SUBCASE("length not a multiple of 512") {
        CHECK_FALSE(load_uf2(mem, good.data(), good.size() - 1).ok);
    }
    SUBCASE("bad start magic") {
        auto bad = good;
        wr32(bad, 0, 0xDEADBEEFu);
        CHECK_FALSE(load_uf2(mem, bad.data(), bad.size()).ok);
    }
    SUBCASE("bad end magic") {
        auto bad = good;
        wr32(bad, 508, 0u);
        CHECK_FALSE(load_uf2(mem, bad.data(), bad.size()).ok);
    }
    SUBCASE("wrong family ID") {
        const auto bad = build_uf2({{0x20000000u, {0, 0, 0, 0}, kFamilyPresent, 0x1C8A17FDu}});
        const Uf2Image img = load_uf2(mem, bad.data(), bad.size());
        CHECK_FALSE(img.ok);
        CHECK(img.error.find("family") != std::string::npos);
    }
    SUBCASE("payload larger than 476 bytes") {
        auto bad = good;
        wr32(bad, 16, 500u);
        CHECK_FALSE(load_uf2(mem, bad.data(), bad.size()).ok);
    }
    SUBCASE("numBlocks disagrees with the file") {
        auto bad = good;
        wr32(bad, 24, 9u);
        CHECK_FALSE(load_uf2(mem, bad.data(), bad.size()).ok);
    }
    SUBCASE("target address not backed by memory") {
        const auto bad = build_uf2({{0x30000000u, {1, 2, 3, 4}}});
        CHECK_FALSE(load_uf2(mem, bad.data(), bad.size()).ok);
    }
}

TEST_CASE("a UF2-loaded program runs on the CPU") {
    Memory mem;
    RegisterFile regs;
    Cpu cpu(regs, mem);

    // r0 = 4; do { r1 += r0; } while (--r0); then spin.
    const std::vector<std::uint8_t> prog{
        0x04, 0x20,  // movs r0, #4
        0x00, 0x21,  // movs r1, #0
        0x09, 0x18,  // adds r1, r1, r0
        0x01, 0x38,  // subs r0, #1
        0xFC, 0xD1,  // bne  -8
        0xFE, 0xE7,  // b .
    };
    const auto uf2 = build_uf2({{0x20000000u, prog}});
    const Uf2Image img = load_uf2(mem, uf2.data(), uf2.size());
    REQUIRE(img.ok);

    regs.set_pc(0x20000000u);
    regs.set_thumb(true);
    for (int i = 0; i < 100 && regs.pc() != 0x2000000Au; ++i) {
        REQUIRE(cpu.step() == ExecStatus::Ok);
    }
    CHECK(regs.get(1) == 10u);
    CHECK(regs.get(0) == 0u);
}
