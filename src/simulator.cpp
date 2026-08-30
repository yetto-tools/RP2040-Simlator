#include "simulator.h"

#include "loaders/uf2_loader.h"

namespace rp2040 {

namespace {

bool has_suffix(const std::string& s, const char* suffix) {
    const std::string suf = suffix;
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

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
    watchdog_.attach(mem_);
    pads_.attach(mem_);
    rtc_.attach(mem_);
    xosc_.attach(mem_);
    rosc_.attach(mem_);
    pll_sys_.attach(mem_);
    pll_usb_.attach(mem_);
    clocks_.attach(mem_);
    watchdog_.on_reset([this] { cpu_.reset(); });
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
    pio0_regs_.connect_core1(&cpu1_);
    pio1_regs_.connect_core1(&cpu1_);

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
        // A UF2 is a flash image: reset always runs through its vector table.
        cpu_.set_vtor(img.lowest_addr);
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
        cpu_.set_vtor(img.lowest_addr);
        cpu_.reset();
    }
    return img;
}

ExecStatus Simulator::step() {
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
    }
    timer_.on_cycles(spent);
    dma_.on_cycles(spent);
    adc_.on_cycles(spent);
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
