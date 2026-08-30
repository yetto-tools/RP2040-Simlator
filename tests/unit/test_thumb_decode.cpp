// Unit tests for the ARMv6-M Thumb decoder (BACKLOG P1.2).
// Encodings hand-verified against the ARMv6-M ARM (DDI 0419) instruction tables.
#include "doctest.h"

#include <cstdint>

#include "thumb_isa.h"

using namespace rp2040;

namespace {
DecodedInstr d16(std::uint16_t x) { return decode_thumb16(x); }
}  // namespace

TEST_CASE("32-bit instruction detection (ARMv6-M A5.1)") {
    CHECK(is_32bit_thumb(0xF000));  // BL first halfword
    CHECK(is_32bit_thumb(0xF3BF));  // DSB/DMB/ISB
    CHECK(is_32bit_thumb(0xE800));  // 0b11101 prefix
    CHECK(is_32bit_thumb(0xF800));
    CHECK_FALSE(is_32bit_thumb(0x2001));  // MOVS
    CHECK_FALSE(is_32bit_thumb(0xE7FE));  // B (T2), 0b11100
    CHECK_FALSE(is_32bit_thumb(0xBF00));  // NOP
}

TEST_CASE("A5.2.1 shift / add / sub / move / compare") {
    SUBCASE("LSLS r0, r1, #2") {
        auto d = d16(0x0088);
        CHECK(d.op == Mnemonic::LSL_imm);
        CHECK(d.rd == 0); CHECK(d.rm == 1); CHECK(d.imm == 2);
        CHECK(d.setflags);
    }
    SUBCASE("LSLS with imm 0 decodes as MOVS (register)") {
        auto d = d16(0x0008);  // LSLS r0, r1, #0
        CHECK(d.op == Mnemonic::MOV_reg);
        CHECK(d.rd == 0); CHECK(d.rm == 1);
        CHECK(d.setflags);
    }
    SUBCASE("LSRS / ASRS") {
        CHECK(d16(0x0888).op == Mnemonic::LSR_imm);
        CHECK(d16(0x1088).op == Mnemonic::ASR_imm);
    }
    SUBCASE("ADDS r0, r1, r2 / SUBS r0, r1, r2") {
        auto a = d16(0x1888);
        CHECK(a.op == Mnemonic::ADD_reg);
        CHECK(a.rd == 0); CHECK(a.rn == 1); CHECK(a.rm == 2);
        auto s = d16(0x1A88);
        CHECK(s.op == Mnemonic::SUB_reg);
        CHECK(s.rd == 0); CHECK(s.rn == 1); CHECK(s.rm == 2);
    }
    SUBCASE("ADDS r1, r2, #3 (imm3)") {
        auto d = d16(0x1CD1);
        CHECK(d.op == Mnemonic::ADD_imm3);
        CHECK(d.rd == 1); CHECK(d.rn == 2); CHECK(d.imm == 3);
    }
    SUBCASE("MOVS / CMP immediate") {
        auto m = d16(0x27FF);  // MOVS r7, #255
        CHECK(m.op == Mnemonic::MOV_imm);
        CHECK(m.rd == 7); CHECK(m.imm == 255); CHECK(m.setflags);
        auto c = d16(0x280A);  // CMP r0, #10
        CHECK(c.op == Mnemonic::CMP_imm);
        CHECK(c.rn == 0); CHECK(c.imm == 10);
    }
    SUBCASE("ADDS/SUBS r, #imm8") {
        auto a = d16(0x3005);  // ADDS r0, #5
        CHECK(a.op == Mnemonic::ADD_imm8);
        CHECK(a.rd == 0); CHECK(a.rn == 0); CHECK(a.imm == 5);
        auto s = d16(0x3F01);  // SUBS r7, #1
        CHECK(s.op == Mnemonic::SUB_imm8);
        CHECK(s.rd == 7); CHECK(s.rn == 7); CHECK(s.imm == 1);
    }
}

