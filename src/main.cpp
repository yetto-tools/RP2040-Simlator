// main.cpp - CLI front-end for the RP2040 simulator.
//
// Loads an ARM ELF32 or UF2 image and runs it on the Cortex-M0+ core (with its
// peripherals) until it spins on a self-branch, faults, or reaches an
// instruction cap.
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

#include "debuggers/gdb_stub.h"
#include "simulator.h"

namespace {

void print_usage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options] <firmware.elf|firmware.uf2>\n"
              << "\n"
              << "Options:\n"
              << "  --gdb <port>  Wait for arm-none-eabi-gdb on localhost:<port>\n"
              << "  --max <n>   Stop after n instructions (default 10000000)\n"
              << "  --entry     Start at e_entry instead of the reset vector\n"
              << "  --version   Print the core version and exit\n"
              << "  --help      Show this message\n";
}

const char* status_name(rp2040::ExecStatus s) {
    using S = rp2040::ExecStatus;
    switch (s) {
        case S::Ok: return "ok";
        case S::Unimplemented: return "unimplemented-instruction";
        case S::Undefined: return "undefined-instruction";
        case S::Breakpoint: return "breakpoint";
        case S::Svc: return "svc";
        case S::MemFault: return "memory-fault";
        case S::WaitingForInterrupt: return "wfi";
        case S::ExceptionTaken: return "exception-taken";
        case S::Lockup: return "lockup";
    }
    return "?";
}

int run_image(const std::string& path, std::uint64_t max_instructions, bool from_entry,
              int gdb_port) {
    rp2040::Simulator sim;
    const rp2040::ElfImage img = sim.load(path, from_entry);
    if (!img.ok) {
        std::cerr << "error: " << path << ": " << img.error << '\n';
        return 1;
    }
    std::printf("loaded %u segment(s), 0x%08X-0x%08X, entry 0x%08X\n",
                img.segments_loaded, img.lowest_addr, img.highest_addr, img.entry);

    if (gdb_port > 0) {
        rp2040::GdbStub stub(sim);
        if (!stub.serve(static_cast<std::uint16_t>(gdb_port))) {
            std::cerr << "error: could not start the gdb stub on port " << gdb_port << '\n';
            return 1;
        }
        std::printf("gdb session ended\n");
        return 0;
    }

    const rp2040::Simulator::RunResult r = sim.run(max_instructions);
    if (r.self_branch) {
        std::printf("halted: self-branch at 0x%08X\n", r.stopped_at);
    } else if (r.hit_cap) {
        std::printf("halted: instruction cap reached\n");
    } else {
        std::printf("halted: %s at 0x%08X\n", status_name(r.status), r.stopped_at);
    }
    std::printf("retired %llu instructions, %llu cycles\n",
                static_cast<unsigned long long>(r.instructions),
                static_cast<unsigned long long>(r.cycles));

    rp2040::RegisterFile& regs = sim.regs();
    for (unsigned n = 0; n < 16; n += 4) {
        std::printf("  r%-2u=%08X  r%-2u=%08X  r%-2u=%08X  r%-2u=%08X\n",
                    n, regs.get(n), n + 1, regs.get(n + 1),
                    n + 2, regs.get(n + 2), n + 3, regs.get(n + 3));
    }
    std::printf("  xpsr=%08X  gpio_in=%08X\n", regs.xpsr(), sim.gpio().input_bits());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t max_instructions = 10'000'000;
    bool from_entry = false;
    int gdb_port = 0;
    std::string image;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        if (arg == "--version") { std::cout << rp2040::version_string() << '\n'; return 0; }
        if (arg == "--entry") { from_entry = true; continue; }
        if (arg == "--max") {
            if (i + 1 >= argc) { std::cerr << "error: --max needs an argument\n"; return 2; }
            max_instructions = std::stoull(argv[++i]);
            continue;
        }
        if (arg == "--gdb") {
            if (i + 1 >= argc) { std::cerr << "error: --gdb needs a port\n"; return 2; }
            gdb_port = std::stoi(argv[++i]);
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "error: unrecognised argument '" << arg << "'\n";
            print_usage(argv[0]);
            return 2;
        }
        image = arg;
    }

    if (image.empty()) { print_usage(argv[0]); return 2; }
    return run_image(image, max_instructions, from_entry, gdb_port);
}
