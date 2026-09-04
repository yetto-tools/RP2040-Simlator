// usb.h - RP2040 USB device controller (datasheet 4.1) - functional model.
//
// Covers USBCTRL_DPRAM (0x50100000, 4 KB of endpoint buffers + buffer-control
// registers) and USBCTRL_REGS (0x50110000): MAIN_CTRL, SIE_CTRL, SIE_STATUS,
// ADDR_ENDP, BUFF_STATUS, EP_STALL_ARM / EP_STATUS_STALL_NAK, USB_PWR, the
// frame counter, and the INTR/INTE/INTF/INTS -> USBCTRL_IRQ (IRQ 5) path.
//
// There is no wire-level SIE; a "virtual host" API drives enumeration:
// host_reset(), host_setup(), host_in_ep0(), host_out_ep0().
#ifndef RP2040_PERIPHERALS_USB_H
#define RP2040_PERIPHERALS_USB_H

#include <array>
#include <cstdint>
#include <vector>

#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
#include "core/memory.h"
#include "exceptions.h"

namespace rp2040 {

class UsbCtrl : public BusPeripheral {
public:
    // One decode window covering the DPRAM and the register block.
    static constexpr std::uint32_t kBase = 0x50100000u;
    static constexpr std::uint32_t kSize = 0x00020000u;   // DPRAM + gap + REGS
    static constexpr std::uint32_t kRegBase = 0x00010000u;  // REGS offset in the window
    static constexpr std::uint32_t kDpramSize = 0x1000u;
    static constexpr unsigned kIrq = kExcExternal0 + 5;   // USBCTRL_IRQ

    explicit UsbCtrl(Cpu& cpu) : nvic_(cpu) {}
    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }
    void connect_core1(Cpu* c) { nvic_.connect(c); }

    BusResult<std::uint32_t> bus_read(std::uint32_t offset, BusWidth w) override;
    BusStatus bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) override;
    void reset() override;

    // --- virtual host (test bench) ------------------------------------
    void host_reset();
    void host_setup(const std::array<std::uint8_t, 8>& setup);
    // Read whatever the device has queued in the EP0 IN buffer (device->host).
    std::vector<std::uint8_t> host_in_ep0();
    // Deliver an OUT data stage to the device (host->device).
    void host_out_ep0(const std::vector<std::uint8_t>& data);

    // --- inspection --------------------------------------------------
    bool pullup_enabled() const { return (sie_ctrl_ & (1u << 16)) != 0; }
    bool controller_enabled() const { return (main_ctrl_ & 1u) != 0; }
    std::uint8_t device_address() const { return static_cast<std::uint8_t>(addr_endp_ & 0x7Fu); }
    std::uint16_t frame_number() const { return static_cast<std::uint16_t>(sof_ & 0x7FFu); }

private:
    static constexpr std::uint32_t kEP0_BUF = 0x100u;          // DPRAM offset of the EP0 buffer
    static constexpr std::uint32_t kEP0_IN_BC = 0x80u;         // EP0_IN_BUFFER_CONTROL
    static constexpr std::uint32_t kEP0_OUT_BC = 0x84u;        // EP0_OUT_BUFFER_CONTROL

    // SIE_STATUS event bits (write-1-to-clear).
    static constexpr std::uint32_t kSS_SETUP_REC = 1u << 17;
    static constexpr std::uint32_t kSS_TRANS_COMPLETE = 1u << 18;
    static constexpr std::uint32_t kSS_BUS_RESET = 1u << 19;
    static constexpr std::uint32_t kSS_CONNECTED = 1u << 16;   // RO status

    std::uint32_t dpram_rd(std::uint32_t off) const;
    void dpram_wr(std::uint32_t off, std::uint32_t value, unsigned nbytes = 4);
    std::uint32_t compute_intr() const;
    void refresh_irq();

    InterruptController nvic_;
    std::array<std::uint8_t, kDpramSize> dpram_{};

    std::uint32_t addr_endp_ = 0;
    std::uint32_t main_ctrl_ = 0;
    std::uint32_t sie_ctrl_ = 0;
    std::uint32_t sie_status_ = 0;
    std::uint32_t buff_status_ = 0;
    std::uint32_t ep_stall_arm_ = 0;
    std::uint32_t ep_status_stall_nak_ = 0;
    std::uint32_t usb_pwr_ = 0;
    std::uint32_t muxing_ = 0;
    std::uint32_t inte_ = 0;
    std::uint32_t intf_ = 0;
    std::uint32_t sof_ = 0;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_USB_H