TEST_CASE("A5.2.2 data processing (register)") {
    CHECK(d16(0x4008).op == Mnemonic::AND_reg);
    CHECK(d16(0x4048).op == Mnemonic::EOR_reg);
    CHECK(d16(0x4088).op == Mnemonic::LSL_reg);
    CHECK(d16(0x41C8).op == Mnemonic::ROR_reg);
    CHECK(d16(0x43C8).op == Mnemonic::MVN_reg);

    SUBCASE("ANDS r0, r1 -> rd=rn=0, rm=1, sets flags") {
        auto d = d16(0x4008);
        CHECK(d.rd == 0); CHECK(d.rn == 0); CHECK(d.rm == 1);
        CHECK(d.setflags);
    }
    SUBCASE("TST r0, r1 has no destination") {
        auto d = d16(0x4208);
        CHECK(d.op == Mnemonic::TST_reg);
        CHECK(d.rn == 0); CHECK(d.rm == 1);
    }
    SUBCASE("CMP r2, r3 (low registers)") {
        auto d = d16(0x429A);
        CHECK(d.op == Mnemonic::CMP_reg);
        CHECK(d.rn == 2); CHECK(d.rm == 3); CHECK(d.setflags);
    }
    SUBCASE("RSBS r0, r1, #0") {
        auto d = d16(0x4248);
        CHECK(d.op == Mnemonic::RSB_imm);
        CHECK(d.rd == 0); CHECK(d.rn == 1); CHECK(d.imm == 0);
    }
    SUBCASE("MULS r0, r1, r0") {
        auto d = d16(0x4348);
        CHECK(d.op == Mnemonic::MUL);
        CHECK(d.rd == 0); CHECK(d.rn == 1); CHECK(d.rm == 0);
        CHECK(d.setflags);
    }
}

TEST_CASE("A5.2.3 special data instructions and branch/exchange") {
    SUBCASE("ADD r8, r9 (high) - no flags") {
        auto d = d16(0x44C8);
        CHECK(d.op == Mnemonic::ADD_reg_hi);
        CHECK(d.rd == 8); CHECK(d.rn == 8); CHECK(d.rm == 9);
        CHECK_FALSE(d.setflags);
    }
    SUBCASE("CMP r10, r11 (high) - sets flags") {
        auto d = d16(0x45DA);
        CHECK(d.op == Mnemonic::CMP_reg_hi);
        CHECK(d.rn == 10); CHECK(d.rm == 11);
        CHECK(d.setflags);
    }
    SUBCASE("MOV r10, r11 (high) - no flags") {
        auto d = d16(0x46DA);
        CHECK(d.op == Mnemonic::MOV_reg_hi);
        CHECK(d.rd == 10); CHECK(d.rm == 11);
        CHECK_FALSE(d.setflags);
    }
    SUBCASE("BX lr / BLX r3") {
        auto bx = d16(0x4770);
        CHECK(bx.op == Mnemonic::BX);
        CHECK(bx.rm == 14);
        auto blx = d16(0x4798);
        CHECK(blx.op == Mnemonic::BLX_reg);
        CHECK(blx.rm == 3);
    }
}

TEST_CASE("A5.2.4 load / store single data item") {
    SUBCASE("LDR literal") {
        auto d = d16(0x4804);  // LDR r0, [pc, #16]
        CHECK(d.op == Mnemonic::LDR_lit);
        CHECK(d.rt == 0); CHECK(d.rn == 15); CHECK(d.imm == 16);
    }
    SUBCASE("register offset family") {
        CHECK(d16(0x5088).op == Mnemonic::STR_reg);
        CHECK(d16(0x5688).op == Mnemonic::LDRSB_reg);
        CHECK(d16(0x5888).op == Mnemonic::LDR_reg);
        CHECK(d16(0x5E88).op == Mnemonic::LDRSH_reg);
        auto d = d16(0x5888);  // LDR r0, [r1, r2]
        CHECK(d.rt == 0); CHECK(d.rn == 1); CHECK(d.rm == 2);
    }
    SUBCASE("word immediate offset scales by 4") {
        auto l = d16(0x6848);  // LDR r0, [r1, #4]
        CHECK(l.op == Mnemonic::LDR_imm);
        CHECK(l.rt == 0); CHECK(l.rn == 1); CHECK(l.imm == 4);
        CHECK(d16(0x6048).op == Mnemonic::STR_imm);
    }
    SUBCASE("byte immediate offset is unscaled") {
        auto l = d16(0x7848);  // LDRB r0, [r1, #1]
        CHECK(l.op == Mnemonic::LDRB_imm);
        CHECK(l.imm == 1);
        CHECK(d16(0x7048).op == Mnemonic::STRB_imm);
    }
    SUBCASE("halfword immediate offset scales by 2") {
        auto l = d16(0x8848);  // LDRH r0, [r1, #2]
        CHECK(l.op == Mnemonic::LDRH_imm);
        CHECK(l.imm == 2);
        CHECK(d16(0x8048).op == Mnemonic::STRH_imm);
    }
    SUBCASE("SP-relative scales by 4, base is r13") {
        auto l = d16(0x9804);  // LDR r0, [sp, #16]
        CHECK(l.op == Mnemonic::LDR_imm_sp);
        CHECK(l.rt == 0); CHECK(l.rn == 13); CHECK(l.imm == 16);
        CHECK(d16(0x9004).op == Mnemonic::STR_imm_sp);
    }
}

