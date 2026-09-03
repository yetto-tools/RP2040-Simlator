#include "simulator.h"

#include "loaders/uf2_loader.h"

namespace rp2040 {

namespace {

bool has_suffix(const std::string& s, const char* suffix) {
    const std::string suf = suffix;
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// RP2040 boot ROM requirement: every flash image begins with a stage-2
// bootloader of exactly this size (checksum included) - not configurable.
constexpr std::uint32_t kBoot2Size = 256u;

}  // namespace

Simulator::Simulator() {
    scs_.attach(mem_);
    sio_.attach(mem_);
    iobank_.attach(mem_);
    timer_.attach(mem_);
    dma_.attach(mem_);
    adc_.attach(mem_);
    uart0_.attach(mem_);
    uart1_.attach(mem_);
    spi0_.attach(mem_);
    spi1_.attach(mem_);
    pwm_.attach(mem_);
    i2c0_.attach(mem_);
    i2c1_.attach(mem_);
    resets_.attach(mem_);
    sysinfo_.attach(mem_);
    usb_.attach(mem_);
    syscfg_.attach(mem_);
    busctrl_.attach(mem_);
    psm_.attach(mem_);
    vreg_.attach(mem_);
    tbman_.attach(mem_);
    io_qspi_.attach(mem_);
    pads_qspi_.attach(mem_);
    watchdog_.attach(mem_);
    pads_.attach(mem_);
    rtc_.attach(mem_);
    xosc_.attach(mem_);
    rosc_.attach(mem_);
    pll_sys_.attach(mem_);
    pll_usb_.attach(mem_);
    clocks_.attach(mem_);
    watchdog_.on_reset([this](bool reset_core1) {
        cpu_.reset();
        if (reset_core1) {
            cpu1_.reset();
            core1_running_ = false;
        }
    });
    watchdog_.set_wdsel_provider([this] { return psm_.reg_read(0x08u, BusWidth::Word).value; });
    scs_.on_system_reset([this] {
        cpu_.reset();
        cpu1_.reset();
        core1_running_ = false;
    });
    pio0_regs_.attach(mem_);
    pio1_regs_.attach(mem_);
    pio0_regs_.connect_nvic(&cpu_, PioRegisters::kPio0Irq0);
    pio1_regs_.connect_nvic(&cpu_, PioRegisters::kPio1Irq0);

    cpu_.set_sev_target(&cpu1_);
    cpu1_.set_sev_target(&cpu_);

    iobank_.connect_cores(&cpu_, &cpu1_);

    // Every peripheral IRQ is wired to both cores; each core's NVIC decides.
    scs_.connect_core1(&cpu1_);
    timer_.connect_core1(&cpu1_);
    dma_.connect_core1(&cpu1_);
    adc_.connect_core1(&cpu1_);
    uart0_.connect_core1(&cpu1_);
    uart1_.connect_core1(&cpu1_);
    spi0_.connect_core1(&cpu1_);
    spi1_.connect_core1(&cpu1_);
    pwm_.connect_core1(&cpu1_);
    i2c0_.connect_core1(&cpu1_);
    i2c1_.connect_core1(&cpu1_);
    rtc_.connect_core1(&cpu1_);
    usb_.connect_core1(&cpu1_);
    pio0_regs_.connect_core1(&cpu1_);
    pio1_regs_.connect_core1(&cpu1_);

    // Real per-peripheral DREQ sources (datasheet 2.5.3.1 Table 119), so DMA
    // pacing against these peripherals reflects their own real FIFO state
    // (e.g. a UART TX DMA channel is only as fast as the UART's own
    // baud-paced draining) instead of the generic dreq_divisor() fallback.
    dma_.set_dreq_source(16, [this] { return spi0_.tx_dreq_ready(); });
    dma_.set_dreq_source(17, [this] { return spi0_.rx_dreq_ready(); });
    dma_.set_dreq_source(18, [this] { return spi1_.tx_dreq_ready(); });
    dma_.set_dreq_source(19, [this] { return spi1_.rx_dreq_ready(); });
    dma_.set_dreq_source(20, [this] { return uart0_.tx_dreq_ready(); });
    dma_.set_dreq_source(21, [this] { return uart0_.rx_dreq_ready(); });
    dma_.set_dreq_source(22, [this] { return uart1_.tx_dreq_ready(); });
    dma_.set_dreq_source(23, [this] { return uart1_.rx_dreq_ready(); });
    dma_.set_dreq_source(32, [this] { return i2c0_.tx_dreq_ready(); });
    dma_.set_dreq_source(33, [this] { return i2c0_.rx_dreq_ready(); });
    dma_.set_dreq_source(34, [this] { return i2c1_.tx_dreq_ready(); });
    dma_.set_dreq_source(35, [this] { return i2c1_.rx_dreq_ready(); });
    dma_.set_dreq_source(36, [this] { return adc_.dreq_ready(); });

    sio_.connect_cores(&cpu_, &cpu1_);
    sio_.on_core1_launch([this](std::uint32_t vtor, std::uint32_t sp, std::uint32_t entry) {
        regs1_.reset();
        cpu1_.set_vtor(vtor);
        regs1_.set_msp(sp);
        regs1_.set_thumb((entry & 1u) != 0);
        regs1_.set_pc(entry & ~std::uint32_t{1});
        core1_running_ = true;
    });
}

ElfImage Simulator::load(const std::string& path, bool from_entry) {
    if (has_suffix(path, ".uf2")) {
        const Uf2Image u = load_uf2_file(mem_, path);
        ElfImage img;
        img.ok = u.ok;
        img.error = u.error;
        img.segments_loaded = u.blocks_loaded;
        img.lowest_addr = u.lowest_addr;
        img.highest_addr = u.highest_addr;
        img.entry = 0;  // UF2 carries no entry point
        if (!img.ok) return img;
        // A UF2 is a flash image: reset runs through its vector table. Real
        // RP2040 silicon always executes a mandatory 256-byte stage-2
        // bootloader (the boot ROM validates and runs it before anything
        // else - pico-sdk's boot2_*.S sources are all exactly this size,
        // ending in a CRC32) immediately ahead of that vector table; skip
        // it here the same way, or every flash image boots 256 bytes into
        // its own boot2 stub instead of the app's actual reset handler. A
        // RAM-resident image (loaded straight into SRAM, which the boot ROM
        // never validates) carries no such stub, so only apply this to
        // images that actually target flash.
        std::uint32_t vtor = img.lowest_addr;
        if (Memory::kFlash.contains(vtor)) vtor += kBoot2Size;
        cpu_.set_vtor(vtor);
        cpu_.reset();
        return img;
    }

    const ElfImage img = load_elf_file(mem_, path);
    if (!img.ok) return img;

    if (from_entry) {
        regs_.reset();
        regs_.set_msp(kSramBase + kSramSize);
        regs_.set_thumb((img.entry & 1u) != 0);
        regs_.set_pc(img.entry & ~std::uint32_t{1});
    } else {
        // The vector table doesn't have to start at the lowest loaded
        // address: a flash image can have a boot-stage stub segment before
        // it (e.g. the RP2040 SDK's 256-byte boot2), which real hardware's
        // boot ROM consumes before ever reaching this vector table itself.
        // Prefer the conventional linker-provided symbol for it when
        // present; only fall back to lowest_addr for images without one
        // (e.g. this project's own hand-written freestanding test fixtures).
        std::uint32_t vtor = img.lowest_addr;
        for (const char* name : {"__vectors", "__VECTOR_TABLE", "__Vectors"}) {
            if (const ElfSymbol* sym = img.symbol_named(name)) {
                vtor = sym->value & ~std::uint32_t{1};
                break;
            }
        }
        cpu_.set_vtor(vtor);
        cpu_.reset();
    }
    return img;
}

void Simulator::sync_clock_pacing() {
    const std::uint32_t sig = clock_tree_.signature();
    if (sig == clock_sig_) return;
    clock_sig_ = sig;

    const std::uint32_t us_cyc = clock_tree_.timer_us_cycles();
    timer_.set_cycles_per_us(us_cyc);
    watchdog_.set_cycles_per_us(us_cyc);

    const auto sys_hz = static_cast<std::uint32_t>(clock_tree_.clk_sys_hz());
    adc_.set_clock_hz(static_cast<std::uint32_t>(clock_tree_.clk_adc_hz()), sys_hz);
    rtc_.set_clock_hz(static_cast<std::uint32_t>(clock_tree_.clk_rtc_hz()), sys_hz);
    const auto peri_hz = static_cast<std::uint32_t>(clock_tree_.clk_peri_hz());
    uart0_.set_clock_hz(peri_hz, sys_hz);
    uart1_.set_clock_hz(peri_hz, sys_hz);
    spi0_.set_clock_hz(peri_hz, sys_hz);
    spi1_.set_clock_hz(peri_hz, sys_hz);
    i2c0_.set_clock_hz(sys_hz, sys_hz);  // ic_clk == clk_sys on the RP2040
    i2c1_.set_clock_hz(sys_hz, sys_hz);
}

ExecStatus Simulator::step() {
    sync_clock_pacing();
    sio_.set_active_core(0);
    scs_.set_active_core(0);
    const std::uint64_t before = cpu_.cycle_count();
    const ExecStatus status = cpu_.step();
    const std::uint64_t spent = cpu_.cycle_count() - before;

    if (core1_running_) {
        sio_.set_active_core(1);
        scs_.set_active_core(1);
        cpu1_.step();
        sio_.set_active_core(0);
        scs_.set_active_core(0);
    }
    for (std::uint64_t i = 0; i < spent; ++i) {
        pio0_.tick();
        pio1_.tick();
        // Latched per cycle, not after the loop: a stall on a non-final cycle
        // of a multi-cycle CPU instruction would otherwise be lost when
        // PioBlock overwrites its last_outcome() on the next tick().
        pio0_regs_.poll_fdebug();
        pio1_regs_.poll_fdebug();
    }
    sio_.on_cycles(spent);
    timer_.on_cycles(spent);
    dma_.on_cycles(spent);
    adc_.on_cycles(spent);
    uart0_.on_cycles(spent);
    uart1_.on_cycles(spent);
    spi0_.on_cycles(spent);
    spi1_.on_cycles(spent);
    i2c0_.on_cycles(spent);
    i2c1_.on_cycles(spent);
    pwm_.on_cycles(spent);
    watchdog_.on_cycles(spent);
    rtc_.on_cycles(spent);
    pio0_regs_.poll_interrupts();
    pio1_regs_.poll_interrupts();
    iobank_.poll();
    return status;
}

Simulator::RunResult Simulator::run(std::uint64_t max_instructions) {
    RunResult r;
    for (; r.instructions < max_instructions; ++r.instructions) {
        const std::uint32_t pc = regs_.pc();
        r.status = step();
        // A WFI/WFE sleep keeps time advancing so a peripheral interrupt can
        // still wake the core; it is not a stop condition.
        if (r.status == ExecStatus::WaitingForInterrupt) continue;
        if (r.status == ExecStatus::Ok || r.status == ExecStatus::ExceptionTaken) {
            if (r.status == ExecStatus::Ok && regs_.pc() == pc) {
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
    r.cycles = cpu_.cycle_count();
    return r;
}

std::string Simulator::status_line() const {
    return "rp2040-sim " + std::string(version_string()) +
           " | pc=" + std::to_string(regs_.pc()) +
           " | cycles=" + std::to_string(cpu_.cycle_count());
}

const char* version_string() { return "0.1.0"; }

}  // namespace rp2040
