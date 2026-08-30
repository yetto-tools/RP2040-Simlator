// Unit tests for the memory subsystem (BACKLOG P1.3).
// Behaviour reference: ARCHITECTURE.md section 2.
#include "doctest.h"

#include <array>
#include <cstdint>

#include "core/bus.h"
#include "core/memory.h"

using rp2040::BusStatus;
using rp2040::BusWidth;
using rp2040::Memory;

namespace {

constexpr std::uint32_t kRam = rp2040::kSramBase;

}  // namespace

TEST_CASE("fresh memory reads back as zero") {
    Memory mem;
    CHECK(mem.read_word(kRam).ok());
    CHECK(mem.read_word(kRam).value == 0u);
    CHECK(mem.read_byte(rp2040::kRomBase).value == 0u);
    CHECK(mem.read_word(rp2040::kFlashBase).value == 0u);
}

TEST_CASE("SRAM round-trips each access width") {
    Memory mem;

    CHECK(mem.write_word(kRam, 0xDEADBEEFu) == BusStatus::Ok);
    CHECK(mem.read_word(kRam).value == 0xDEADBEEFu);

    CHECK(mem.write_half(kRam + 4, 0xCAFEu) == BusStatus::Ok);
    CHECK(mem.read_half(kRam + 4).value == 0xCAFEu);

    CHECK(mem.write_byte(kRam + 8, 0x5Au) == BusStatus::Ok);
    CHECK(mem.read_byte(kRam + 8).value == 0x5Au);
}

TEST_CASE("storage is little-endian") {
    Memory mem;
    REQUIRE(mem.write_word(kRam, 0x11223344u) == BusStatus::Ok);

    CHECK(mem.read_byte(kRam + 0).value == 0x44u);
    CHECK(mem.read_byte(kRam + 1).value == 0x33u);
    CHECK(mem.read_byte(kRam + 2).value == 0x22u);
    CHECK(mem.read_byte(kRam + 3).value == 0x11u);

    CHECK(mem.read_half(kRam + 0).value == 0x3344u);
    CHECK(mem.read_half(kRam + 2).value == 0x1122u);
}

TEST_CASE("byte writes compose into a half/word (little-endian)") {
    Memory mem;
    REQUIRE(mem.write_byte(kRam + 0, 0x0Du) == BusStatus::Ok);
    REQUIRE(mem.write_byte(kRam + 1, 0xF0u) == BusStatus::Ok);
    CHECK(mem.read_half(kRam).value == 0xF00Du);
}

TEST_CASE("unaligned half/word accesses fault; bytes never do") {
    Memory mem;

    CHECK(mem.read_half(kRam + 1).status == BusStatus::MisalignedAccess);
    CHECK(mem.write_half(kRam + 3, 0x1234u) == BusStatus::MisalignedAccess);

    CHECK(mem.read_word(kRam + 1).status == BusStatus::MisalignedAccess);
    CHECK(mem.read_word(kRam + 2).status == BusStatus::MisalignedAccess);
    CHECK(mem.write_word(kRam + 3, 0u) == BusStatus::MisalignedAccess);

    CHECK(mem.read_byte(kRam + 1).ok());
    CHECK(mem.write_byte(kRam + 3, 0xAAu) == BusStatus::Ok);
}

TEST_CASE("a faulted read leaves value at zero and does not write memory") {
    Memory mem;
    REQUIRE(mem.write_word(kRam, 0xFFFFFFFFu) == BusStatus::Ok);

    auto r = mem.read_word(kRam + 2);
    CHECK_FALSE(r.ok());
    CHECK(r.value == 0u);

    CHECK(mem.write_word(kRam + 2, 0x12345678u) == BusStatus::MisalignedAccess);
    CHECK(mem.read_word(kRam).value == 0xFFFFFFFFu);  // unchanged
}

TEST_CASE("ROM and flash reject direct CPU stores") {
    Memory mem;
    CHECK(mem.write_byte(rp2040::kRomBase, 0x1u) == BusStatus::WriteToReadOnly);
    CHECK(mem.write_word(rp2040::kRomBase, 0x1u) == BusStatus::WriteToReadOnly);
    CHECK(mem.write_word(rp2040::kFlashBase, 0x1u) == BusStatus::WriteToReadOnly);

    CHECK(mem.read_byte(rp2040::kRomBase).value == 0u);  // still zero
}

TEST_CASE("addresses outside every region fault as InvalidAddress") {
    Memory mem;
    CHECK(mem.read_word(0x08000000u).status == BusStatus::InvalidAddress);   // between ROM and flash
    CHECK(mem.read_word(0x30000000u).status == BusStatus::InvalidAddress);   // between SRAM and regs
    CHECK(mem.write_byte(0xF0000000u, 0u) == BusStatus::InvalidAddress);
}

