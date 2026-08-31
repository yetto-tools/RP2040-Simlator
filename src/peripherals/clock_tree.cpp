#include "peripherals/clock_tree.h"

namespace rp2040 {

namespace {

// pico-sdk steady-state defaults (used before clocks_init writes a generator).
constexpr std::uint64_t kDefaultRefHz  = 12'000'000u;
constexpr std::uint64_t kDefaultSysHz  = 125'000'000u;
constexpr std::uint64_t kDefaultAdcHz  = 48'000'000u;
constexpr std::uint64_t kDefaultRtcHz  = 46'875u;

// Apply a CLK_*_DIV register: int part [31:8], frac part [7:0] (256ths).
std::uint64_t apply_div(std::uint64_t in_hz, std::uint32_t div_reg, bool has_frac) {
    std::uint64_t num = (static_cast<std::uint64_t>(div_reg) >> 8);
    std::uint64_t frac = has_frac ? (div_reg & 0xFFu) : 0u;
    std::uint64_t denom = num * 256u + frac;
    if (denom == 0) return in_hz;                 // DIV of 0 -> treat as /1
    return in_hz * 256u / denom;
}

}  // namespace

std::uint64_t ClockTree::xosc_hz() const {
    return xosc_.stable() ? Xosc::kCrystalHz : 0u;
}

std::uint64_t ClockTree::rosc_hz() const {
    return rosc_.enabled() ? Rosc::kNominalHz : 0u;
}

std::uint64_t ClockTree::pll_sys_hz() const { return pll_sys_.output_hz(Xosc::kCrystalHz); }
std::uint64_t ClockTree::pll_usb_hz() const { return pll_usb_.output_hz(Xosc::kCrystalHz); }

std::uint64_t ClockTree::clk_ref_hz() const {
    if (!clocks_.gen_configured(Clocks::kRef)) return kDefaultRefHz;
    const std::uint32_t ctrl = clocks_.gen_ctrl(Clocks::kRef);
    std::uint64_t src = 0;
    switch (ctrl & 0x3u) {                        // CLK_REF_CTRL.SRC
        case 0: src = rosc_hz(); break;           // ROSC_CLKSRC_PH
        case 2: src = xosc_hz(); break;           // XOSC_CLKSRC
        default: {                                // CLKSRC_CLK_REF_AUX
            const unsigned aux = (ctrl >> 5) & 0x3u;
            src = (aux == 0) ? pll_usb_hz() : 0u; // PLL_USB / GPIN0 / GPIN1
            break;
        }
    }
    const std::uint64_t out = apply_div(src, clocks_.gen_div(Clocks::kRef), /*frac=*/false);
    return out != 0 ? out : kDefaultRefHz;
}

std::uint64_t ClockTree::aux_source_hz(unsigned auxsrc) const {
    switch (auxsrc) {
        case 0: return pll_sys_hz();   // for clk_sys; for adc/rtc/peri 0 == PLL_USB (see callers)
        case 1: return pll_usb_hz();
        case 2: return rosc_hz();
        case 3: return xosc_hz();
        default: return 0u;            // GPINx - not modelled
    }
}

std::uint64_t ClockTree::clk_sys_hz() const {
    if (!clocks_.gen_configured(Clocks::kSys)) return kDefaultSysHz;
    const std::uint32_t ctrl = clocks_.gen_ctrl(Clocks::kSys);
    std::uint64_t src = 0;
    if ((ctrl & 0x1u) == 0) {                     // SRC == CLK_REF
        src = clk_ref_hz();
    } else {                                      // SRC == CLKSRC_CLK_SYS_AUX
        src = aux_source_hz((ctrl >> 5) & 0x7u);
    }
    const std::uint64_t out = apply_div(src, clocks_.gen_div(Clocks::kSys), /*frac=*/true);
    return out != 0 ? out : kDefaultSysHz;
}

std::uint64_t ClockTree::clk_peri_hz() const {
    if (!clocks_.gen_configured(Clocks::kPeri)) return clk_sys_hz();
    const unsigned aux = (clocks_.gen_ctrl(Clocks::kPeri) >> 5) & 0x7u;
    const std::uint64_t src = (aux == 0) ? clk_sys_hz() : aux_source_hz(aux - 1u);
    return src != 0 ? src : clk_sys_hz();         // clk_peri has no divider on RP2040
}

std::uint64_t ClockTree::clk_adc_hz() const {
    if (!clocks_.gen_configured(Clocks::kAdc)) return kDefaultAdcHz;
    const unsigned aux = (clocks_.gen_ctrl(Clocks::kAdc) >> 5) & 0x7u;
    // AUXSRC 0 == PLL_USB, 1 == PLL_SYS, 2 == ROSC, 3 == XOSC.
    std::uint64_t src = 0;
    switch (aux) {
        case 0: src = pll_usb_hz(); break;
        case 1: src = pll_sys_hz(); break;
        case 2: src = rosc_hz(); break;
        case 3: src = xosc_hz(); break;
        default: src = 0u; break;
    }
    const std::uint64_t out = apply_div(src, clocks_.gen_div(Clocks::kAdc), /*frac=*/false);
    return out != 0 ? out : kDefaultAdcHz;
}

std::uint64_t ClockTree::clk_rtc_hz() const {
    if (!clocks_.gen_configured(Clocks::kRtc)) return kDefaultRtcHz;
    const unsigned aux = (clocks_.gen_ctrl(Clocks::kRtc) >> 5) & 0x7u;
    std::uint64_t src = 0;
    switch (aux) {
        case 0: src = pll_usb_hz(); break;
        case 1: src = pll_sys_hz(); break;
        case 2: src = rosc_hz(); break;
        case 3: src = xosc_hz(); break;
        default: src = 0u; break;
    }
    const std::uint64_t out = apply_div(src, clocks_.gen_div(Clocks::kRtc), /*frac=*/true);
    return out != 0 ? out : kDefaultRtcHz;
}

std::uint32_t ClockTree::signature() const {
    return clocks_.signature()
         ^ (pll_sys_.signature() * 2654435761u)
         ^ (pll_usb_.signature() * 40503u)
         ^ (xosc_.signature() * 7u)
         ^ (rosc_.signature() * 131u)
         ^ (wd_.tick_cycles() << 20);
}

std::uint32_t ClockTree::timer_us_cycles() const {
    const std::uint64_t sys = clk_sys_hz();
    const std::uint64_t ref = clk_ref_hz();
    std::uint64_t tick = wd_.tick_cycles();
    if (tick == 0) tick = ref / 1'000'000u;       // pico-sdk sets clk_ref/MHz
    if (tick == 0 || ref == 0) return static_cast<std::uint32_t>(sys / 1'000'000u);
    const std::uint64_t cyc = tick * sys / ref;   // clk_sys cycles per us tick
    return cyc != 0 ? static_cast<std::uint32_t>(cyc) : 1u;
}

}  // namespace rp2040
