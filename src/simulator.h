// simulator.h - Top-level orchestrator that ties the CPU cores, PIO blocks,
// peripherals and clock together and advances them cycle by cycle.
//
// This is a scaffold: the run loop and subsystem wiring are filled in as the
// components land (see BACKLOG.md, Phase 1+). The public surface is kept
// minimal on purpose so the CLI and tests have a stable entry point.
#ifndef RP2040_SIMULATOR_H
#define RP2040_SIMULATOR_H

#include <cstdint>
#include <string>

#include "rp2040.h"

namespace rp2040 {

struct SimulatorConfig {
    std::uint32_t sys_clk_hz = kDefaultSysClkHz;
};

class Simulator {
public:
    Simulator() = default;
    explicit Simulator(SimulatorConfig config);

    // Total number of system clock cycles retired so far. Deterministic.
    std::uint64_t cycle_count() const { return cycles_; }

    // Advance the whole machine by exactly `n` system clock cycles.
    // Currently a no-op placeholder that only moves the cycle counter.
    void step(std::uint64_t n = 1);

    // Human-readable one-line status, useful for the CLI and smoke tests.
    std::string status_line() const;

private:
    SimulatorConfig config_{};
    std::uint64_t cycles_ = 0;
};

// Semantic version of the simulator core.
const char* version_string();

}  // namespace rp2040

#endif  // RP2040_SIMULATOR_H
