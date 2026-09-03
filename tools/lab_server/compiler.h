// compiler.h - spawns arm-none-eabi-gcc to build freestanding Cortex-M0+
// firmware for the local web "virtual lab" (rp2040-lab-server).
//
// v1 firmware model matches tests/fixtures/sum.c: freestanding C with a
// _start, no libc, no pico-sdk - the same flags and linker script
// tests/CMakeLists.txt's firmware fixture already uses. Full pico-sdk
// support is a documented follow-up (BACKLOG.md), not attempted here.
#ifndef RP2040LAB_COMPILER_H
#define RP2040LAB_COMPILER_H

#include <cstdint>
#include <string>
#include <vector>

namespace rp2040lab {

// A user-submitted file, e.g. "main.c" / "helpers.h", flat (no
// subdirectories) - BACKLOG.md P10.4's multi-file editing, not a general
// multi-file CMake project.
struct SourceFile {
    std::string name;
    std::string content;
};

// One C source line's first instruction address, for mapping an editor
// gutter click (a file + line number) to the PC address /breakpoints
// expects. Built from `arm-none-eabi-objdump -dl`'s interleaved
// source/disassembly output (see compiler.cpp) rather than a full DWARF
// .debug_line parse - good enough for this project's flat, C-only firmware
// model, not a general-purpose debug-info reader.
struct LineAddr {
    std::string file;  // basename only, e.g. "main.c"
    int line = 0;
    std::uint32_t addr = 0;
};

struct CompileResult {
    bool ok = false;
    std::vector<std::uint8_t> elf;
    std::string log;  // combined compiler/linker stdout+stderr
    std::vector<LineAddr> line_map;
};

// Must be called once at startup (see main.cpp) with the paths CMake
// resolved: arm-none-eabi-gcc and the freestanding linker script. Also
// resolves arm-none-eabi-objdump from the same toolchain directory as
// gcc_path, for the line_map above.
void configure_compiler(std::string gcc_path, std::string linker_script_path);

// True once configure_compiler() has been given a real, existing gcc path
// (mirrors tests/CMakeLists.txt's own "not found -> skip" graceful
// degradation instead of failing the whole server to start).
bool compiler_available();

CompileResult compile_firmware(const std::vector<SourceFile>& files);

// pico-sdk compile mode (BACKLOG.md P10.3/P10.4) - a real
// `pico_stdlib`-linked multi-file project, built with the SDK's own
// CMake+Ninja flow rather than a bare gcc invocation. See compiler.cpp for
// why a persistent project/build directory (not a fresh one per request,
// unlike compile_firmware() above) is required for this to be fast enough
// to be interactive.
void configure_pico_sdk(std::string sdk_path, std::string cmake_path, std::string ninja_path,
                         std::string toolchain_dir);

bool pico_sdk_available();

CompileResult compile_pico_sdk_firmware(const std::vector<SourceFile>& files);

}  // namespace rp2040lab

#endif  // RP2040LAB_COMPILER_H
