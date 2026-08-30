#include "simulator.h"

namespace rp2040 {

Simulator::Simulator(SimulatorConfig config) : config_(config) {}

void Simulator::step(std::uint64_t n) {
    // TODO(phase1): drive CPU cores, PIO blocks and peripherals here.
    // For now the machine is an idle clock so the harness has something
    // deterministic to test against.
    cycles_ += n;
}

std::string Simulator::status_line() const {
    return "rp2040-sim " + std::string(version_string()) +
           " | sys_clk=" + std::to_string(config_.sys_clk_hz) + "Hz" +
           " | cycles=" + std::to_string(cycles_);
}

const char* version_string() { return "0.1.0"; }

}  // namespace rp2040