TEST_CASE("PC/SP relative address generation") {
    auto adr = d16(0xA004);  // ADR r0, #16
    CHECK(adr.op == Mnemonic::ADR);
    CHECK(adr.rd == 0); CHECK(adr.rn == 15); CHECK(adr.imm == 16);

    auto sp = d16(0xA804);  // ADD r0, sp, #16
    CHECK(sp.op == Mnemonic::ADD_SP_imm);
    CHECK(sp.rd == 0); CHECK(sp.rn == 13); CHECK(sp.imm == 16);
}

TEST_CASE("A5.2.5 miscellaneous 16-bit") {
    SUBCASE("ADD/SUB sp, #imm7 scales by 4") {
        auto a = d16(0xB004);  // ADD sp, #16
        CHECK(a.op == Mnemonic::ADD_SP_sp_imm);
        CHECK(a.rd == 13); CHECK(a.imm == 16); CHECK(a.add);
        auto s = d16(0xB084);  // SUB sp, #16
        CHECK(s.op == Mnemonic::SUB_SP_imm);
        CHECK(s.imm == 16); CHECK_FALSE(s.add);
    }
    SUBCASE("sign/zero extend") {
        CHECK(d16(0xB208).op == Mnemonic::SXTH);
        CHECK(d16(0xB248).op == Mnemonic::SXTB);
        CHECK(d16(0xB288).op == Mnemonic::UXTH);
        CHECK(d16(0xB2C8).op == Mnemonic::UXTB);
        auto d = d16(0xB248);
        CHECK(d.rd == 0); CHECK(d.rm == 1);
    }
    SUBCASE("PUSH {r0, r4, lr}") {
        auto d = d16(0xB511);
        CHECK(d.op == Mnemonic::PUSH);
        CHECK(d.register_list == 0x4011);  // r0, r4, r14(LR)
    }
    SUBCASE("POP {r0, pc}") {
        auto d = d16(0xBD01);
        CHECK(d.op == Mnemonic::POP);
        CHECK(d.register_list == 0x8001);  // r0, r15(PC)
    }
    SUBCASE("byte reversal") {
        CHECK(d16(0xBA08).op == Mnemonic::REV);
        CHECK(d16(0xBA48).op == Mnemonic::REV16);
        CHECK(d16(0xBAC8).op == Mnemonic::REVSH);
    }
    SUBCASE("CPSID / CPSIE") {
        auto off = d16(0xB672);  // CPSID i
        CHECK(off.op == Mnemonic::CPS);
        CHECK(off.imm == 1);
        auto on = d16(0xB662);   // CPSIE i
        CHECK(on.op == Mnemonic::CPS);
        CHECK(on.imm == 0);
    }
    SUBCASE("BKPT keeps its immediate") {
        auto d = d16(0xBEAB);
        CHECK(d.op == Mnemonic::BKPT);
        CHECK(d.imm == 0xAB);
    }
    SUBCASE("hints") {
        CHECK(d16(0xBF00).op == Mnemonic::NOP);
        CHECK(d16(0xBF10).op == Mnemonic::YIELD);
        CHECK(d16(0xBF20).op == Mnemonic::WFE);
        CHECK(d16(0xBF30).op == Mnemonic::WFI);
        CHECK(d16(0xBF40).op == Mnemonic::SEV);
    }
    SUBCASE("IT block is undefined on ARMv6-M") {
        CHECK(d16(0xBF08).op == Mnemonic::UNDEFINED);  // ITE EQ
        CHECK(d16(0xBFA1).op == Mnemonic::UNDEFINED);
    }
    SUBCASE("CBZ / CBNZ are undefined on ARMv6-M") {
        CHECK(d16(0xB100).op == Mnemonic::UNDEFINED);  // CBZ
        CHECK(d16(0xBB00).op == Mnemonic::UNDEFINED);  // CBNZ
    }
}

