#include "debuggers/gdb_stub.h"

#include <array>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "ws2_32.lib")
#  endif
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
static int close_socket(socket_t s) { return closesocket(s); }
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
static int close_socket(socket_t s) { return ::close(s); }
#endif

namespace rp2040 {

namespace {

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char hex_digit(unsigned v) { return "0123456789abcdef"[v & 0xF]; }

// Plain hex, no leading zeros (except "0" itself) - for RSP text annotations
// like "watch:<addr>;", as opposed to the fixed-width little-endian register
// dump encoding u32_le_hex() produces.
std::string hex_plain(std::uint32_t v) {
    if (v == 0) return "0";
    std::string s;
    while (v != 0) {
        s.insert(s.begin(), hex_digit(v & 0xFu));
        v >>= 4;
    }
    return s;
}

// 32-bit value as 8 little-endian hex digits (GDB's target byte order).
std::string u32_le_hex(std::uint32_t v) {
    std::string s(8, '0');
    for (int i = 0; i < 4; ++i) {
        const unsigned b = (v >> (8 * i)) & 0xFFu;
        s[static_cast<std::size_t>(i) * 2] = hex_digit(b >> 4);
        s[static_cast<std::size_t>(i) * 2 + 1] = hex_digit(b & 0xF);
    }
    return s;
}

bool parse_u32_le_hex(const std::string& s, std::size_t pos, std::uint32_t& out) {
    if (pos + 8 > s.size()) return false;
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const int hi = hex_val(s[pos + static_cast<std::size_t>(i) * 2]);
        const int lo = hex_val(s[pos + static_cast<std::size_t>(i) * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        v |= (static_cast<std::uint32_t>((hi << 4) | lo)) << (8 * i);
    }
    out = v;
    return true;
}

// Parse "addr,len" (both big-endian hex, as GDB sends addresses).
bool parse_addr_len(const std::string& s, std::uint32_t& addr, std::uint32_t& len) {
    const auto comma = s.find(',');
    if (comma == std::string::npos) return false;
    addr = 0;
    len = 0;
    for (std::size_t i = 0; i < comma; ++i) {
        const int h = hex_val(s[i]);
        if (h < 0) return false;
        addr = (addr << 4) | static_cast<std::uint32_t>(h);
    }
    for (std::size_t i = comma + 1; i < s.size(); ++i) {
        const int h = hex_val(s[i]);
        if (h < 0) break;
        len = (len << 4) | static_cast<std::uint32_t>(h);
    }
    return true;
}

std::uint32_t parse_be_hex(const std::string& s) {
    std::uint32_t v = 0;
    for (char c : s) {
        const int h = hex_val(c);
        if (h < 0) break;
        v = (v << 4) | static_cast<std::uint32_t>(h);
    }
    return v;
}

}  // namespace

std::uint8_t GdbStub::checksum(const std::string& payload) {
    unsigned sum = 0;
    for (char c : payload) sum += static_cast<unsigned char>(c);
    return static_cast<std::uint8_t>(sum & 0xFFu);
}

std::string GdbStub::frame(const std::string& payload) {
    const std::uint8_t cs = checksum(payload);
    std::string out = "$" + payload + "#";
    out += hex_digit(cs >> 4);
    out += hex_digit(cs & 0xF);
    return out;
}

std::string GdbStub::unframe(const std::string& packet) {
    const auto dollar = packet.find('$');
    const auto hash = packet.rfind('#');
    if (dollar == std::string::npos || hash == std::string::npos || hash < dollar + 1) {
        return "";
    }
    return packet.substr(dollar + 1, hash - dollar - 1);
}

std::string GdbStub::read_registers() const {
    RegisterFile& r = sim_.regs(core_);
    std::string out;
    for (unsigned i = 0; i < 13; ++i) out += u32_le_hex(r.get(i));
    out += u32_le_hex(r.sp());
    out += u32_le_hex(r.lr());
    out += u32_le_hex(r.pc());
    out += u32_le_hex(r.xpsr());
    return out;
}

void GdbStub::write_registers(const std::string& hex) {
    RegisterFile& r = sim_.regs(core_);
    std::uint32_t v = 0;
    for (unsigned i = 0; i < 13; ++i) {
        if (parse_u32_le_hex(hex, i * 8u, v)) r.set(i, v);
    }
    if (parse_u32_le_hex(hex, 13u * 8u, v)) r.set_sp(v);
    if (parse_u32_le_hex(hex, 14u * 8u, v)) r.set_lr(v);
    if (parse_u32_le_hex(hex, 15u * 8u, v)) r.set_pc(v);
    if (parse_u32_le_hex(hex, 16u * 8u, v)) r.set_xpsr(v);
}

std::string GdbStub::read_memory(std::uint32_t addr, std::uint32_t len) const {
    // Suppressed: this is the debugger inspecting memory, not the CPU
    // executing - it must not re-trigger the watchpoint it's looking at.
    sim_.memory().suppress_watchpoints(true);
    std::string out;
    for (std::uint32_t i = 0; i < len; ++i) {
        const BusResult<std::uint8_t> b = sim_.memory().read_byte(addr + i);
        if (!b.ok()) { sim_.memory().suppress_watchpoints(false); return "E01"; }
        out += hex_digit(static_cast<unsigned>(b.value) >> 4);
        out += hex_digit(b.value & 0xF);
    }
    sim_.memory().suppress_watchpoints(false);
    return out;
}

bool GdbStub::write_memory(std::uint32_t addr, const std::string& hex) {
    sim_.memory().suppress_watchpoints(true);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = hex_val(hex[i]);
        const int lo = hex_val(hex[i + 1]);
        if (hi < 0 || lo < 0) { sim_.memory().suppress_watchpoints(false); return false; }
        const std::uint8_t byte = static_cast<std::uint8_t>((hi << 4) | lo);
        if (sim_.memory().write_byte(addr + static_cast<std::uint32_t>(i / 2), byte) != BusStatus::Ok) {
            sim_.memory().suppress_watchpoints(false);
            return false;
        }
    }
    sim_.memory().suppress_watchpoints(false);
    return true;
}

std::string GdbStub::run(bool single_step) {
    const int max_steps = single_step ? 1 : 100'000'000;
    for (int n = 0; n < max_steps; ++n) {
        const std::uint32_t pc = sim_.regs(core_).pc();
        const ExecStatus st = sim_.step();
        if (st == ExecStatus::Breakpoint) return "S05";
        if (st == ExecStatus::Lockup || st == ExecStatus::Undefined) return "S0B";
        std::uint32_t wp_addr = 0;
        bool wp_write = false;
        if (sim_.memory().take_watchpoint_hit(wp_addr, wp_write)) {
            return "T05" + std::string(wp_write ? "watch" : "rwatch") + ":" + hex_plain(wp_addr) + ";";
        }
        if (!single_step && breakpoints_.count(sim_.regs(core_).pc()) != 0) return "S05";
        if (!single_step && sim_.regs(core_).pc() == pc) return "S05";  // spin -> stop
    }
    return "S05";
}

std::string GdbStub::handle_packet(const std::string& p) {
    if (p.empty()) return "";
    const char cmd = p[0];
    const std::string arg = p.substr(1);

    switch (cmd) {
        case '?': return "S05";
        case 'g': return read_registers();
        case 'G': write_registers(arg); return "OK";
        case 'c': return run(/*single_step=*/false);
        case 's': return run(/*single_step=*/true);
        case 'k': return "";  // kill - handled by the transport loop
        case 'D': return "OK";  // detach

        case 'm': {
            std::uint32_t addr = 0, len = 0;
            if (!parse_addr_len(arg, addr, len)) return "E01";
            return read_memory(addr, len);
        }
        case 'M': {
            const auto colon = arg.find(':');
            if (colon == std::string::npos) return "E01";
            std::uint32_t addr = 0, len = 0;
            if (!parse_addr_len(arg.substr(0, colon), addr, len)) return "E01";
            return write_memory(addr, arg.substr(colon + 1)) ? "OK" : "E01";
        }
        case 'p': {
            const unsigned n = static_cast<unsigned>(parse_be_hex(arg));
            RegisterFile& r = sim_.regs(core_);
            if (n < 13) return u32_le_hex(r.get(n));
            if (n == 13) return u32_le_hex(r.sp());
            if (n == 14) return u32_le_hex(r.lr());
            if (n == 15) return u32_le_hex(r.pc());
            if (n == 16 || n == 25) return u32_le_hex(r.xpsr());
            return "xxxxxxxx";
        }
        case 'P': {
            const auto eq = arg.find('=');
            if (eq == std::string::npos) return "E01";
            const unsigned n = static_cast<unsigned>(parse_be_hex(arg.substr(0, eq)));
            std::uint32_t v = 0;
            if (!parse_u32_le_hex(arg, eq + 1, v)) return "E01";
            RegisterFile& r = sim_.regs(core_);
            if (n < 13) r.set(n, v);
            else if (n == 13) r.set_sp(v);
            else if (n == 14) r.set_lr(v);
            else if (n == 15) r.set_pc(v);
            else if (n == 16 || n == 25) r.set_xpsr(v);
            return "OK";
        }

        case 'Z':
        case 'z': {
            // Z0,addr,kind    / z0,addr,kind    - software breakpoint
            // Z2,addr,length  / z2,addr,length  - write watchpoint
            // Z3,addr,length  / z3,addr,length  - read watchpoint
            // Z4,addr,length  / z4,addr,length  - access (read+write) watchpoint
            if (arg.size() < 2 || arg[1] != ',') return "";
            const char type = arg[0];
            std::uint32_t addr = 0, len = 0;
            if (!parse_addr_len(arg.substr(2), addr, len)) return "E01";
            if (type == '0') {
                if (cmd == 'Z') breakpoints_.insert(addr);
                else            breakpoints_.erase(addr);
                return "OK";
            }
            if (type == '2' || type == '3' || type == '4') {
                if (cmd == 'Z') {
                    sim_.memory().add_watchpoint(addr, len, /*on_read=*/type != '2',
                                                  /*on_write=*/type != '3');
                } else {
                    sim_.memory().remove_watchpoint(addr);
                }
                return "OK";
            }
            return "";  // unsupported type
        }

        case 'q': {
            if (arg.rfind("Supported", 0) == 0) return "PacketSize=4000;qXfer:features:read-;QStartNoAckMode+";
            if (arg == "Attached") return "1";
            if (arg == "C") return "QC1";
            if (arg == "fThreadInfo") return "m1";
            if (arg == "sThreadInfo") return "l";
            if (arg.rfind("ThreadExtraInfo", 0) == 0) return "436f7265";  // "Core"
            return "";
        }
        case 'Q':
            if (arg == "StartNoAckMode") { no_ack_mode_ = true; return "OK"; }
            return "";
        case 'v':
            if (arg.rfind("Cont?", 0) == 0) return "vCont;c;s";
            if (arg.rfind("Cont;c", 0) == 0) return run(false);
            if (arg.rfind("Cont;s", 0) == 0) return run(true);
            if (arg.rfind("MustReplyEmpty", 0) == 0) return "";
            return "";
        case 'H': return "OK";   // set thread for subsequent ops
        case 'T': return "OK";   // thread alive

        default:
            return "";
    }
}

// --- TCP transport --------------------------------------------------------

namespace {

bool send_all(socket_t s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = ::send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

bool GdbStub::serve(std::uint16_t port) {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kInvalidSocket) return false;
    int yes = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listener, 1) != 0) {
        close_socket(listener);
        return false;
    }
    std::printf("gdb stub listening on localhost:%u - connect with:\n"
                "  arm-none-eabi-gdb -ex 'target remote localhost:%u'\n", port, port);

