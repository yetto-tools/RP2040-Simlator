// Unit tests for the ARMv6-M ALU primitives (add_with_carry / shift_c).
// Reference: ARMv6-M ARM A2.2.1 (AddWithCarry) and A2.2.4 (Shift_C).
#include "doctest.h"

#include <cstdint>

#include "core/alu.h"

using rp2040::add_with_carry;
using rp2040::shift_c;
using rp2040::SRType;

TEST_CASE("add_with_carry: plain addition") {
    auto r = add_with_carry(2, 3, false);
    CHECK(r.value == 5);
    CHECK_FALSE(r.carry);
    CHECK_FALSE(r.overflow);
}

TEST_CASE("add_with_carry: unsigned carry out") {
    auto r = add_with_carry(0xFFFFFFFFu, 1, false);
    CHECK(r.value == 0);
    CHECK(r.carry);
    CHECK_FALSE(r.overflow);
}

TEST_CASE("add_with_carry: signed overflow (pos + pos -> neg)") {
    auto r = add_with_carry(0x7FFFFFFFu, 1, false);
    CHECK(r.value == 0x80000000u);
    CHECK_FALSE(r.carry);
    CHECK(r.overflow);
}

TEST_CASE("add_with_carry: subtraction idiom (x + ~y + 1)") {
    SUBCASE("5 - 3 = 2, borrow-clear means carry set") {
        auto r = add_with_carry(5, ~3u, true);
        CHECK(r.value == 2);
        CHECK(r.carry);            // no borrow
        CHECK_FALSE(r.overflow);
    }
    SUBCASE("3 - 5 = -2, borrow means carry clear") {
        auto r = add_with_carry(3, ~5u, true);
        CHECK(r.value == 0xFFFFFFFEu);
        CHECK_FALSE(r.carry);      // borrow
        CHECK_FALSE(r.overflow);
    }
    SUBCASE("INT_MIN - 1 overflows") {
        auto r = add_with_carry(0x80000000u, ~1u, true);  // 0x80000000 - 1
        CHECK(r.value == 0x7FFFFFFFu);
        CHECK(r.overflow);
    }
}

TEST_CASE("shift_c LSL") {
    CHECK(shift_c(0x1, SRType::LSL, 0, false).value == 0x1);       // amount 0: untouched
    CHECK(shift_c(0x1, SRType::LSL, 0, true).carry == true);       // carry_in passes through

    auto s = shift_c(0x00000001u, SRType::LSL, 4, false);
    CHECK(s.value == 0x10);
    CHECK_FALSE(s.carry);

    auto top = shift_c(0x80000000u, SRType::LSL, 1, false);
    CHECK(top.value == 0);
    CHECK(top.carry);                                              // bit shifted out

    CHECK(shift_c(0x3u, SRType::LSL, 32, false).value == 0);
    CHECK(shift_c(0x3u, SRType::LSL, 32, false).carry == true);    // bit0 -> carry
    CHECK(shift_c(0x3u, SRType::LSL, 33, false).carry == false);
}

TEST_CASE("shift_c LSR") {
    auto s = shift_c(0x10u, SRType::LSR, 4, false);
    CHECK(s.value == 1);
    CHECK_FALSE(s.carry);

    auto c = shift_c(0x18u, SRType::LSR, 4, false);
    CHECK(c.value == 1);
    CHECK(c.carry);                                                // bit 3 was set

    CHECK(shift_c(0x80000000u, SRType::LSR, 32, false).value == 0);
    CHECK(shift_c(0x80000000u, SRType::LSR, 32, false).carry == true);
}

TEST_CASE("shift_c ASR keeps the sign bit") {
    auto s = shift_c(0x80000000u, SRType::ASR, 4, false);
    CHECK(s.value == 0xF8000000u);
    CHECK_FALSE(s.carry);

    CHECK(shift_c(0x80000000u, SRType::ASR, 32, false).value == 0xFFFFFFFFu);
    CHECK(shift_c(0x80000000u, SRType::ASR, 40, false).value == 0xFFFFFFFFu);
    CHECK(shift_c(0x40000000u, SRType::ASR, 40, false).value == 0);
}

TEST_CASE("shift_c ROR rotates") {
    auto s = shift_c(0x00000001u, SRType::ROR, 1, false);
    CHECK(s.value == 0x80000000u);
    CHECK(s.carry);

    CHECK(shift_c(0x12345678u, SRType::ROR, 8, false).value == 0x78123456u);
    CHECK(shift_c(0x12345678u, SRType::ROR, 32, false).value == 0x12345678u);
}

TEST_CASE("shift_c RRX feeds carry into bit 31") {
    auto s = shift_c(0x00000002u, SRType::RRX, 1, true);
    CHECK(s.value == 0x80000001u);
    CHECK_FALSE(s.carry);                                          // bit0 was 0
    auto c = shift_c(0x00000003u, SRType::RRX, 1, false);
    CHECK(c.value == 0x00000001u);
    CHECK(c.carry);
}
