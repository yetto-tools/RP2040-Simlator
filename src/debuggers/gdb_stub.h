// gdb_stub.h - GDB Remote Serial Protocol server for the simulator
// (BACKLOG P7.2). The protocol handler is pure (packet in -> packet out over
// a Simulator&); serve() wraps it in a TCP listener for arm-none-eabi-gdb.
#ifndef RP2040_DEBUGGERS_GDB_STUB_H
#define RP2040_DEBUGGERS_GDB_STUB_H

#include <cstdint>
#include <set>
#include <string>

#include "simulator.h"

namespace rp2040 {

class GdbStub {
public:
    explicit GdbStub(Simulator& sim) : sim_(sim) {}

    // Handle one RSP packet payload (the bytes between '$' and '#') and return
    // the reply payload (without framing). An empty string means "no reply".
    std::string handle_packet(const std::string& payload);

    // Frame a payload as "$<payload>#<checksum>".
    static std::string frame(const std::string& payload);
    // Extract the payload from "$...#xx"; returns "" if malformed.
    static std::string unframe(const std::string& packet);
    static std::uint8_t checksum(const std::string& payload);

    // Blocking: accept one connection on `port` and serve it until the client
    // detaches or disconnects. Returns false if the socket could not be set up.
    bool serve(std::uint16_t port);

    // Debugger which core the stub controls (RP2040 has two).
    void select_core(unsigned core) { core_ = core & 1u; }

private:
    std::string read_registers() const;
    void write_registers(const std::string& hex);
    std::string read_memory(std::uint32_t addr, std::uint32_t len) const;
    bool write_memory(std::uint32_t addr, const std::string& hex);
    std::string run(bool single_step);   // 'c' / 's' -> stop-reply packet

    Simulator& sim_;
    unsigned core_ = 0;
    std::set<std::uint32_t> breakpoints_;
    bool no_ack_mode_ = false;
};

}  // namespace rp2040

#endif  // RP2040_DEBUGGERS_GDB_STUB_H