    const socket_t client = ::accept(listener, nullptr, nullptr);
    close_socket(listener);
    if (client == kInvalidSocket) return false;

    std::string rx;
    std::array<char, 4096> buf{};
    bool running = true;
    while (running) {
        const int n = ::recv(client, buf.data(), static_cast<int>(buf.size()), 0);
        if (n <= 0) break;
        rx.append(buf.data(), static_cast<std::size_t>(n));

        std::size_t pos;
        while ((pos = rx.find('$')) != std::string::npos) {
            const std::size_t hash = rx.find('#', pos);
            if (hash == std::string::npos || hash + 2 >= rx.size()) break;
            const std::string payload = rx.substr(pos + 1, hash - pos - 1);
            rx.erase(0, hash + 3);

            if (!no_ack_mode_ && !send_all(client, "+")) { running = false; break; }
            if (payload == "k") { running = false; break; }
            const std::string reply = handle_packet(payload);
            if (!send_all(client, frame(reply))) { running = false; break; }
        }
        // Swallow a leading ack/nak from the client.
        while (!rx.empty() && (rx[0] == '+' || rx[0] == '-')) rx.erase(0, 1);
    }
    close_socket(client);
#if defined(_WIN32)
    WSACleanup();
#endif
    return true;
}

}  // namespace rp2040
