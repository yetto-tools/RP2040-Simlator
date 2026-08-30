// simulator.h - top-level RP2040 machine: the Cortex-M0+ core plus the
// peripherals it drives, advanced together one instruction at a time.
//
// Each retired CPU instruction costs a number of core cycles (Cortex-M0+
// timings); the PIO blocks are ticked that many system clocks so PIO stays
// in step with the CPU.
#ifndef RP2040_SIMULATOR_H
#define RP2040_SIMULATOR_H

#include <cstdint>
#include <string>

#include "core/cpu.h"
#include "core/memory.h"
#include "core/registers.h"
#include "core/scs.h"
#include "loaders/elf_loader.h"
#include "peripherals/adc.h"
#include "peripherals/dma.h"
#include "peripherals/gpio.h"
#include "peripherals/iobank0.h"
#include "peripherals/pwm.h"
#include "peripherals/sio.h"
#include "peripherals/spi.h"
#include "peripherals/timer.h"
#include "peripherals/uart.h"
#include "pio/pio_block.h"
#include "pio/pio_registers.h"
#include "rp2040.h"

namespace rp2040 {

class Simulator {
public:
    Simulator();

    // Load an ELF image, point VTOR at its lowest loaded address and reset.
    // With `from_entry`, instead jump straight to e_entry with SP at top of SRAM.
    ElfImage load(const std::string& path, bool from_entry = false);

    void reset() { cpu_.reset(); }

    struct RunResult {
        ExecStatus status = ExecStatus::Ok;
        std::uint64_t instructions = 0;
        std::uint64_t cycles = 0;
        std::uint32_t stopped_at = 0;
        bool hit_cap = false;
        bool self_branch = false;
    };

    // Execute one instruction and tick the PIO blocks by the cycles it cost.
    ExecStatus step();

    // step() until a self-branch, a non-Ok status, or `max_instructions`.
    RunResult run(std::uint64_t max_instructions = 10'000'000);

    std::uint64_t cycle_count() const { return cpu_.cycle_count(); }
    std::string status_line() const;

    RegisterFile& regs() { return regs_; }
    Memory& memory() { return mem_; }
    Cpu& cpu() { return cpu_; }
    Gpio& gpio() { return gpio_; }
    Timer& timer() { return timer_; }
    Dma& dma() { return dma_; }
    Adc& adc() { return adc_; }
    Uart& uart(unsigned n) { return n == 0 ? uart0_ : uart1_; }
    Spi& spi(unsigned n) { return n == 0 ? spi0_ : spi1_; }
    Pwm& pwm() { return pwm_; }
    PioBlock& pio(unsigned block) { return block == 0 ? pio0_ : pio1_; }

private:
    RegisterFile regs_;
    Memory mem_;
    Cpu cpu_{regs_, mem_};
    Gpio gpio_;
    Scs scs_{cpu_};
    Sio sio_{gpio_};
    IoBank0 iobank_{gpio_};
    Timer timer_{cpu_};
    Dma dma_{cpu_, mem_};
    Adc adc_{cpu_};
    Uart uart0_{cpu_, Uart::kUart0Base, Uart::kUart0Irq};
    Uart uart1_{cpu_, Uart::kUart1Base, Uart::kUart1Irq};
    Spi spi0_{cpu_, Spi::kSpi0Base, Spi::kSpi0Irq};
    Spi spi1_{cpu_, Spi::kSpi1Base, Spi::kSpi1Irq};
    Pwm pwm_{cpu_, gpio_};
    PioBlock pio0_{gpio_, 0};
    PioBlock pio1_{gpio_, 1};
    PioRegisters pio0_regs_{pio0_, PioRegisters::kPio0Base};
    PioRegisters pio1_regs_{pio1_, PioRegisters::kPio1Base};
};

// Semantic version of the simulator core.
const char* version_string();

}  // namespace rp2040

#endif  // RP2040_SIMULATOR_H
