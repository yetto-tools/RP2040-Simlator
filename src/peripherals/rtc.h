// rtc.h - RP2040 RTC (datasheet 4.8) @ 0x4005C000.
//
// Functional calendar clock: SETUP_0/1 + CTRL.LOAD set the time, on_cycles()
// advances it one second per CLKDIV_M1+1 RTC ticks, and a field-masked alarm
// raises RTC_IRQ (IRQ25).
#ifndef RP2040_PERIPHERALS_RTC_H
#define RP2040_PERIPHERALS_RTC_H

#include <cstdint>

#include "core/atomic_peripheral.h"
#include "core/bus.h"
#include "core/cpu.h"
#include "core/interrupt_controller.h"
#include "core/memory.h"

namespace rp2040 {

class Rtc : public AtomicPeripheral {
public:
    static constexpr std::uint32_t kBase = 0x4005C000u;
    static constexpr std::uint32_t kSize = AtomicPeripheral::kAtomicSize;
    static constexpr unsigned kIrq = kExcExternal0 + 25;  // RTC_IRQ == IRQ25

    // `rtc_tick_hz` is the clock feeding the RTC divider (48 kHz by default on
    // the RP2040, from clk_rtc); `sys_clk_hz` scales on_cycles().
    Rtc(Cpu& cpu, std::uint32_t rtc_tick_hz = 46875u, std::uint32_t sys_clk_hz = 125'000'000u)
        : nvic_(cpu), rtc_hz_(rtc_tick_hz), sys_hz_(sys_clk_hz) {}

    bool attach(Memory& mem) { return mem.attach_peripheral(kBase, kSize, this); }

    BusResult<std::uint32_t> reg_read(std::uint32_t reg, BusWidth w) override;
    BusStatus reg_write(std::uint32_t reg, std::uint32_t value, BusWidth w) override;
    void reset() override;

    void on_cycles(std::uint64_t sys_cycles);

    // clk_rtc (post-CLKDIV) and clk_sys in Hz (pushed by the clock tree).
    void set_clock_hz(std::uint32_t rtc_hz, std::uint32_t sys_hz) {
        rtc_hz_ = rtc_hz == 0 ? 1u : rtc_hz;
        sys_hz_ = sys_hz == 0 ? 1u : sys_hz;
    }

    // Wire the second Cortex-M0+ core into this peripheral's IRQ.
    void connect_core1(Cpu* c) { nvic_.connect(c); }

private:
    struct DateTime {
        std::uint32_t year = 0, month = 1, day = 1;
        std::uint32_t dotw = 0, hour = 0, min = 0, sec = 0;
    };
    void tick_second();
    void check_alarm();
    std::uint32_t pack_date(const DateTime&) const;
    std::uint32_t pack_time(const DateTime&) const;

    InterruptController nvic_;
    std::uint32_t rtc_hz_;
    std::uint32_t sys_hz_;

    std::uint32_t clkdiv_m1_ = 46874u;
    std::uint32_t setup0_ = 0, setup1_ = 0;
    bool enabled_ = false;
    DateTime now_;

    std::uint32_t irq_setup0_ = 0, irq_setup1_ = 0;   // alarm fields + MATCH_ENA bits
    std::uint32_t intr_ = 0, inte_ = 0, intf_ = 0;

    std::uint64_t sub_accum_ = 0;   // sys cycles toward one RTC tick
    std::uint32_t div_counter_ = 0; // RTC ticks toward one second
    bool alarm_matched_ = false;    // edge-detect: alarm fires on the 0->1 match
};

}  // namespace rp2040

#endif  // RP2040_PERIPHERALS_RTC_H
