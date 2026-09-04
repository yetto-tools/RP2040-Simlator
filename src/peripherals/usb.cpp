#include "peripherals/usb.h"

#include <algorithm>

namespace rp2040 {

namespace {
// USBCTRL_REGS offsets (from kRegBase).
enum : std::uint32_t {
    kADDR_ENDP = 0x00,
    kMAIN_CTRL = 0x40,
    kSOF_WR = 0x44, kSOF_RD = 0x48,
    kSIE_CTRL = 0x4C, kSIE_STATUS = 0x50,
    kINT_EP_CTRL = 0x54,
    kBUFF_STATUS = 0x58, kBUFF_CPU_SHOULD_HANDLE = 0x5C,
    kEP_ABORT = 0x60, kEP_ABORT_DONE = 0x64,
    kEP_STALL_ARM = 0x68, kNAK_POLL = 0x6C,
    kEP_STATUS_STALL_NAK = 0x70,
    kUSB_MUXING = 0x74, kUSB_PWR = 0x78,
    kUSBPHY_DIRECT = 0x7C, kUSBPHY_DIRECT_OVERRIDE = 0x80, kUSBPHY_TRIM = 0x84,
    kINTR = 0x8C, kINTE = 0x90, kINTF = 0x94, kINTS = 0x98,
};

// Buffer-control fields (buffer 0, bits [15:0]).
constexpr std::uint32_t kBC_FULL = 1u << 15;
constexpr std::uint32_t kBC_AVAILABLE = 1u << 10;
constexpr std::uint32_t kBC_LEN_MASK = 0x3FFu;

// INTS/INTR bits used by a device.
constexpr std::uint32_t kINT_TRANS_COMPLETE = 1u << 3;
constexpr std::uint32_t kINT_BUFF_STATUS = 1u << 4;
constexpr std::uint32_t kINT_BUS_RESET = 1u << 12;
constexpr std::uint32_t kINT_DEV_SUSPEND = 1u << 14;
constexpr std::uint32_t kINT_SETUP_REQ = 1u << 16;
constexpr std::uint32_t kINT_EP_STALL_NAK = 1u << 19;
}  // namespace

std::uint32_t UsbCtrl::dpram_rd(std::uint32_t off) const {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < 4 && off + i < kDpramSize; ++i)
        v |= static_cast<std::uint32_t>(dpram_[off + i]) << (8u * i);
    return v;
}
void UsbCtrl::dpram_wr(std::uint32_t off, std::uint32_t value, unsigned nbytes) {
    for (unsigned i = 0; i < nbytes && off + i < kDpramSize; ++i)
        dpram_[off + i] = static_cast<std::uint8_t>(value >> (8u * i));
}

std::uint32_t UsbCtrl::compute_intr() const {
    std::uint32_t v = 0;
    if (sie_status_ & kSS_TRANS_COMPLETE) v |= kINT_TRANS_COMPLETE;
    if (buff_status_ != 0)                v |= kINT_BUFF_STATUS;
    if (sie_status_ & kSS_BUS_RESET)      v |= kINT_BUS_RESET;
    if (sie_status_ & (1u << 4))          v |= kINT_DEV_SUSPEND;   // SUSPENDED
    if (sie_status_ & kSS_SETUP_REC)      v |= kINT_SETUP_REQ;
    if (ep_status_stall_nak_ != 0)        v |= kINT_EP_STALL_NAK;
    return v;
}

void UsbCtrl::refresh_irq() {
    const std::uint32_t ints = (compute_intr() | intf_) & inte_;
    if (ints != 0) nvic_.pend_exception(kIrq);
    else           nvic_.clear_pending(kIrq);
}

// --- virtual host ------------------------------------------------------

void UsbCtrl::host_reset() {
    addr_endp_ = 0;
    sie_status_ |= kSS_BUS_RESET | kSS_CONNECTED;
    refresh_irq();
}

void UsbCtrl::host_setup(const std::array<std::uint8_t, 8>& setup) {
    std::copy(setup.begin(), setup.end(), dpram_.begin());
    sie_status_ |= kSS_SETUP_REC;
    refresh_irq();
}

std::vector<std::uint8_t> UsbCtrl::host_in_ep0() {
    const std::uint32_t bc = dpram_rd(kEP0_IN_BC);
    std::vector<std::uint8_t> out;
    if ((bc & kBC_AVAILABLE) && (bc & kBC_FULL)) {
        const std::uint32_t len = bc & kBC_LEN_MASK;
        for (std::uint32_t i = 0; i < len && kEP0_BUF + i < kDpramSize; ++i)
            out.push_back(dpram_[kEP0_BUF + i]);
        dpram_wr(kEP0_IN_BC, bc & ~kBC_AVAILABLE);   // hardware consumes the buffer
        buff_status_ |= 1u << 0;                     // EP0 IN
        sie_status_ |= kSS_TRANS_COMPLETE;
        refresh_irq();
    }
    return out;
}

