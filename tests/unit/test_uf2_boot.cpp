// Unit tests for Simulator::load()'s UF2 boot path (simulator.cpp).
//
// Real RP2040 flash images carry a mandatory 256-byte stage-2 bootloader
// ahead of the vector table - the boot ROM validates and runs it before
// anything else, and every pico-sdk boot2_*.S source (and this repo's own
// picoOS test firmware) is exactly that size. A RAM-resident image, which
// the boot ROM never touches, has no such stub.
#include "doctest.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "loaders/uf2_loader.h"
#include "simulator.h"

using namespace rp2040;

namespace {
namespace fs = std::filesystem;

constexpr std::uint32_t kMagic0 = 0x0A324655u;
constexpr std::uint32_t kMagic1 = 0x9E5D5157u;
constexpr std::uint32_t kMagicEnd = 0x0AB16F30u;
constexpr std::uint32_t kFamilyPresent = 0x00002000u;

void wr32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
    b[off + 0] = static_cast<std::uint8_t>(v);
    b[off + 1] = static_cast<std::uint8_t>(v >> 8);
    b[off + 2] = static_cast<std::uint8_t>(v >> 16);
    b[off + 3] = static_cast<std::uint8_t>(v >> 24);
}

// [sp, pc] vector table word pair at offset 0/4 of an 8-byte buffer.
std::vector<std::uint8_t> vector_table(std::uint32_t sp, std::uint32_t pc) {
    std::vector<std::uint8_t> v(8, 0u);
    wr32(v, 0, sp);
    wr32(v, 4, pc);
    return v;
}

// Writes `payload` as a single UF2 block loaded at `addr`, to a fresh temp
// file, and returns its path.
fs::path write_uf2(std::uint32_t addr, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out(512u, 0u);
    wr32(out, 0, kMagic0);
    wr32(out, 4, kMagic1);
    wr32(out, 8, kFamilyPresent);
    wr32(out, 12, addr);
    wr32(out, 16, static_cast<std::uint32_t>(payload.size()));
    wr32(out, 20, 0u);
    wr32(out, 24, 1u);
    wr32(out, 28, kUf2FamilyRp2040);
    for (std::size_t k = 0; k < payload.size(); ++k) out[32 + k] = payload[k];
    wr32(out, 508, kMagicEnd);

    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path =
        fs::temp_directory_path() / ("rp2040_test_uf2_boot_" + std::to_string(id) + ".uf2");
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return path;
}

}  // namespace

TEST_CASE("Simulator::load(.uf2): a flash image boots past its 256-byte boot2 stub") {
    // 256 bytes of boot2 filler, then the real vector table at +0x100 - the
    // shape every pico-sdk / picoOS link script produces.
    std::vector<std::uint8_t> image(264, 0xE7u);
    wr32(image, 256, 0x20040000u);
    wr32(image, 260, 0x10000101u);  // thumb bit set

    const fs::path path = write_uf2(0x10000000u, image);
    Simulator sim;
    const ElfImage img = sim.load(path.string(), false);
    fs::remove(path);

    REQUIRE(img.ok);
    CHECK(sim.regs().sp() == 0x20040000u);
    CHECK(sim.regs().pc() == 0x10000100u);   // the app's reset handler, not the boot2 stub
}

TEST_CASE("Simulator::load(.uf2): a RAM-resident image has no boot2 stub to skip") {
    const std::vector<std::uint8_t> vt = vector_table(0x20040000u, 0x20000101u);
    const fs::path path = write_uf2(0x20000000u, vt);
    Simulator sim;
    const ElfImage img = sim.load(path.string(), false);
    fs::remove(path);

    REQUIRE(img.ok);
    CHECK(sim.regs().sp() == 0x20040000u);
    CHECK(sim.regs().pc() == 0x20000100u);
}
