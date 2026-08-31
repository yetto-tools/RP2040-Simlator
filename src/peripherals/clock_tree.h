// clock_tree.h - resolves the RP2040 clock generators (datasheet 2.15.3) into
// concrete frequencies, so the timed peripherals can be paced from the clock
// firmware actually configured instead of a fixed 125 MHz.
//
// Until firmware has written a generator's CTRL (i.e. clocks_init has run) the
// tree returns the pico-sdk steady-state default for that leg, so bare-metal
// images that never touch CLOCKS keep the old behaviour.
#ifndef RP2040_PERIPHERALS_CLOCK_TREE_H
#define RP2040_PERIPHERALS_CLOCK_TREE_H

#include <cstdint>

#include "peripherals/clocks.h"
#include "peripherals/watchdog.h"

namespace rp2040 {

class ClockTree {
public:
    ClockTree(const Xosc& xosc, const Rosc& rosc, const Pll& pll_sys,
              const Pll& pll_usb, const Clocks& clocks, const Watchdog& wd)
        : xosc_(xosc), rosc_(rosc), pll_sys_(pll_sys), pll_usb_(pll_usb),
          clocks_(clocks), wd_(wd) {}

    std::uint64_t clk_ref_hz() const;
    std::uint64_t clk_sys_hz() const;
    std::uint64_t clk_peri_hz() const;
    std::uint64_t clk_adc_hz() const;
    std::uint64_t clk_rtc_hz() const;

    // clk_sys cycles per 1 us TIMER tick, from WATCHDOG_TICK.CYCLES scaled by
    // clk_sys / clk_ref. Falls back to clk_sys/1e6 when TICK is unprogrammed.
    std::uint32_t timer_us_cycles() const;

    // Cheap change detector: the caller only needs to re-push the derived
    // frequencies when this value changes.
    std::uint32_t signature() const;

private:
    std::uint64_t xosc_hz() const;
    std::uint64_t rosc_hz() const;
    std::uint64_t pll_sys_hz() const;
    std::uint64_t pll_usb_hz() const;
    std::uint64_t aux_source_hz(unsigned auxsrc) const;   // clk_sys/peri/adc/rtc AUXSRC table

    const Xosc& xosc_;
    const Rosc& rosc_;
    const Pll& pll_sys_;
    const Pll& pll_usb_;
    const Clocks& clocks_;
    const Watchdog& wd_;
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_CLOCK_TREE_H