TEST_CASE("load / store multiple") {
    SUBCASE("STMIA always writes back") {
        auto d = d16(0xC006);  // STMIA r0!, {r1, r2}
        CHECK(d.op == Mnemonic::STM);
        CHECK(d.rn == 0); CHECK(d.register_list == 0x06);
        CHECK(d.wback);
    }
    SUBCASE("LDMIA writes back iff base not in list") {
        auto wb = d16(0xC806);   // LDMIA r0!, {r1, r2}
        CHECK(wb.op == Mnemonic::LDM);
        CHECK(wb.wback);
        auto nowb = d16(0xC803);  // LDMIA r0, {r0, r1}
        CHECK(nowb.op == Mnemonic::LDM);
        CHECK_FALSE(nowb.wback);
    }
}

TEST_CASE("A5.2.6 conditional branch and supervisor call") {
    SUBCASE("Bcc keeps condition and sign-extends the offset") {
        auto d = d16(0xD0FE);  // BEQ .-4  (PC+4-4-4 ... offset field only here)
        CHECK(d.op == Mnemonic::B_cond);
        CHECK(d.cond == 0x0);            // EQ
        CHECK(d.branch_offset == -4);
        auto fwd = d16(0xD17F);          // BNE #+254
        CHECK(fwd.op == Mnemonic::B_cond);
        CHECK(fwd.cond == 0x1);
        CHECK(fwd.branch_offset == 254);
    }
    SUBCASE("SVC / UDF") {
        auto svc = d16(0xDF00);
        CHECK(svc.op == Mnemonic::SVC);
        CHECK(svc.imm == 0);
        auto udf = d16(0xDE00);
        CHECK(udf.op == Mnemonic::UDF);
    }
}

TEST_CASE("unconditional branch (T2)") {
    auto back = d16(0xE7FE);  // b .   (offset -4)
    CHECK(back.op == Mnemonic::B);
    CHECK(back.branch_offset == -4);
    auto fwd = d16(0xE000);   // b #+4
    CHECK(fwd.op == Mnemonic::B);
    CHECK(fwd.branch_offset == 0);
}

TEST_CASE("32-bit: BL offset reconstruction") {
    SUBCASE("BL to self+4 (offset 0)") {
        auto d = decode_thumb32(0xF000, 0xF800);
        CHECK(d.op == Mnemonic::BL);
        CHECK(d.length == 4);
        CHECK(d.branch_offset == 0);
    }
    SUBCASE("BL backwards (offset -4)") {
        auto d = decode_thumb32(0xF7FF, 0xFFFE);
        CHECK(d.op == Mnemonic::BL);
        CHECK(d.branch_offset == -4);
    }
    SUBCASE("BL far forward") {
        // S=0, imm10=0, J1=J2=1 -> I1=I2=0, imm11=0x400 -> offset = 0x800
        auto d = decode_thumb32(0xF000, 0xFC00);
        CHECK(d.op == Mnemonic::BL);
        CHECK(d.branch_offset == 0x800);
    }
}

TEST_CASE("32-bit: system instructions") {
    SUBCASE("MRS r0, IPSR") {
        auto d = decode_thumb32(0xF3EF, 0x8005);
        CHECK(d.op == Mnemonic::MRS);
        CHECK(d.rd == 0); CHECK(d.imm == 5);
    }
    SUBCASE("MSR PRIMASK, r0") {
        auto d = decode_thumb32(0xF380, 0x8810);
        CHECK(d.op == Mnemonic::MSR);
        CHECK(d.rn == 0); CHECK(d.imm == 0x10);
    }
    SUBCASE("barriers") {
        CHECK(decode_thumb32(0xF3BF, 0x8F4F).op == Mnemonic::DSB);
        CHECK(decode_thumb32(0xF3BF, 0x8F5F).op == Mnemonic::DMB);
        CHECK(decode_thumb32(0xF3BF, 0x8F6F).op == Mnemonic::ISB);
    }
}

TEST_CASE("undefined encodings") {
    CHECK(d16(0xDE01).op == Mnemonic::UDF);          // UDF #1 (defined-as-undefined)
    CHECK(d16(0x4700).op == Mnemonic::BX);           // BX r0 (sanity)
    CHECK(d16(0x4701).op == Mnemonic::UNDEFINED);    // BX with bits[2:0]!=0
    CHECK(decode_thumb32(0xE800, 0x0000).op == Mnemonic::UNDEFINED);  // v7-M LDM.W
    CHECK(decode_thumb32(0xE800, 0x0000).length == 4);
}
