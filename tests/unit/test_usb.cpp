// Unit tests for the USB device controller functional model (datasheet 4.1).
#include "doctest.h"

#include <array>
#include <cstdint>
#include <vector>

#include "peripherals/usb.h"
#include "simulator.h"

using namespace rp2040;

namespace {
struct Fix {
    Simulator sim;
    UsbCtrl& usb = sim.usb();
    static constexpr std::uint32_t D = UsbCtrl::kBase;                       // DPRAM
    static constexpr std::uint32_t R = UsbCtrl::kBase + UsbCtrl::kRegBase;   // REGS

    std::uint32_t rd(std::uint32_t a) { return sim.memory().read_word(a).value; }
    void wr(std::uint32_t a, std::uint32_t v) {
        REQUIRE(sim.memory().write_word(a, v) == BusStatus::Ok);
    }
    Fix() {
        wr(R + 0x40, 1u);            // MAIN_CTRL.CONTROLLER_EN
        wr(R + 0x4C, 1u << 16);      // SIE_CTRL.PULLUP_EN
        wr(R + 0x90, (1u << 16) | (1u << 12) | (1u << 4));  // INTE: SETUP_REQ|BUS_RESET|BUFF_STATUS
        sim.cpu().set_irq_enabled(5, true);
        sim.cpu().set_exception_priority(UsbCtrl::kIrq, 0);
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fix, "controller enable / pullup / address round-trip") {
    CHECK(usb.controller_enabled());
    CHECK(usb.pullup_enabled());

    wr(R + 0x00, 5u);               // ADDR_ENDP
    CHECK(usb.device_address() == 5u);
    CHECK((rd(R + 0x00) & 0x7Fu) == 5u);

    wr(R + 0x44, 0x123u);           // SOF_WR
    CHECK(usb.frame_number() == 0x123u);
    CHECK((rd(R + 0x48) & 0x7FFu) == 0x123u);
}

TEST_CASE_FIXTURE(Fix, "a bus reset raises USBCTRL_IRQ and is write-1-clear") {
    usb.host_reset();
    CHECK((rd(R + 0x50) & (1u << 19)) != 0);          // SIE_STATUS.BUS_RESET
    CHECK((rd(R + 0x50) & (1u << 16)) != 0);          // SIE_STATUS.CONNECTED
    CHECK(sim.cpu().is_pending(UsbCtrl::kIrq));

    wr(R + 0x50, 1u << 19);                           // clear BUS_RESET
    CHECK((rd(R + 0x50) & (1u << 19)) == 0);
    CHECK((rd(R + 0x50) & (1u << 16)) != 0);          // CONNECTED is not w1c
    CHECK_FALSE(sim.cpu().is_pending(UsbCtrl::kIrq));
}

TEST_CASE_FIXTURE(Fix, "a control IN transfer: SETUP packet then EP0 IN data") {
    // Host: GET_DESCRIPTOR(DEVICE), wLength 18.
    const std::array<std::uint8_t, 8> setup{0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00};
    usb.host_setup(setup);

    CHECK((rd(R + 0x50) & (1u << 17)) != 0);          // SIE_STATUS.SETUP_REC
    CHECK(sim.cpu().is_pending(UsbCtrl::kIrq));
    CHECK(rd(D + 0x00) == 0x01000680u);               // LE of 80 06 00 01
    CHECK(rd(D + 0x04) == 0x00120000u);               // 00 00 12 00
    wr(R + 0x50, 1u << 17);                           // ack SETUP_REC

    // Device: write an 18-byte descriptor into the EP0 IN buffer (DPRAM 0x100).
    const std::vector<std::uint8_t> desc{
        18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x83, 0x04, 0x40, 0x00, 1, 0, 1, 2, 3, 1};
    for (std::uint32_t i = 0; i < desc.size(); ++i)
        REQUIRE(sim.memory().write_byte(D + 0x100u + i, desc[i]) == BusStatus::Ok);
    // EP0_IN_BUFFER_CONTROL: LENGTH=18, PID=DATA1, FULL, AVAILABLE.
    wr(D + 0x80, 18u | (1u << 13) | (1u << 15) | (1u << 10));

    const std::vector<std::uint8_t> got = usb.host_in_ep0();
    REQUIRE(got.size() == 18u);
    CHECK(got == desc);
    CHECK((rd(R + 0x58) & 1u) != 0);                  // BUFF_STATUS: EP0 IN done
    CHECK((rd(R + 0x50) & (1u << 18)) != 0);          // SIE_STATUS.TRANS_COMPLETE
    CHECK((rd(D + 0x80) & (1u << 10)) == 0);          // AVAILABLE consumed

    wr(R + 0x58, 1u);                                 // clear BUFF_STATUS
    wr(R + 0x50, 1u << 18);
    CHECK_FALSE(sim.cpu().is_pending(UsbCtrl::kIrq));
}

TEST_CASE_FIXTURE(Fix, "a control OUT data stage lands in the EP0 OUT buffer") {
    const std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF, 0x55};
    usb.host_out_ep0(payload);

    for (std::uint32_t i = 0; i < payload.size(); ++i)
        CHECK(sim.memory().read_byte(D + 0x100u + i).value == payload[i]);
    CHECK((rd(D + 0x84) & 0x3FFu) == payload.size());  // EP0_OUT_BC length
    CHECK((rd(D + 0x84) & (1u << 15)) != 0);          // FULL
    CHECK((rd(R + 0x58) & (1u << 1)) != 0);           // BUFF_STATUS: EP0 OUT
}

TEST_CASE_FIXTURE(Fix, "unmapped USB registers read as zero, DPRAM is 4 KB of RAM") {
    CHECK(rd(R + 0x200) == 0u);
    wr(D + 0xFFC, 0xABCD1234u);
    CHECK(rd(D + 0xFFC) == 0xABCD1234u);
}
