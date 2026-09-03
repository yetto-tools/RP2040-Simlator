#include "peripherals/ssd1306.h"

#include <algorithm>

namespace rp2040 {

namespace {
constexpr std::uint8_t kSetContrast = 0x81;
constexpr std::uint8_t kSetEntireOnLo = 0xA4, kSetEntireOnHi = 0xA5;
constexpr std::uint8_t kSetNormInvOff = 0xA6, kSetNormInvOn = 0xA7;
constexpr std::uint8_t kSetDispOff = 0xAE, kSetDispOn = 0xAF;
constexpr std::uint8_t kSetMemAddr = 0x20;
constexpr std::uint8_t kSetColAddr = 0x21;
constexpr std::uint8_t kSetPageAddr = 0x22;
constexpr std::uint8_t kSetSegRemapLo = 0xA0, kSetSegRemapHi = 0xA1;
constexpr std::uint8_t kSetMuxRatio = 0xA8;
constexpr std::uint8_t kSetComOutDirLo = 0xC0, kSetComOutDirHi = 0xC8;
constexpr std::uint8_t kSetDispOffset = 0xD3;
constexpr std::uint8_t kSetComPinCfg = 0xDA;
constexpr std::uint8_t kSetDispClkDiv = 0xD5;
constexpr std::uint8_t kSetPrecharge = 0xD9;
constexpr std::uint8_t kSetVcomDesel = 0xDB;
constexpr std::uint8_t kSetChargePump = 0x8D;
constexpr std::uint8_t kSetStartLineLo = 0x40, kSetStartLineHi = 0x7F;
}  // namespace

bool Ssd1306::on_transfer(bool is_read, std::uint8_t& byte) {
    if (is_read) return false;  // no driver in scope reads back from this device

    if (expect_control_) {
        co_ = (byte & 0x80) != 0;
        dc_ = (byte & 0x40) != 0;
        expect_control_ = false;
        return true;
    }

    if (dc_) handle_data(byte);
    else handle_command(byte);
    if (co_) expect_control_ = true;
    return true;
}

void Ssd1306::on_stop() {
    expect_control_ = true;
}

void Ssd1306::handle_command(std::uint8_t byte) {
    if (awaiting_ != Awaiting::None) {
        const Awaiting a = awaiting_;
        awaiting_ = Awaiting::None;
        switch (a) {
            case Awaiting::ColStart:
                col_start_ = std::min<std::uint8_t>(byte, kWidth - 1);
                awaiting_ = Awaiting::ColEnd;
                return;
            case Awaiting::ColEnd:
                col_end_ = std::min<std::uint8_t>(byte, kWidth - 1);
                cur_col_ = col_start_;
                cur_page_ = page_start_;
                return;
            case Awaiting::PageStart:
                page_start_ = std::min<std::uint8_t>(byte, kHeight / 8 - 1);
                awaiting_ = Awaiting::PageEnd;
                return;
            case Awaiting::PageEnd:
                page_end_ = std::min<std::uint8_t>(byte, kHeight / 8 - 1);
                cur_col_ = col_start_;
                cur_page_ = page_start_;
                return;
            // MemAddrMode/MuxRatio/DispOffset/ComPinCfg/ClkDiv/Precharge/
            // VcomDesel/Contrast/ChargePump: single param byte, consumed
            // with no framebuffer effect (see file comment).
            default:
                return;
        }
    }

    switch (byte) {
        case kSetDispOff: display_on_ = false; return;
        case kSetDispOn: display_on_ = true; return;
        case kSetNormInvOff: inverted_ = false; return;
        case kSetNormInvOn: inverted_ = true; return;
        case kSetMemAddr: awaiting_ = Awaiting::MemAddrMode; return;
        case kSetColAddr: awaiting_ = Awaiting::ColStart; return;
        case kSetPageAddr: awaiting_ = Awaiting::PageStart; return;
        case kSetMuxRatio: awaiting_ = Awaiting::MuxRatio; return;
        case kSetDispOffset: awaiting_ = Awaiting::DispOffset; return;
        case kSetComPinCfg: awaiting_ = Awaiting::ComPinCfg; return;
        case kSetDispClkDiv: awaiting_ = Awaiting::ClkDiv; return;
        case kSetPrecharge: awaiting_ = Awaiting::Precharge; return;
        case kSetVcomDesel: awaiting_ = Awaiting::VcomDesel; return;
        case kSetContrast: awaiting_ = Awaiting::Contrast; return;
        case kSetChargePump: awaiting_ = Awaiting::ChargePump; return;
        case kSetEntireOnLo: case kSetEntireOnHi: return;
        case kSetSegRemapLo: case kSetSegRemapHi: return;
        default:
            if (byte >= kSetStartLineLo && byte <= kSetStartLineHi) return;
            if (byte == kSetComOutDirLo || byte == kSetComOutDirHi) return;
            return;  // unknown/unsupported command: ignored
    }
}

void Ssd1306::handle_data(std::uint8_t byte) {
    if (col_end_ < col_start_ || page_end_ < page_start_) return;
    gddram_[static_cast<std::size_t>(cur_col_) + static_cast<std::size_t>(cur_page_) * kWidth] = byte;
    if (++cur_col_ > col_end_) {
        cur_col_ = col_start_;
        if (++cur_page_ > page_end_) cur_page_ = page_start_;
    }
}

}  // namespace rp2040
