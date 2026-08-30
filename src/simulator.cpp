#include "simulator.h"

namespace rp2040 {

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
    watchdog_.attach(mem_);
    pads_.attach(mem_);
    rtc_.attach(mem_);
    xosc_.attach(mem_);
    pll_sys_.attach(mem_);
    pll_usb_.attach(mem_);
    clocks_.attach(mem_);
    watchdog_.on_reset([this] { cpu_.reset(); });
    pio0_regs_.attach(mem_);
    pio1_regs_.attach(mem_);
    pio0_regs_.connect_nvic(&cpu_, PioRegisters::kPio0Irq0);
    pio1_regs_.connect_nvic(&cpu_, PioRegisters::kPio1Irq0);
}

ElfImage Simulator::load(const std::string& path, bool from_entry) {
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
    const std::uint64_t before = cpu_.cycle_count();
    const ExecStatus status = cpu_.step();
    const std::uint64_t spent = cpu_.cycle_count() - before;
    for (std::uint64_t i = 0; i < spent; ++i) {
        pio0_.tick();
        pio1_.tick();
    }
    timer_.on_cycles(spent);
    adc_.on_cycles(spent);
    pwm_.on_cycles(spent);
    watchdog_.on_cycles(spent);
    rtc_.on_cycles(spent);
    pio0_regs_.poll_interrupts();
    pio1_regs_.poll_interrupts();
    return status;
}

Simulator::RunResult Simulator::run(std::uint64_t max_instructions) {
    RunResult r;
    for (; r.instructions < max_instructions; ++r.instructions) {
        const std::uint32_t pc = regs_.pc();
        r.status = step();
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
