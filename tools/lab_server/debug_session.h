// debug_session.h - thin, JSON-friendly debug engine wrapping Simulator for
// the local web "virtual lab" (rp2040-lab-server).
//
// The run/step/breakpoint logic here is a near-copy of the proven pattern
// in src/debuggers/gdb_stub.cpp's run(): step the CPU, check ExecStatus,
// check the PC breakpoint set, check Memory's watchpoint latch. The only
// genuinely new piece is running that loop on a background thread (so the
// HTTP server can keep answering /state, /pause, /gpio, ... while firmware
// is executing) instead of blocking one RSP request until a stop.
#ifndef RP2040LAB_DEBUG_SESSION_H
#define RP2040LAB_DEBUG_SESSION_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "peripherals/ili9341.h"
#include "peripherals/ssd1306.h"
#include "peripherals/st7789.h"
#include "simulator.h"

namespace rp2040lab {

enum class RunStatus { Idle, Running, Halted, Breakpoint, Fault };

const char* to_string(RunStatus s);

struct PinState {
    unsigned pin = 0;
    bool level = false;
    bool driving = false;   // pad actively driving -> "output"; else "input"
    std::uint8_t funcsel = 0;
};

struct StateSnapshot {
    std::uint32_t pc = 0;
    std::array<std::uint32_t, 13> r{};  // r0..r12
    std::uint32_t sp = 0, lr = 0, xpsr = 0;
    std::uint64_t cycles = 0;
    RunStatus status = RunStatus::Idle;
    std::string fault_reason;
    std::vector<PinState> gpio;
    std::string uart0_out;  // drained since the last snapshot()
    std::string uart1_out;
    bool loaded = false;
};

class DebugSession {
public:
    explicit DebugSession(unsigned core = 0);
    ~DebugSession();

    DebugSession(const DebugSession&) = delete;
    DebugSession& operator=(const DebugSession&) = delete;

    // Stops any run in progress, replaces the Simulator with a fresh one
    // (full power-cycle - no stale peripheral state from a previous
    // firmware image), writes `bytes` to a temp file named by `kind`
    // ("elf" or "uf2") and loads it. `from_entry` only applies to ELF
    // (jump straight to e_entry with SP at the top of SRAM, matching
    // tests/fixtures/sum.c's freestanding model - the default for firmware
    // that came from /compile). Uploaded images built with a real vector
    // table (e.g. pico-sdk) should pass from_entry=false to reset through
    // it normally. Returns false and fills `error` on failure.
    bool load(const std::vector<std::uint8_t>& bytes, const std::string& kind,
              bool from_entry, std::string& error);

    void start_run();   // begin/resume background continuous execution
    void pause();        // request the run loop stop; blocks until it has
    void step();          // one instruction, synchronous (only when not running)

    void add_breakpoint(std::uint32_t addr);
    void remove_breakpoint(std::uint32_t addr);
    std::vector<std::uint32_t> breakpoints() const;

    void set_gpio_external(unsigned pin, bool level);
    void clear_gpio_external(unsigned pin);
    // Sets the ADC's test-bench input directly (Adc::set_input) - a
    // potentiometer/analog sensor node's raw 12-bit reading. `channel` is
    // 0-4 (Adc::kNumInputs: 4 GPIO-backed + the temperature sensor).
    void set_adc_external(unsigned channel, std::uint16_t raw12);
    void feed_uart(unsigned n, const std::string& text);

    // Wires a virtual ST7789 TFT (st7789.h - a circuit-editor device, not
    // an RP2040 peripheral) to spi(spi_n)'s on_transfer hook, at whichever
    // GPIOs the circuit's wiring says are CS/DC. Survives a later load()
    // (re-attached against the fresh Simulator each time - see
    // reattach_st7789_locked()) so recompiling/reloading firmware doesn't
    // silently disconnect the display. Returns false and fills `error` on
    // an out-of-range spi/pin argument.
    bool attach_st7789(unsigned spi_n, unsigned cs_pin, unsigned dc_pin, std::string& error);
    void detach_st7789();
    // Packed big-endian RGB565, row-major, kWidth*kHeight*2 bytes - empty
    // if nothing is attached.
    std::vector<std::uint8_t> st7789_framebuffer() const;

    // Same idea as attach_st7789, for the ILI9341 (ili9341.h - a thin
    // St7789 specialization, see reattach_ili9341_locked()). A separate
    // attach slot from ST7789's: both can be wired at once (different SPI
    // instances), but two of the *same* type would still steal one slot.
    bool attach_ili9341(unsigned spi_n, unsigned cs_pin, unsigned dc_pin, std::string& error);
    void detach_ili9341();
    std::vector<std::uint8_t> ili9341_framebuffer() const;

    // Wires a virtual SSD1306 OLED (ssd1306.h) to i2c(i2c_n)'s slave slot at
    // `addr7`, via I2c::set_slave() + I2c::on_stop() (the STOP hook the
    // control-byte framing needs - see ssd1306.h). No GPIO pins are needed
    // here (unlike ST7789's bit-banged CS/DC): I2C addressing already
    // selects the device, and SDA/SCL only matter to the frontend for
    // picking which I2C instance's wiring resolved. Same reattach-on-load()
    // survival as ST7789 - see reattach_ssd1306_locked().
    bool attach_ssd1306(unsigned i2c_n, std::uint8_t addr7, std::string& error);
    void detach_ssd1306();
    // Page-major 1bpp GDDRAM bytes, kWidth*(kHeight/8) - empty if nothing is
    // attached. See ssd1306.h's gddram() for the exact bit layout.
    std::vector<std::uint8_t> ssd1306_gddram() const;

    StateSnapshot snapshot();

private:
    void run_loop();
    // Caller must hold mu_. Returns the outcome of one Cpu step, updating
    // status_/fault_reason_ if it should stop the run.
    bool step_locked();  // true = keep going, false = stop (status_ is set)
    // Caller must hold mu_. (Re)creates st7789_ against the current sim_
    // and hooks it into spi(st7789_spi_) - a no-op if nothing is attached
    // (st7789_spi_ < 0).
    void reattach_st7789_locked();
    void reattach_ili9341_locked();
    // Same idea as reattach_st7789_locked(), for the SSD1306's I2c slave slot.
    void reattach_ssd1306_locked();

    unsigned core_;
    mutable std::mutex mu_;
    std::unique_ptr<rp2040::Simulator> sim_;
    std::set<std::uint32_t> breakpoints_;

    std::unique_ptr<rp2040::St7789> st7789_;
    int st7789_spi_ = -1;  // -1 = not attached
    unsigned st7789_cs_ = 0, st7789_dc_ = 0;

    std::unique_ptr<rp2040::Ili9341> ili9341_;
    int ili9341_spi_ = -1;  // -1 = not attached
    unsigned ili9341_cs_ = 0, ili9341_dc_ = 0;

    std::unique_ptr<rp2040::Ssd1306> ssd1306_;
    int ssd1306_i2c_ = -1;  // -1 = not attached
    std::uint8_t ssd1306_addr_ = 0x3C;

    std::atomic<bool> running_{false};
    std::atomic<bool> pause_requested_{false};
    std::thread run_thread_;

    RunStatus status_ = RunStatus::Idle;
    std::string fault_reason_;
    std::string uart0_buf_, uart1_buf_;
    bool loaded_ = false;
};

}  // namespace rp2040lab

#endif  // RP2040LAB_DEBUG_SESSION_H
