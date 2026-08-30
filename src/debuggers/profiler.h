// profiler.h - performance instrumentation for a running Simulator
// (BACKLOG P7.6).
//
// Drives the machine like Simulator::run() but records, per retired
// instruction: the fetch PC (for a hot-spot histogram), the cycles it cost
// (for CPI), and exception activity (count and handler duration per vector).
#ifndef RP2040_DEBUGGERS_PROFILER_H
#define RP2040_DEBUGGERS_PROFILER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "core/cpu.h"
#include "exceptions.h"
#include "simulator.h"

namespace rp2040 {

class Profiler {
public:
    explicit Profiler(Simulator& sim) : sim_(sim) {}

    void reset();

    // Run up to `max_instructions`, stopping on the same conditions as
    // Simulator::run(). Instrumentation accumulates across calls until reset().
    Simulator::RunResult run(std::uint64_t max_instructions);

    struct HotSpot {
        std::uint32_t pc;
        std::uint64_t count;
        std::uint64_t cycles;
    };
    struct ExceptionStat {
        unsigned vector = 0;
        std::uint64_t entries = 0;
        std::uint64_t total_handler_cycles = 0;
        std::uint64_t max_handler_cycles = 0;
    };
    struct Report {
        std::uint64_t instructions = 0;
        std::uint64_t cycles = 0;
        double cycles_per_instruction = 0.0;
        std::vector<HotSpot> hot_spots;          // sorted by count desc, top N
        std::vector<ExceptionStat> exceptions;   // vectors that fired, count desc
        std::uint64_t total_exception_entries = 0;
    };
    Report report(std::size_t top_n = 10) const;

private:
    Simulator& sim_;

    std::map<std::uint32_t, std::uint64_t> pc_count_;
    std::map<std::uint32_t, std::uint64_t> pc_cycles_;
    std::uint64_t instr_ = 0;
    std::uint64_t cycles_ = 0;

    struct Frame { unsigned vec; std::uint64_t start_cycle; };
    std::vector<Frame> handler_stack_;
    std::array<std::uint64_t, kExceptionTableEntries> exc_entries_{};
    std::array<std::uint64_t, kExceptionTableEntries> exc_total_{};
    std::array<std::uint64_t, kExceptionTableEntries> exc_max_{};
};

}  // namespace rp2040

#endif  // RP2040_DEBUGGERS_PROFILER_H
