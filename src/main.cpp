// main.cpp - CLI front-end for the RP2040 simulator.
//
// Loads an ARM ELF32 image and runs it on the Cortex-M0+ core until it spins
// on a self-branch, hits a breakpoint, faults, or reaches an instruction cap.
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "core/scs.h"
#include "loaders/elf_loader.h"
#include "simulator.h"

namespace {

void print_usage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options] <firmware.elf>\n"
              << "\n"
              << "Options:\n"
              << "  --max <n>      Stop after n instructions (default 10000000)\n"
              << "  --entry        Start at e_entry instead of the reset vector\n"
              << "  --trace        Print PC + cycle for every instruction\n"
              << "  --version      Print the core version and exit\n"
              << "  --help         Show this message\n";
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

int run_image(const std::string& path, std::uint64_t max_instructions,
              bool from_entry, bool trace) {
    rp2040::RegisterFile regs;
    rp2040::Memory mem;
    rp2040::Cpu cpu(regs, mem);
    rp2040::Scs scs(cpu);
    scs.attach(mem);

    const rp2040::ElfImage img = rp2040::load_elf_file(mem, path);
    if (!img.ok) {
        std::cerr << "error: " << path << ": " << img.error << '\n';
        return 1;
    }
    std::cout << "loaded " << img.segments_loaded << " segment(s), "
              << "0x" << std::hex << img.lowest_addr << "-0x" << img.highest_addr
              << std::dec << ", entry 0x" << std::hex << img.entry << std::dec << '\n';

    if (from_entry) {
        regs.set_pc(img.entry & ~std::uint32_t{1});
        regs.set_thumb((img.entry & 1u) != 0);
        regs.set_msp(rp2040::kSramBase + rp2040::kSramSize);  // top of SRAM
    } else {
        cpu.set_vtor(img.lowest_addr);
        cpu.reset();
    }

    std::uint32_t last_pc = ~0u;
    std::uint64_t retired = 0;
    rp2040::ExecStatus status = rp2040::ExecStatus::Ok;

    for (; retired < max_instructions; ++retired) {
        const std::uint32_t pc = regs.pc();
        if (trace) {
            std::printf("[%10llu cyc] pc=%08X\n",
                        static_cast<unsigned long long>(cpu.cycle_count()), pc);
        }
        status = cpu.step();
        if (status == rp2040::ExecStatus::Ok || status == rp2040::ExecStatus::ExceptionTaken) {
            if (regs.pc() == pc && status == rp2040::ExecStatus::Ok) {
                std::cout << "halted: self-branch at 0x" << std::hex << pc << std::dec << '\n';
                break;
            }
            last_pc = pc;
            continue;
        }
        std::cout << "halted: " << status_name(status)
                  << " at 0x" << std::hex << pc << std::dec << '\n';
        break;
    }
    if (retired == max_instructions) {
        std::cout << "halted: instruction cap reached\n";
    }
    (void)last_pc;

    std::printf("retired %llu instructions, %llu cycles\n",
                static_cast<unsigned long long>(retired),
                static_cast<unsigned long long>(cpu.cycle_count()));
    for (unsigned r = 0; r < 16; r += 4) {
        std::printf("  r%-2u=%08X  r%-2u=%08X  r%-2u=%08X  r%-2u=%08X\n",
                    r, regs.get(r), r + 1, regs.get(r + 1),
                    r + 2, regs.get(r + 2), r + 3, regs.get(r + 3));
    }
    std::printf("  xpsr=%08X  primask=%u  mode=%s\n", regs.xpsr(), regs.primask() ? 1u : 0u,
                regs.mode() == rp2040::CpuMode::Handler ? "handler" : "thread");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t max_instructions = 10'000'000;
    bool from_entry = false;
    bool trace = false;
    std::string image;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        if (arg == "--version") { std::cout << rp2040::version_string() << '\n'; return 0; }
        if (arg == "--entry") { from_entry = true; continue; }
        if (arg == "--trace") { trace = true; continue; }
        if (arg == "--max") {
            if (i + 1 >= argc) { std::cerr << "error: --max needs an argument\n"; return 2; }
            max_instructions = std::stoull(argv[++i]);
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "error: unrecognised argument '" << arg << "'\n";
            print_usage(argv[0]);
            return 2;
        }
        image = arg;
    }

    if (image.empty()) {
        print_usage(argv[0]);
        return 2;
    }
    return run_image(image, max_instructions, from_entry, trace);
}
