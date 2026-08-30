// Unit tests for the GDB Remote Serial Protocol handler (transport-free).
#include "doctest.h"

#include <array>
#include <cstdint>
#include <string>

#include "debuggers/gdb_stub.h"
#include "simulator.h"

using namespace rp2040;

namespace {

struct GdbFix {
    Simulator sim;
    GdbStub stub{sim};
    static constexpr std::uint32_t kBase = 0x20000000u;

    GdbFix() {
        // movs r0,#7 ; movs r1,#0x11 ; b .
        const std::array<std::uint16_t, 3> prog{0x2007, 0x2111, 0xE7FE};
        REQUIRE(sim.memory().load(kBase, prog.data(), prog.size() * 2));
        sim.regs(0).set_pc(kBase);
    }
};

std::uint32_t le_hex_to_u32(const std::string& s, std::size_t pos) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const std::string byte = s.substr(pos + static_cast<std::size_t>(i) * 2, 2);
        v |= static_cast<std::uint32_t>(std::stoul(byte, nullptr, 16)) << (8 * i);
    }
    return v;
}

}  // namespace

TEST_CASE("frame / unframe / checksum") {
    CHECK(GdbStub::checksum("OK") == static_cast<std::uint8_t>('O' + 'K'));
    CHECK(GdbStub::frame("OK") == "$OK#9a");
    CHECK(GdbStub::unframe("+$g#67") == "g");
    CHECK(GdbStub::unframe("garbage").empty());
}

TEST_CASE_FIXTURE(GdbFix, "halt reason and register read") {
    CHECK(stub.handle_packet("?") == "S05");

    const std::string g = stub.handle_packet("g");
    REQUIRE(g.size() == 17 * 8);              // r0-r12, sp, lr, pc, xpsr
    CHECK(le_hex_to_u32(g, 15 * 8) == kBase); // PC
}

TEST_CASE_FIXTURE(GdbFix, "single register read / write via p and P") {
    CHECK(stub.handle_packet("P0=39300000") == "OK");     // r0 = 0x3039 (LE)
    CHECK(sim.regs(0).get(0) == 0x3039u);
    CHECK(stub.handle_packet("p0") == "39300000");
    CHECK(stub.handle_packet("pa") == "00000000");    // r10 (reg 0xa)
    CHECK(stub.handle_packet("p10") == "00000001");   // xpsr (reg 0x10): EPSR.T set
}

TEST_CASE_FIXTURE(GdbFix, "memory read and write") {
    CHECK(stub.handle_packet("m20000000,4") == "07201121");   // the first two opcodes
    CHECK(stub.handle_packet("M20002000,4:deadbeef") == "OK");
    CHECK(sim.memory().read_word(0x20002000u).value == 0xEFBEADDEu);
    CHECK(stub.handle_packet("m30000000,4") == "E01");        // unbacked
}

TEST_CASE_FIXTURE(GdbFix, "single-step advances one instruction") {
    CHECK(stub.handle_packet("s") == "S05");
    CHECK(sim.regs(0).get(0) == 7u);
    CHECK(sim.regs(0).pc() == kBase + 2);
    stub.handle_packet("s");
    CHECK(sim.regs(0).get(1) == 0x11u);
}

TEST_CASE_FIXTURE(GdbFix, "continue stops at a software breakpoint") {
    CHECK(stub.handle_packet("Z0,20000004,2") == "OK");   // bp on 'b .'
    CHECK(stub.handle_packet("c") == "S05");
    CHECK(sim.regs(0).pc() == kBase + 4);
    CHECK(sim.regs(0).get(0) == 7u);
    CHECK(sim.regs(0).get(1) == 0x11u);
    CHECK(stub.handle_packet("z0,20000004,2") == "OK");
}

TEST_CASE_FIXTURE(GdbFix, "qSupported advertises the packet size and no-ack mode") {
    const std::string s = stub.handle_packet("qSupported:multiprocess+;xmlRegisters=arm");
    CHECK(s.find("PacketSize=") != std::string::npos);
    CHECK(s.find("QStartNoAckMode+") != std::string::npos);
    CHECK(stub.handle_packet("QStartNoAckMode") == "OK");
    CHECK(stub.handle_packet("vCont?") == "vCont;c;s");
}
