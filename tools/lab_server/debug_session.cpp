#include "debug_session.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/cpu.h"
#include "peripherals/gpio.h"
#include "peripherals/i2c.h"
#include "peripherals/uart.h"

namespace rp2040lab {

namespace fs = std::filesystem;
using rp2040::Cpu;
using rp2040::ExecStatus;
using rp2040::Gpio;
using rp2040::Simulator;

const char* to_string(RunStatus s) {
    switch (s) {
        case RunStatus::Idle:       return "idle";
        case RunStatus::Running:    return "running";
        case RunStatus::Halted:     return "halted";
        case RunStatus::Breakpoint: return "breakpoint";
        case RunStatus::Fault:      return "fault";
    }
    return "?";
}

DebugSession::DebugSession(unsigned core) : core_(core), sim_(std::make_unique<Simulator>()) {}

DebugSession::~DebugSession() {
    pause_requested_ = true;
    if (run_thread_.joinable()) run_thread_.join();
}

bool DebugSession::load(const std::vector<std::uint8_t>& bytes, const std::string& kind,
                         bool from_entry, std::string& error) {
    pause();  // stop any run in progress before replacing the Simulator

    std::lock_guard<std::mutex> lk(mu_);

    const fs::path dir = fs::temp_directory_path();
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = dir / ("rp2040lab_load_" + std::to_string(id) + "." + kind);

    {
        std::ofstream f(path, std::ios::binary);
        if (!f) { error = "could not create temp file for the image"; return false; }
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    sim_ = std::make_unique<Simulator>();
    uart0_buf_.clear();
    uart1_buf_.clear();
    sim_->uart(0).on_transmit([this](std::uint8_t b) { uart0_buf_.push_back(static_cast<char>(b)); });
    sim_->uart(1).on_transmit([this](std::uint8_t b) { uart1_buf_.push_back(static_cast<char>(b)); });
    reattach_st7789_locked();
    reattach_ili9341_locked();
    reattach_ssd1306_locked();

    const rp2040::ElfImage img = sim_->load(path.string(), from_entry);

    std::error_code ec;
    fs::remove(path, ec);

    if (!img.ok) {
        error = img.error.empty() ? "failed to load the image" : img.error;
        loaded_ = false;
        status_ = RunStatus::Idle;
        return false;
    }

    loaded_ = true;
    status_ = RunStatus::Halted;
    fault_reason_.clear();
    breakpoints_.clear();
    return true;
}

bool DebugSession::step_locked() {
    const ExecStatus st = sim_->step();
    if (st == ExecStatus::Lockup) {
        status_ = RunStatus::Fault;
        fault_reason_ = "lockup (unhandled fault)";
        return false;
    }

    // A `bkpt` instruction actually present in the executed code (this
    // session's breakpoints are address-matched below, not memory-patched -
    // see gdb_stub.cpp's own comment on the same choice), e.g. pico-sdk's
    // default "unhandled interrupt" ISR stubs. Real Cortex-M0+ hardware
    // would fault here (no attached debugger); halting instead of silently
    // continuing past it is the closest safe equivalent without modelling
    // that fault escalation in the core CPU.
    if (st == ExecStatus::Breakpoint) {
        status_ = RunStatus::Breakpoint;
        fault_reason_.clear();
        return false;
    }

    std::uint32_t wp_addr = 0;
    bool wp_write = false;
    if (sim_->memory().take_watchpoint_hit(wp_addr, wp_write)) {
        status_ = RunStatus::Breakpoint;
        fault_reason_.clear();
        return false;
    }

    if (breakpoints_.count(sim_->regs(core_).pc()) != 0) {
        status_ = RunStatus::Breakpoint;
        fault_reason_.clear();
        return false;
    }

    return true;
}

void DebugSession::run_loop() {
    constexpr int kBatchSize = 2000;
    while (running_.load()) {
        if (pause_requested_.load()) break;

        std::lock_guard<std::mutex> lk(mu_);
        for (int i = 0; i < kBatchSize; ++i) {
            if (!step_locked()) {
                running_ = false;
                return;
            }
            if (pause_requested_.load()) break;
        }
    }
    std::lock_guard<std::mutex> lk(mu_);
    if (status_ == RunStatus::Running) status_ = RunStatus::Halted;
}

void DebugSession::start_run() {
    pause();  // make sure any previous run thread is joined first
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!loaded_) return;
        status_ = RunStatus::Running;
    }
    pause_requested_ = false;
    running_ = true;
    run_thread_ = std::thread(&DebugSession::run_loop, this);
}

