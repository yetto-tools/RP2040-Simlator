// main.cpp - CLI front-end for the RP2040 simulator.
//
// Scaffold stage: parses only the flags needed to prove the build works.
// Firmware loading, the GDB stub and PIO tracing are added in later phases
// (see BACKLOG.md, Phase 7).
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "simulator.h"

namespace {

void print_usage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options] [firmware.elf]\n"
              << "\n"
              << "Options:\n"
              << "  --cycles <n>   Run the (idle) machine for n cycles and exit\n"
              << "  --version      Print the core version and exit\n"
              << "  --help         Show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t cycles_to_run = 0;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            std::cout << rp2040::version_string() << '\n';
            return 0;
        }
        if (arg == "--cycles") {
            if (i + 1 >= argc) {
                std::cerr << "error: --cycles requires an argument\n";
                return 2;
            }
            cycles_to_run = std::stoull(argv[++i]);
            continue;
        }
        std::cerr << "error: unrecognised argument '" << arg << "'\n";
        print_usage(argv[0]);
        return 2;
    }

    rp2040::Simulator sim;
    if (cycles_to_run > 0) {
        sim.step(cycles_to_run);
    }
    std::cout << sim.status_line() << '\n';
    return 0;
}