TEST_CASE("region boundaries: last valid word vs first invalid") {
    Memory mem;
    const std::uint32_t last_word = kRam + rp2040::kSramSize - 4;
    CHECK(mem.write_word(last_word, 0xABCDEF01u) == BusStatus::Ok);
    CHECK(mem.read_word(last_word).value == 0xABCDEF01u);

    CHECK(mem.read_byte(kRam + rp2040::kSramSize).status == BusStatus::InvalidAddress);
    CHECK(mem.read_word(kRam + rp2040::kSramSize - 2).status == BusStatus::MisalignedAccess);
}

TEST_CASE("backdoor load/dump bypasses write protection") {
    Memory mem;
    const std::array<std::uint8_t, 4> prog{0x01, 0x02, 0x03, 0x04};

    REQUIRE(mem.load(rp2040::kFlashBase, prog.data(), prog.size()));
    CHECK(mem.read_word(rp2040::kFlashBase).value == 0x04030201u);

    std::array<std::uint8_t, 4> back{};
    REQUIRE(mem.dump(rp2040::kFlashBase, back.data(), back.size()));
    CHECK(back == prog);
}

TEST_CASE("backdoor rejects spans not fully backed by one region") {
    Memory mem;
    std::array<std::uint8_t, 8> buf{};
    CHECK_FALSE(mem.load(kRam + rp2040::kSramSize - 4, buf.data(), buf.size()));
    CHECK_FALSE(mem.load(0x30000000u, buf.data(), buf.size()));
    CHECK_FALSE(mem.dump(0x30000000u, buf.data(), buf.size()));
    CHECK(mem.load(kRam, buf.data(), 0));  // zero-length is a no-op success
}

// --- Peripheral routing ---------------------------------------------------

namespace {

class FakePeripheral : public rp2040::BusPeripheral {
public:
    rp2040::BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override {
        last_read_offset = offset;
        last_read_width = w;
        if (offset == 0xF0u) {
            return rp2040::BusResult<std::uint32_t>::fail(BusStatus::PeripheralError);
        }
        return {reg[(offset & 0xFFu) >> 2], BusStatus::Ok};
    }
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override {
        last_write_offset = offset;
        last_write_width = w;
        last_write_value = value;
        if (offset == 0xF0u) return BusStatus::PeripheralError;
        reg[(offset & 0xFFu) >> 2] = value;
        return BusStatus::Ok;
    }

    std::array<std::uint32_t, 64> reg{};
    std::uint32_t last_read_offset = 0, last_write_offset = 0, last_write_value = 0;
    BusWidth last_read_width{}, last_write_width{};
};

}  // namespace

TEST_CASE("register-space access routes to the attached peripheral by offset") {
    Memory mem;
    FakePeripheral p;
    REQUIRE(mem.attach_peripheral(rp2040::kGpioBase, 0x1000u, &p));

    REQUIRE(mem.write_word(rp2040::kGpioBase + 0x10u, 0xA5A5u) == BusStatus::Ok);
    CHECK(p.last_write_offset == 0x10u);
    CHECK(p.last_write_width == BusWidth::Word);
    CHECK(p.last_write_value == 0xA5A5u);

    CHECK(mem.read_word(rp2040::kGpioBase + 0x10u).value == 0xA5A5u);
    CHECK(p.last_read_offset == 0x10u);
}

TEST_CASE("peripheral errors and unmapped register space are distinct faults") {
    Memory mem;
    FakePeripheral p;
    REQUIRE(mem.attach_peripheral(rp2040::kGpioBase, 0x1000u, &p));

    CHECK(mem.read_word(rp2040::kGpioBase + 0xF0u).status == BusStatus::PeripheralError);
    CHECK(mem.write_word(rp2040::kGpioBase + 0xF0u, 0u) == BusStatus::PeripheralError);

    // Register space with nothing mapped there.
    CHECK(mem.read_word(rp2040::kUart0Base).status == BusStatus::InvalidAddress);
}

TEST_CASE("peripheral access still obeys alignment before dispatch") {
    Memory mem;
    FakePeripheral p;
    REQUIRE(mem.attach_peripheral(rp2040::kGpioBase, 0x1000u, &p));

    CHECK(mem.read_word(rp2040::kGpioBase + 0x2u).status == BusStatus::MisalignedAccess);
    CHECK(p.last_read_offset == 0u);  // never dispatched
}

TEST_CASE("attach_peripheral rejects overlap and out-of-range mappings") {
    Memory mem;
    FakePeripheral a, b;
    REQUIRE(mem.attach_peripheral(rp2040::kGpioBase, 0x1000u, &a));

    CHECK_FALSE(mem.attach_peripheral(rp2040::kGpioBase + 0x800u, 0x1000u, &b));  // overlaps a
    CHECK_FALSE(mem.attach_peripheral(kRam, 0x1000u, &b));                        // not register space
    CHECK(mem.attach_peripheral(rp2040::kUart0Base, 0x1000u, &b));               // fine
}
