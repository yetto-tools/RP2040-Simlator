#include "compiler.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#define RP2040LAB_DUP _dup
#define RP2040LAB_DUP2 _dup2
#define RP2040LAB_CLOSE _close
#define RP2040LAB_FILENO _fileno
#define RP2040LAB_OPEN _open
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#define RP2040LAB_DUP dup
#define RP2040LAB_DUP2 dup2
#define RP2040LAB_CLOSE close
#define RP2040LAB_FILENO fileno
#define RP2040LAB_OPEN open
extern char** environ;
#endif

namespace rp2040lab {

namespace {
namespace fs = std::filesystem;

std::string g_gcc_path;
std::string g_gxx_path;
std::string g_objdump_path;
std::string g_linker_script;
std::atomic<std::uint64_t> g_counter{0};

std::string g_pico_sdk_path;
std::string g_pico_cmake_path;
std::string g_pico_ninja_path;
std::string g_pico_toolchain_dir;

// arm-none-eabi-objdump and arm-none-eabi-g++ sit next to arm-none-eabi-gcc
// in the same toolchain bin/ directory, just under different names.
std::string derive_sibling_tool(const std::string& gcc_path, const std::string& replacement) {
    fs::path p(gcc_path);
    std::string name = p.filename().string();
    const auto pos = name.rfind("gcc");
    if (pos == std::string::npos) return {};
    name.replace(pos, 3, replacement);
    return (p.parent_path() / name).string();
}

std::string derive_objdump_path(const std::string& gcc_path) {
    return derive_sibling_tool(gcc_path, "objdump");
}

std::uint64_t next_id() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return static_cast<std::uint64_t>(now) ^ g_counter.fetch_add(1);
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Spawns `argv[0]` with the given arguments (a real argument vector - no
// shell involved, so no quoting ambiguity), redirecting the child's
// stdout/stderr to `log_path`. Returns the exit code, or -1 on spawn
// failure.
int run_process(const std::vector<std::string>& args, const fs::path& log_path) {
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    std::fflush(stdout);
    std::fflush(stderr);
    const int saved_out = RP2040LAB_DUP(RP2040LAB_FILENO(stdout));
    const int saved_err = RP2040LAB_DUP(RP2040LAB_FILENO(stderr));
#if defined(_WIN32)
    const int log_fd = RP2040LAB_OPEN(log_path.string().c_str(),
                                       _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
#else
    const int log_fd = RP2040LAB_OPEN(log_path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
    if (log_fd >= 0) {
        RP2040LAB_DUP2(log_fd, RP2040LAB_FILENO(stdout));
        RP2040LAB_DUP2(log_fd, RP2040LAB_FILENO(stderr));
        RP2040LAB_CLOSE(log_fd);
    }

    int rc = -1;
#if defined(_WIN32)
    rc = static_cast<int>(_spawnv(_P_WAIT, argv[0], argv.data()));
#else
    pid_t pid = 0;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv.data()),
                     environ) == 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif

    std::fflush(stdout);
    std::fflush(stderr);
    RP2040LAB_DUP2(saved_out, RP2040LAB_FILENO(stdout));
    RP2040LAB_DUP2(saved_err, RP2040LAB_FILENO(stderr));
    RP2040LAB_CLOSE(saved_out);
    RP2040LAB_CLOSE(saved_err);
    return rc;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

// Rejects anything that isn't a flat source/header file - these get written
// to disk with the caller-supplied name, so path separators and ".." aren't
// just a formatting nicety to reject.
bool is_safe_filename(const std::string& name) {
    if (name.empty() || name.front() == '.') return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    if (name.find("..") != std::string::npos) return false;
    static const char* const kExts[] = {".c", ".h", ".cpp", ".hpp", ".s", ".S"};
    for (const char* ext : kExts) {
        const std::size_t elen = std::strlen(ext);
        if (name.size() > elen && name.compare(name.size() - elen, elen, ext) == 0) return true;
    }
    return false;
}

// Parses `arm-none-eabi-objdump -dl <elf>` output. Lines alternate between
// a source-location marker (bare "path/to/file.c:NN") and disassembled
// instruction lines ("  <hex-addr>:\t..."); the first instruction address
// following each marker is that source line's breakpoint address.
std::vector<LineAddr> parse_objdump_line_map(const std::string& text) {
    std::vector<LineAddr> out;
    std::string pending_file;
    int pending_line = 0;
    bool have_pending = false;
    std::istringstream in(text);
    std::string raw_line;
    while (std::getline(in, raw_line)) {
        const std::string line = trim(raw_line);
        if (line.empty()) continue;

        // Source-location marker: bare "<path>:<digits>", no tab (an
        // instruction line always has one after the address, since
        // --no-show-raw-insn still tab-separates mnemonic from address).
        if (line.find('\t') == std::string::npos) {
            const auto colon = line.rfind(':');
            if (colon != std::string::npos) {
                const std::string ln = line.substr(colon + 1);
                if (!ln.empty() &&
                    std::all_of(ln.begin(), ln.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    pending_file = fs::path(line.substr(0, colon)).filename().string();
                    pending_line = std::stoi(ln);
                    have_pending = true;
                    continue;
                }
            }
        }

        if (have_pending) {
            const auto addr_end = line.find(':');
            if (addr_end != std::string::npos && addr_end > 0) {
                const std::string hex = trim(line.substr(0, addr_end));
                const bool looks_hex = !hex.empty() && std::all_of(hex.begin(), hex.end(), [](unsigned char c) {
                                            return std::isxdigit(c) != 0;
                                        });
                if (looks_hex) {
                    try {
                        const auto addr = static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16));
                        out.push_back({pending_file, pending_line, addr});
                    } catch (...) {
                    }
                    have_pending = false;
                }
            }
        }
    }
    return out;
}

}  // namespace

void configure_compiler(std::string gcc_path, std::string linker_script_path) {
    g_gcc_path = std::move(gcc_path);
    g_gxx_path = derive_sibling_tool(g_gcc_path, "g++");
    g_objdump_path = derive_objdump_path(g_gcc_path);
    g_linker_script = std::move(linker_script_path);
}

bool compiler_available() {
    return !g_gcc_path.empty() && fs::exists(g_gcc_path) && !g_linker_script.empty() &&
           fs::exists(g_linker_script);
}

CompileResult compile_firmware(const std::vector<SourceFile>& files) {
    CompileResult result;
    if (!compiler_available()) {
        result.log = "arm-none-eabi-gcc or the linker script was not found at server startup";
        return result;
    }
    if (files.empty()) {
        result.log = "no source files submitted";
        return result;
    }
    for (const auto& f : files) {
        if (!is_safe_filename(f.name)) {
            result.log = "invalid file name: " + f.name;
            return result;
        }
    }

    // A fresh subdirectory per request (unlike the pico-sdk path below,
    // this compile is fast enough not to need a persistent, incrementally
    // rebuilt project dir) - real names so #include "..." resolves the
    // same way it would in any other multi-file C build, and so objdump's
    // line map reports the names the editor's tabs actually use.
    const fs::path dir = fs::temp_directory_path() / ("rp2040lab_src_" + std::to_string(next_id()));
    std::error_code ec;
    fs::create_directories(dir, ec);

    auto ends_with = [](const std::string& s, const char* suffix) {
        const std::size_t n = std::strlen(suffix);
        return s.size() > n && s.compare(s.size() - n, n, suffix) == 0;
    };
    auto is_header = [&](const std::string& n) { return ends_with(n, ".h") || ends_with(n, ".hpp"); };
    auto is_cpp    = [&](const std::string& n) { return ends_with(n, ".cpp"); };

    std::vector<std::string> src_files;
    bool has_cpp = false;
    for (const auto& f : files) {
        std::ofstream out(dir / f.name, std::ios::binary);
        out << f.content;
        if (!is_header(f.name)) {
            src_files.push_back((dir / f.name).string());
            if (is_cpp(f.name)) has_cpp = true;
        }
    }

    const fs::path elf_path = dir / "out.elf";
    const fs::path log_path = dir / "log.txt";

    const bool use_gxx = has_cpp && !g_gxx_path.empty() && fs::exists(g_gxx_path);
    std::vector<std::string> args = {
        use_gxx ? g_gxx_path : g_gcc_path,
        "-mcpu=cortex-m0plus", "-mthumb", "-nostdlib", "-nostartfiles", "-ffreestanding", "-O2", "-g",
        "-Wl,--build-id=none", "-Wl,--no-warn-rwx-segments",
        "-T", g_linker_script,
        "-o", elf_path.string(),
    };
    if (use_gxx) {
        args.push_back("-fno-exceptions");
        args.push_back("-fno-rtti");
    }
    args.insert(args.end(), src_files.begin(), src_files.end());

    const int rc = run_process(args, log_path);
    result.log = read_file(log_path);

    if (rc == 0 && fs::exists(elf_path) && fs::file_size(elf_path) > 0) {
        std::ifstream ef(elf_path, std::ios::binary);
        result.elf.assign(std::istreambuf_iterator<char>(ef), std::istreambuf_iterator<char>());
        result.ok = true;

        if (!g_objdump_path.empty() && fs::exists(g_objdump_path)) {
            const fs::path dump_path = dir / "dump.txt";
            run_process({g_objdump_path, "-dl", "--no-show-raw-insn", elf_path.string()}, dump_path);
            result.line_map = parse_objdump_line_map(read_file(dump_path));
        }
    }

    fs::remove_all(dir, ec);

    return result;
}

// --- pico-sdk compile mode (BACKLOG.md P10.3) -------------------------------
//
// Unlike compile_firmware() above, this uses a *persistent* project/build
// directory pair, reused across requests: pico-sdk's own CMake build
// compiles its core libraries (pico_runtime, hardware_gpio, hardware_uart,
// ...) the first time, which takes tens of seconds - a fresh temp directory
// per request (compile_firmware()'s pattern) would repeat that full rebuild
// on every single keystroke-to-compile cycle, which isn't interactive.
// Reusing the build dir lets Ninja's incremental build do its job: only
// main.c changes between requests, so only main.c (and the final link)
// needs to be redone.
namespace {

fs::path pico_project_dir() { return fs::temp_directory_path() / "rp2040lab_pico_project"; }
fs::path pico_build_dir() { return fs::temp_directory_path() / "rp2040lab_pico_build"; }

// PICO_RUNTIME_SKIP_INIT_BOOTROM_RESET / ..._PER_CORE_...: pico-sdk's crt0
// jumps through the real boot ROM before main() on real hardware (see
// ARCHITECTURE.md "Local web lab" for why); this simulator's ROM is an
// empty 16 KiB block (no proprietary bootrom image), so the two runtime-init
// steps that call rom_func_lookup() against it must be skipped - pico-sdk
// ships this exact escape hatch for embedders who don't need it.
constexpr const char* kCMakeListsTemplate = R"CMAKE(cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(labfw C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# pico-sdk defaults several runtime helper libraries (bit-count intrinsics,
# integer divide, double/float math, memcpy/memset) to implementations that
# look up optimized routines in the RP2040's boot ROM at startup
# (rom_func_lookup(), registered eagerly via a preinit table regardless of
# whether the program actually uses them). This simulator's ROM is an empty
# 16 KiB block (no proprietary bootrom image - see ARCHITECTURE.md "Local
# web lab"), so those lookups fail and the SDK's own null-check faults
# immediately at boot, before main(). Every one of these libraries ships a
# plain-compiler (libgcc) fallback variant for exactly this situation -
# selecting it here, before pico_sdk_init(), avoids the ROM dependency
# entirely rather than papering over the fault.
set(PICO_DEFAULT_BIT_OPS_IMPL compiler)
set(PICO_DEFAULT_DIVIDER_IMPL compiler)
set(PICO_DEFAULT_DOUBLE_IMPL compiler)
set(PICO_DEFAULT_FLOAT_IMPL compiler)
set(PICO_DEFAULT_MEM_OPS_IMPL compiler)
pico_sdk_init()
# CONFIGURE_DEPENDS (CMake >=3.12) makes `cmake --build` notice when this
# glob's result changed (a file added/removed since the last compile) and
# reconfigure automatically - needed because compiler.cpp reuses this same
# project/build dir across requests (BACKLOG.md P10.4: multi-file support).
file(GLOB SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.s"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.S"
)
add_executable(labfw ${SOURCES})
target_compile_definitions(labfw PRIVATE
    PICO_RUNTIME_SKIP_INIT_BOOTROM_RESET=1
    PICO_RUNTIME_SKIP_INIT_PER_CORE_BOOTROM_RESET=1)
target_link_libraries(labfw
    pico_stdlib hardware_pwm hardware_adc hardware_dma
    hardware_i2c hardware_spi hardware_rtc hardware_watchdog)
pico_enable_stdio_usb(labfw 0)
pico_enable_stdio_uart(labfw 1)
pico_add_extra_outputs(labfw)
)CMAKE";

}  // namespace

void configure_pico_sdk(std::string sdk_path, std::string cmake_path, std::string ninja_path,
                         std::string toolchain_dir) {
    g_pico_sdk_path = std::move(sdk_path);
    g_pico_cmake_path = std::move(cmake_path);
    g_pico_ninja_path = std::move(ninja_path);
    g_pico_toolchain_dir = std::move(toolchain_dir);
}

bool pico_sdk_available() {
    return !g_pico_sdk_path.empty() && fs::exists(fs::path(g_pico_sdk_path) / "pico_sdk_init.cmake") &&
           !g_pico_cmake_path.empty() && fs::exists(g_pico_cmake_path) && !g_pico_ninja_path.empty() &&
           fs::exists(g_pico_ninja_path) && !g_pico_toolchain_dir.empty();
}

CompileResult compile_pico_sdk_firmware(const std::vector<SourceFile>& files) {
    CompileResult result;
    if (!pico_sdk_available()) {
        result.log = "pico-sdk was not found at server startup (PICO_SDK_PATH/cmake/ninja)";
        return result;
    }
    if (files.empty()) {
        result.log = "no source files submitted";
        return result;
    }
    for (const auto& f : files) {
        if (!is_safe_filename(f.name)) {
            result.log = "invalid file name: " + f.name;
            return result;
        }
    }

    const fs::path project_dir = pico_project_dir();
    const fs::path build_dir = pico_build_dir();
    std::error_code ec;
    fs::create_directories(project_dir, ec);

    const fs::path cmakelists_path = project_dir / "CMakeLists.txt";
    if (!fs::exists(cmakelists_path)) {
        std::ofstream f(cmakelists_path, std::ios::binary);
        f << kCMakeListsTemplate;
    }
    const fs::path import_path = project_dir / "pico_sdk_import.cmake";
    if (!fs::exists(import_path)) {
        fs::copy_file(fs::path(g_pico_sdk_path) / "external" / "pico_sdk_import.cmake", import_path, ec);
    }

    // Drop any *.c/*.h left over from a previous compile that isn't in the
    // current file set (the user deleted it client-side) - otherwise it
    // would linger, still get picked up by CMakeLists.txt's source glob,
    // and stay linked into firmware that's supposed to no longer include it.
    std::set<std::string> keep;
    for (const auto& f : files) keep.insert(f.name);
    for (const auto& entry : fs::directory_iterator(project_dir)) {
        const std::string name = entry.path().filename().string();
        auto ew = [&](const char* s) {
            const std::size_t n = std::strlen(s);
            return name.size() > n && name.compare(name.size() - n, n, s) == 0;
        };
        const bool is_source_or_header = ew(".c") || ew(".h") || ew(".cpp") || ew(".hpp") || ew(".s") || ew(".S");
        if (is_source_or_header && keep.find(name) == keep.end()) fs::remove(entry.path(), ec);
    }
    for (const auto& f : files) {
        std::ofstream out(project_dir / f.name, std::ios::binary);
        out << f.content;
    }

    const fs::path log_path = fs::temp_directory_path() / "rp2040lab_pico_log.txt";

    if (!fs::exists(build_dir / "build.ninja")) {
        const std::vector<std::string> configure_args = {
            g_pico_cmake_path,
            "-S", project_dir.string(),
            "-B", build_dir.string(),
            "-G", "Ninja",
            std::string("-DCMAKE_MAKE_PROGRAM=") + g_pico_ninja_path,
            "-DPICO_BOARD=pico",
            "-DPICO_PLATFORM=rp2040",
            "-DCMAKE_BUILD_TYPE=Debug",
            std::string("-DPICO_TOOLCHAIN_PATH=") + g_pico_toolchain_dir,
            std::string("-DPICO_SDK_PATH=") + g_pico_sdk_path,
        };
        const int configure_rc = run_process(configure_args, log_path);
        if (configure_rc != 0) {
            result.log = "cmake configure failed:\n" + read_file(log_path);
            fs::remove(log_path, ec);
            fs::remove_all(build_dir, ec);  // don't leave a half-configured build dir behind
            return result;
        }
    }

    const std::vector<std::string> build_args = {g_pico_cmake_path, "--build", build_dir.string()};
    const int build_rc = run_process(build_args, log_path);
    result.log = read_file(log_path);
    fs::remove(log_path, ec);

    const fs::path elf_path = build_dir / "labfw.elf";
    if (build_rc == 0 && fs::exists(elf_path) && fs::file_size(elf_path) > 0) {
        std::ifstream ef(elf_path, std::ios::binary);
        result.elf.assign(std::istreambuf_iterator<char>(ef), std::istreambuf_iterator<char>());
        result.ok = true;

        if (!g_objdump_path.empty() && fs::exists(g_objdump_path)) {
            const fs::path dump_path = fs::temp_directory_path() / "rp2040lab_pico_dump.txt";
            run_process({g_objdump_path, "-dl", "--no-show-raw-insn", elf_path.string()}, dump_path);
            result.line_map = parse_objdump_line_map(read_file(dump_path));
            fs::remove(dump_path, ec);
        }
    }

    return result;
}

}  // namespace rp2040lab