void UsbCtrl::host_out_ep0(const std::vector<std::uint8_t>& data) {
    const std::uint32_t n = static_cast<std::uint32_t>(
        std::min<std::size_t>(data.size(), 64));
    for (std::uint32_t i = 0; i < n; ++i) dpram_[kEP0_BUF + i] = data[i];
    std::uint32_t bc = dpram_rd(kEP0_OUT_BC);
    bc = (bc & ~(kBC_LEN_MASK | kBC_AVAILABLE)) | kBC_FULL | n;
    dpram_wr(kEP0_OUT_BC, bc);
    buff_status_ |= 1u << 1;                         // EP0 OUT
    sie_status_ |= kSS_TRANS_COMPLETE;
    refresh_irq();
}

// --- MMIO ------------------------------------------------------------

void UsbCtrl::reset() {
    dpram_.fill(0);
    addr_endp_ = 0;
    main_ctrl_ = 0;
    sie_ctrl_ = 0;
    sie_status_ = 0;
    buff_status_ = 0;
    ep_stall_arm_ = 0;
    ep_status_stall_nak_ = 0;
    usb_pwr_ = 0;
    muxing_ = 0;
    inte_ = 0;
    intf_ = 0;
    sof_ = 0;
    refresh_irq();
}

BusResult<std::uint32_t> UsbCtrl::bus_read(std::uint32_t offset, BusWidth) {
    if (offset < kDpramSize) return {dpram_rd(offset), BusStatus::Ok};
    if (offset < kRegBase)   return {0u, BusStatus::Ok};   // gap

    switch (offset - kRegBase) {
        case kADDR_ENDP:   return {addr_endp_, BusStatus::Ok};
        case kMAIN_CTRL:   return {main_ctrl_, BusStatus::Ok};
        case kSOF_RD:      return {sof_ & 0x7FFu, BusStatus::Ok};
        case kSIE_CTRL:    return {sie_ctrl_, BusStatus::Ok};
        case kSIE_STATUS:  return {sie_status_, BusStatus::Ok};
        case kBUFF_STATUS: return {buff_status_, BusStatus::Ok};
        case kBUFF_CPU_SHOULD_HANDLE: return {0u, BusStatus::Ok};
        case kEP_ABORT_DONE: return {0u, BusStatus::Ok};
        case kEP_STALL_ARM: return {ep_stall_arm_, BusStatus::Ok};
        case kEP_STATUS_STALL_NAK: return {ep_status_stall_nak_, BusStatus::Ok};
        case kUSB_MUXING:  return {muxing_, BusStatus::Ok};
        case kUSB_PWR:     return {usb_pwr_, BusStatus::Ok};
        case kINTR:        return {compute_intr(), BusStatus::Ok};
        case kINTE:        return {inte_, BusStatus::Ok};
        case kINTF:        return {intf_, BusStatus::Ok};
        case kINTS:        return {(compute_intr() | intf_) & inte_, BusStatus::Ok};
        default:           return {0u, BusStatus::Ok};
    }
}

BusStatus UsbCtrl::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth w) {
    if (offset < kDpramSize) {
        dpram_wr(offset, value, static_cast<unsigned>(w));
        return BusStatus::Ok;
    }
    if (offset < kRegBase)   return BusStatus::Ok;

    switch (offset - kRegBase) {
        case kADDR_ENDP:   addr_endp_ = value & 0x000F007Fu; break;
        case kMAIN_CTRL:   main_ctrl_ = value & 0x7u; break;
        case kSOF_WR:      sof_ = value & 0x7FFu; break;
        case kSIE_CTRL:    sie_ctrl_ = value; break;
        case kSIE_STATUS:
            // Write-1-to-clear: errors [31:24], BUS_RESET/TRANS_COMPLETE/
            // SETUP_REC [19:17], RESUME [11], VBUS_OVER_CURR [10]. CONNECTED,
            // SUSPENDED, LINE_STATE, SPEED, VBUS_DETECTED are read-only status.
            sie_status_ &= ~(value & 0xFF0E0C00u);
            refresh_irq();
            break;
        case kBUFF_STATUS:
            buff_status_ &= ~value;   // write-1-clear
            refresh_irq();
            break;
        case kEP_STALL_ARM: ep_stall_arm_ = value & 0x3u; break;
        case kEP_ABORT:     break;
        case kNAK_POLL:     break;
        case kEP_STATUS_STALL_NAK:
            ep_status_stall_nak_ &= ~value;   // write-1-clear
            refresh_irq();
            break;
        case kUSB_MUXING:  muxing_ = value; break;
        case kUSB_PWR:     usb_pwr_ = value; break;
        case kUSBPHY_DIRECT: case kUSBPHY_DIRECT_OVERRIDE: case kUSBPHY_TRIM: break;
        case kINTE:  inte_ = value; refresh_irq(); break;
        case kINTF:  intf_ = value; refresh_irq(); break;
        default:     break;
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
