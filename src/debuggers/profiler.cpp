#include "debuggers/profiler.h"

#include <algorithm>

namespace rp2040 {

void Profiler::reset() {
    pc_count_.clear();
    pc_cycles_.clear();
    instr_ = 0;
    cycles_ = 0;
    handler_stack_.clear();
    exc_entries_.fill(0);
    exc_total_.fill(0);
    exc_max_.fill(0);
}

Simulator::RunResult Profiler::run(std::uint64_t max_instructions) {
    Simulator::RunResult r;
    Cpu& cpu = sim_.cpu();

    for (; r.instructions < max_instructions; ++r.instructions) {
        const std::uint32_t pc = sim_.regs().pc();
        const std::uint64_t cyc_before = cpu.cycle_count();

        r.status = sim_.step();

        const std::uint64_t spent = cpu.cycle_count() - cyc_before;
        pc_count_[pc] += 1;
        pc_cycles_[pc] += spent;
        cycles_ += spent;
        instr_ += 1;

        if (r.status == ExecStatus::ExceptionTaken) {
            const unsigned v = cpu.current_exception();
            if (v < exc_entries_.size()) exc_entries_[v] += 1;
            handler_stack_.push_back({v, cycles_});
        }

        // Pop any handler frames we have returned out of.
        const unsigned cur = cpu.current_exception();
        while (!handler_stack_.empty() && handler_stack_.back().vec != cur) {
            const Frame f = handler_stack_.back();
            handler_stack_.pop_back();
            const std::uint64_t dur = cycles_ - f.start_cycle;
            if (f.vec < exc_total_.size()) {
                exc_total_[f.vec] += dur;
                exc_max_[f.vec] = std::max(exc_max_[f.vec], dur);
            }
        }

        if (r.status == ExecStatus::WaitingForInterrupt) continue;
        if (r.status == ExecStatus::Ok || r.status == ExecStatus::ExceptionTaken) {
            if (r.status == ExecStatus::Ok && sim_.regs().pc() == pc) {
                r.self_branch = true;
                r.stopped_at = pc;
                break;
            }
            continue;
        }
        r.stopped_at = pc;
        break;
    }
    if (r.instructions == max_instructions) r.hit_cap = true;
    r.cycles = cpu.cycle_count();
    return r;
}

Profiler::Report Profiler::report(std::size_t top_n) const {
    Report rep;
    rep.instructions = instr_;
    rep.cycles = cycles_;
    rep.cycles_per_instruction =
        instr_ != 0 ? static_cast<double>(cycles_) / static_cast<double>(instr_) : 0.0;

    rep.hot_spots.reserve(pc_count_.size());
    for (const auto& [pc, count] : pc_count_) {
        const auto it = pc_cycles_.find(pc);
        rep.hot_spots.push_back({pc, count, it != pc_cycles_.end() ? it->second : 0});
    }
    std::sort(rep.hot_spots.begin(), rep.hot_spots.end(),
              [](const HotSpot& a, const HotSpot& b) { return a.count > b.count; });
    if (rep.hot_spots.size() > top_n) rep.hot_spots.resize(top_n);

    for (unsigned v = 0; v < exc_entries_.size(); ++v) {
        if (exc_entries_[v] == 0) continue;
        rep.exceptions.push_back({v, exc_entries_[v], exc_total_[v], exc_max_[v]});
        rep.total_exception_entries += exc_entries_[v];
    }
    std::sort(rep.exceptions.begin(), rep.exceptions.end(),
              [](const ExceptionStat& a, const ExceptionStat& b) { return a.entries > b.entries; });

    return rep;
}

}  // namespace rp2040