void DebugSession::pause() {
    pause_requested_ = true;
    if (run_thread_.joinable()) run_thread_.join();
    running_ = false;
}

void DebugSession::step() {
    pause();  // no-op if nothing was running
    std::lock_guard<std::mutex> lk(mu_);
    if (!loaded_) return;
    if (!step_locked()) return;
    status_ = RunStatus::Halted;
}

void DebugSession::add_breakpoint(std::uint32_t addr) {
    std::lock_guard<std::mutex> lk(mu_);
    breakpoints_.insert(addr);
}

void DebugSession::remove_breakpoint(std::uint32_t addr) {
    std::lock_guard<std::mutex> lk(mu_);
    breakpoints_.erase(addr);
}

std::vector<std::uint32_t> DebugSession::breakpoints() const {
    std::lock_guard<std::mutex> lk(mu_);
    return {breakpoints_.begin(), breakpoints_.end()};
}

void DebugSession::set_gpio_external(unsigned pin, bool level) {
    std::lock_guard<std::mutex> lk(mu_);
    sim_->gpio().set_external(pin, level);
}

void DebugSession::clear_gpio_external(unsigned pin) {
    std::lock_guard<std::mutex> lk(mu_);
    sim_->gpio().clear_external(pin);
}

void DebugSession::set_adc_external(unsigned channel, std::uint16_t raw12) {
    std::lock_guard<std::mutex> lk(mu_);
    sim_->adc().set_input(channel, raw12);
}

void DebugSession::feed_uart(unsigned n, const std::string& text) {
    std::lock_guard<std::mutex> lk(mu_);
    sim_->uart(n).feed(text);
}

void DebugSession::reattach_st7789_locked() {
    if (st7789_spi_ < 0) return;
    st7789_ = std::make_unique<rp2040::St7789>(sim_->gpio(), st7789_cs_, st7789_dc_);
    rp2040::St7789* tft = st7789_.get();
    sim_->spi(static_cast<unsigned>(st7789_spi_)).on_transfer([tft](std::uint8_t b) { return tft->on_transfer(b); });
}

bool DebugSession::attach_st7789(unsigned spi_n, unsigned cs_pin, unsigned dc_pin, std::string& error) {
    if (spi_n > 1) { error = "spi must be 0 or 1"; return false; }
    if (cs_pin >= static_cast<unsigned>(Gpio::kNumPins) || dc_pin >= static_cast<unsigned>(Gpio::kNumPins)) {
        error = "cs/dc must be a valid GPIO number";
        return false;
    }
    std::lock_guard<std::mutex> lk(mu_);
    st7789_spi_ = static_cast<int>(spi_n);
    st7789_cs_ = cs_pin;
    st7789_dc_ = dc_pin;
    reattach_st7789_locked();
    return true;
}

void DebugSession::detach_st7789() {
    std::lock_guard<std::mutex> lk(mu_);
    if (st7789_spi_ >= 0) sim_->spi(static_cast<unsigned>(st7789_spi_)).on_transfer(nullptr);
    st7789_.reset();
    st7789_spi_ = -1;
}

std::vector<std::uint8_t> DebugSession::st7789_framebuffer() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!st7789_) return {};
    const auto& fb = st7789_->framebuffer();
    std::vector<std::uint8_t> out;
    out.reserve(fb.size() * 2);
    for (const std::uint16_t px : fb) {
        out.push_back(static_cast<std::uint8_t>(px >> 8));
        out.push_back(static_cast<std::uint8_t>(px));
    }
    return out;
}

void DebugSession::reattach_ili9341_locked() {
    if (ili9341_spi_ < 0) return;
    ili9341_ = std::make_unique<rp2040::Ili9341>(sim_->gpio(), ili9341_cs_, ili9341_dc_);
    rp2040::Ili9341* tft = ili9341_.get();
    sim_->spi(static_cast<unsigned>(ili9341_spi_)).on_transfer([tft](std::uint8_t b) { return tft->on_transfer(b); });
}

bool DebugSession::attach_ili9341(unsigned spi_n, unsigned cs_pin, unsigned dc_pin, std::string& error) {
    if (spi_n > 1) { error = "spi must be 0 or 1"; return false; }
    if (cs_pin >= static_cast<unsigned>(Gpio::kNumPins) || dc_pin >= static_cast<unsigned>(Gpio::kNumPins)) {
        error = "cs/dc must be a valid GPIO number";
        return false;
    }
    std::lock_guard<std::mutex> lk(mu_);
    ili9341_spi_ = static_cast<int>(spi_n);
    ili9341_cs_ = cs_pin;
    ili9341_dc_ = dc_pin;
    reattach_ili9341_locked();
    return true;
}

void DebugSession::detach_ili9341() {
    std::lock_guard<std::mutex> lk(mu_);
    if (ili9341_spi_ >= 0) sim_->spi(static_cast<unsigned>(ili9341_spi_)).on_transfer(nullptr);
    ili9341_.reset();
    ili9341_spi_ = -1;
}

std::vector<std::uint8_t> DebugSession::ili9341_framebuffer() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!ili9341_) return {};
    const auto& fb = ili9341_->framebuffer();
    std::vector<std::uint8_t> out;
    out.reserve(fb.size() * 2);
    for (const std::uint16_t px : fb) {
        out.push_back(static_cast<std::uint8_t>(px >> 8));
        out.push_back(static_cast<std::uint8_t>(px));
    }
    return out;
}

void DebugSession::reattach_ssd1306_locked() {
    if (ssd1306_i2c_ < 0) return;
    ssd1306_ = std::make_unique<rp2040::Ssd1306>();
    rp2040::Ssd1306* oled = ssd1306_.get();
    rp2040::I2c& bus = sim_->i2c(static_cast<unsigned>(ssd1306_i2c_));
    bus.set_slave(ssd1306_addr_, [oled](bool is_read, std::uint8_t& b) { return oled->on_transfer(is_read, b); });
    bus.on_stop([oled]() { oled->on_stop(); });
}

bool DebugSession::attach_ssd1306(unsigned i2c_n, std::uint8_t addr7, std::string& error) {
    if (i2c_n > 1) { error = "i2c must be 0 or 1"; return false; }
    if (addr7 > 0x7F) { error = "addr must be a valid 7-bit I2C address"; return false; }
    std::lock_guard<std::mutex> lk(mu_);
    ssd1306_i2c_ = static_cast<int>(i2c_n);
    ssd1306_addr_ = addr7;
    reattach_ssd1306_locked();
    return true;
}

void DebugSession::detach_ssd1306() {
    std::lock_guard<std::mutex> lk(mu_);
    if (ssd1306_i2c_ >= 0) {
        rp2040::I2c& bus = sim_->i2c(static_cast<unsigned>(ssd1306_i2c_));
        bus.set_slave(0xFF, nullptr);
        bus.on_stop(nullptr);
    }
    ssd1306_.reset();
    ssd1306_i2c_ = -1;
}

std::vector<std::uint8_t> DebugSession::ssd1306_gddram() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!ssd1306_) return {};
    return ssd1306_->gddram();
}

StateSnapshot DebugSession::snapshot() {
    std::lock_guard<std::mutex> lk(mu_);
    StateSnapshot s;
    s.loaded = loaded_;
    s.status = status_;
    s.fault_reason = fault_reason_;
    s.cycles = sim_->cycle_count();

    auto& regs = sim_->regs(core_);
    for (unsigned i = 0; i < 13; ++i) s.r[i] = regs.get(i);
    s.sp = regs.sp();
    s.lr = regs.lr();
    s.pc = regs.pc();
    s.xpsr = regs.xpsr();

    Gpio& gpio = sim_->gpio();
    s.gpio.reserve(Gpio::kNumPins);
    for (unsigned pin = 0; pin < static_cast<unsigned>(Gpio::kNumPins); ++pin) {
        PinState p;
        p.pin = pin;
        p.level = gpio.level(pin);
        p.driving = gpio.pad_driving(pin);
        p.funcsel = gpio.funcsel(pin);
        s.gpio.push_back(p);
    }

    s.uart0_out.swap(uart0_buf_);
    s.uart1_out.swap(uart1_buf_);
    uart0_buf_.clear();
    uart1_buf_.clear();

    return s;
}

}  // namespace rp2040lab
