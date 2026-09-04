# BACKLOG.md - Development Roadmap & Sprint Planning

> This document tracks all features, tasks, and sprints. Updated weekly during development.

---

## Project Overview

| Metric | Value |
|--------|-------|
| **Total Duration** | 12 weeks (Phase 1) |
| **Team Size** | 1 FTE (thesis author) |
| **Estimated LOC** | ~15,000 lines (C++ core) |
| **Test Cases** | 200+ (unit + integration + hardware) |
| **Documentation** | 5 files + inline comments |

---

## Milestones

```
Week 1-2:   PHASE 1 - CPU Core + Memory
└─ M1.1: CPU architecture ready
└─ M1.2: Basic instruction set working
└─ M1.3: Memory subsystem functional

Week 3-5:   PHASE 2 - PIO (CRITICAL)
└─ M2.1: PIO architecture design approved
└─ M2.2: State machine execution engine
└─ M2.3: Complete ISA (all 9 instructions)
└─ M2.4: FIFO & clock divider

Week 6:     PHASE 3 - GPIO + Timer
└─ M3.1: GPIO controller functional
└─ M3.2: Timer/PWM working

Week 7:     PHASE 4 - UART + SPI
└─ M4.1: UART0/UART1 transmit/receive
└─ M4.2: SPI0/SPI1 basic operation

Week 8:     PHASE 5 - ADC + Advanced Interrupts
└─ M5.1: ADC conversion timing
└─ M5.2: Interrupt nesting & priority

Week 9:     PHASE 6 - Clock Manager
└─ M6.1: PLL & clock switching
└─ M6.2: Frequency measurement

Week 10:    PHASE 7 - Loaders & Debugging
└─ M7.1: ELF loader working
└─ M7.2: GDB stub functional
└─ M7.3: PIO debugger

Week 11:    PHASE 8 - Testing & Validation
└─ M8.1: 200+ tests passing
└─ M8.2: Hardware comparison suite
└─ M8.3: Performance profiling

Week 12:    PHASE 9 - Documentation & Release
└─ M9.1: Final documentation
└─ M9.2: Code review & polish
└─ M9.3: Thesis manuscript
```

---

## Feature Backlog

### PHASE 1: Core CPU + Memory (Weeks 1-2)

#### P1.1: ARM Cortex-M0+ CPU Architecture  [DONE]
- [x] Register file (R0-R15, banked MSP/PSP, APSR/IPSR/EPSR, CONTROL, PRIMASK)
- [x] Program counter management (raw PC store, bit-0 masking, advance)
- [x] Condition code logic (N, Z, C, V) + full ARMv6-M ConditionPassed()
- [x] Execution model: `Cpu::step()` is atomic fetch/decode/execute (the M0+
      2-stage pipeline is not micro-modelled; its cycle costs are - see P1.5)
- [ ] Exception vector table - moved to P1.4 (NVIC / exceptions.h)
- **Tests**: 20+ unit tests -> `tests/unit/test_registers.cpp` (17 cases)
- **Effort**: 40 hours
- **Priority**: CRITICAL
- **Design**: ARCHITECTURE.md 1.1-1.2, 1.5 (rewritten for ARMv6-M, not Thumb-2)
- **Files**: `src/core/registers.{h,cpp}`

#### P1.2: Thumb Instruction Decoder + Executor (ARMv6-M)  [DONE]
- [x] Decoder framework: `DecodedInstr`, `Mnemonic`, `is_32bit_thumb()`
- [x] A5.2.1 shift/add/sub/mov/cmp (incl. LSL #0 -> MOVS reg)
- [x] A5.2.2 data processing (16 ops, AND..MVN, incl. RSB/MUL forms)
- [x] A5.2.3 special data + BX/BLX (high-register ADD/CMP/MOV)
- [x] Load/store single: literal, register offset, imm offset (w/h/b), SP-rel
- [x] ADR / ADD(SP+imm); A5.2.5 misc (extend, PUSH/POP, REV*, CPS, BKPT, hints)
- [x] STM/LDM (writeback rules); Bcc/SVC/UDF; B (T2)
- [x] 32-bit: BL (offset reconstruction), MRS, MSR, DSB/DMB/ISB
- [x] Rejects ARMv7-M-only encodings (IT, CBZ/CBNZ, LDM.W) as UNDEFINED
- [x] ALU primitives: `add_with_carry`, `shift_c` (LSL/LSR/ASR/ROR/RRX)
- [x] Execute stage - computational core: data processing, shifts, moves,
      compares, MUL, high-reg ADD/MOV/CMP, ADR/ADD-SUB SP, SXT/UXT, REV*
- [x] Execute stage - branches: B, Bcc, BL, BX, BLX; MRS/MSR/CPS; BKPT/SVC
- [x] `Cpu::step()` fetch-decode-execute loop (runs real loops + subroutines)
- [x] Execute stage - memory: LDR/STR/LDRB/LDRH/LDRSB/LDRSH (literal, reg,
      imm, SP-rel), PUSH/POP (incl. POP{PC}), LDM/STM with writeback rules;
      bus faults -> ExecStatus::MemFault; unaligned word/half -> fault
- **Tests**: test_thumb_decode (219), test_alu (45), test_cpu_exec (137)
- **Effort**: 60 hours
- **Priority**: CRITICAL
- **Dependencies**: P1.1
- **Design**: ARCHITECTURE.md 1.4; ARMv6-M ARM chapters A5, A6, A2.2
- **Files**: `include/thumb_isa.h`, `src/core/{thumb_decode,alu,cpu}.{h,cpp}`

#### P1.3: Memory Subsystem  [DONE]
- [x] ROM (16 KB, read-only) - direct stores fault as WriteToReadOnly
- [x] Flash (2 MB) - read-only to CPU stores; written via backdoor load()
- [x] SRAM (264 KB, read/write) - flat model (bank striping deferred)
- [x] Memory map routing (0x40000000+) - BusPeripheral interface + attach_peripheral()
- [x] Access size handling (byte, half, word) - little-endian; LDRD/STRD N/A on ARMv6-M
- [x] Alignment checking - all unaligned half/word accesses trapped (ARMv6-M)
- [x] Memory faults (misaligned, invalid address) - BusStatus enum, no exceptions
- [x] Backdoor load()/dump() for loaders and tests
- [ ] Atomic set/clear/xor register aliases (+0x1000/+0x2000/+0x3000) - deferred to Phase 3 peripheral base class
- **Tests**: 15+ edge cases -> `tests/unit/test_memory.cpp` (19 cases)
- **Effort**: 20 hours
- **Priority**: CRITICAL
- **Dependencies**: P1.1
- **Design**: ARCHITECTURE.md section 2.3-2.4; DESIGN.md Decisions 6 & 13
- **Files**: `src/core/bus.h`, `src/core/memory.{h,cpp}`, `src/core/bus.cpp`

#### P1.4: Exception model + NVIC  [DONE]
- [x] Vector table lookup via VTOR; `Cpu::reset()` = MSP<-vec[0], PC<-vec[1]
- [x] Exception entry: 8-word frame {R0-R3,R12,LR,ReturnAddr,xPSR}, forced
      8-byte align + xPSR[9] realign flag, EXC_RETURN in LR, IPSR + Handler mode
- [x] Exception return: BX LR / POP{PC} with EXC_RETURN payload -> unstack,
      restore mode/flags/SP (MSP or PSP per EXC_RETURN)
- [x] Priority model: fixed (Reset/NMI/HardFault) + 2-bit configurable
      (SVCall/PendSV/SysTick/IRQ); PRIMASK masking; preemption of running handler
- [x] Synchronous faults: UNDEFINED / bus error -> HardFault (+ lockup on
      escalation); SVC -> SVCall
- [x] `pend_exception` / async delivery between instructions; NVIC per-IRQ
      enable gates external interrupt delivery
- [x] System Control Space peripheral (`src/core/scs.{h,cpp}`) on the PPB
      (0xE000E000): NVIC ISER/ICER/ISPR/ICPR/IPR, SCB CPUID/ICSR/VTOR/AIRCR/
      SHPR2/SHPR3, SysTick CSR/RVR/CVR with a down-counter advanced per cycle
      by `step()`, COUNTFLAG, TICKINT -> kExcSysTick
- [x] Memory decoder now routes the PPB region to peripherals
- [x] WFI/WFE sleep + SEV event model: `Cpu` has an event register and an
      `asleep_` state; WFI sleeps until any pending interrupt, WFE until an
      event, SEV sets the event on both cores, exception entry is a wake event.
      `Simulator::run` keeps time advancing through a sleep so a peripheral IRQ
      can wake it (`ExecStatus::WaitingForInterrupt` is not a stop)
- [x] AIRCR.SYSRESETREQ (VECTKEY-guarded) -> system-reset hook; the Simulator
      resets both cores and stops core1
- [x] SCR.SLEEPONEXIT: core re-enters WFI sleep when it returns to Thread mode
- [ ] SLEEPDEEP (no separate deep-sleep clock model)
- **Tests**: test_exceptions (87), test_scs (81), test_cpu_exec WFI/WFE/SEV
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P1.1, P1.3
- **Design**: ARMv6-M ARM B1.5, B3; ARCHITECTURE.md section 5
- **Files**: `include/exceptions.h`, `src/core/{cpu,scs}.{h,cpp}`

#### P1.6: Dual core  [DONE]
- [x] Second `Cpu` + `RegisterFile` (core1) in the `Simulator`; step() runs
      core0 then core1 round-robin, sharing one `Memory`
- [x] SIO rewritten per-core (`src/peripherals/sio.{h,cpp}`): CPUID follows
      the active core, inter-core mailbox FIFO (2 x 8-deep) -> SIO_IRQ_PROC0/1
      (IRQ15/16), 32 hardware spinlocks
- [x] Core-1 launch: the `0,0,1,vtor,sp,entry` mailbox sequence sets core1's
      VTOR/SP/PC and starts it (bootrom echo emulated)
- [x] Cross-core SEV/WFE event signalling (SEV on one core wakes a WFE sleep
      on the other; see P1.4)
- [x] Per-core NVIC + SysTick: `Scs` models both cores' SCS at 0xE000E000,
      switching banks via `set_active_core()` (like the SIO); every peripheral
      holds an `InterruptController` that fans its IRQ line out to both cores,
      each core's NVIC then decides independently (per-core enable + priority).
      Verified: a TIMER alarm is taken by core1 while core0 (IRQ disabled)
      ignores it.
- [x] SIO integer divider (2.3.1.6): UDIVIDEND/UDIVISOR/SDIVIDEND/SDIVISOR/
      QUOTIENT/REMAINDER/CSR at 0x060-0x078, shared (not banked per core) like
      real hardware. 8 clk_sys-cycle latency paced by `on_cycles()`, CSR.READY
      clear for the duration, CSR.DIRTY clears on a QUOTIENT read (not
      REMAINDER, so context save/restore can preserve it), divide-by-zero
      gives QUOTIENT=0xFFFFFFFF/REMAINDER=DIVIDEND in both sign modes. Found
      missing while debugging a real pico-sdk-style firmware (picoOS) that
      busy-waits on CSR.READY with IRQs masked in its decimal-print routine -
      previously that loop spun forever since bus_read() fell through to its
      `default: return 0` for these offsets.
- [ ] Exact instruction-interleave timing (round-robin is a simplification)
- **Tests**: `tests/unit/test_multicore.cpp` (11 cases)
- **Files**: `src/core/{scs,interrupt_controller}.h`, all `src/peripherals/*`
- **Design**: RP2040 datasheet 2.3, 2.4

#### P1.5: Clock Management (Basic)  [IN PROGRESS]
- [x] Per-instruction cycle counter on `Cpu::step()` (`cycle_count()`)
- [x] Cortex-M0+ instruction timing table (`src/core/timing.{h,cpp}`):
      1-cyc ALU, 2-cyc load/store, 1+N for LDM/STM/PUSH/POP, 4+N POP{PC},
      3-cyc taken branch / BX, 4-cyc BL, single-cycle MUL, 4-cyc MRS/MSR
- [x] Clock-tree pacing: `ClockTree` (`src/peripherals/clock_tree.{h,cpp}`)
      resolves the CLOCKS generators (SRC / AUXSRC muxes + int/frac dividers) +
      the PLLs + XOSC/ROSC into real Hz; `Simulator::step()` pushes the derived
      clk_sys / clk_adc / clk_rtc and the WATCHDOG_TICK-scaled microsecond tick
      into TIMER / WATCHDOG / ADC / RTC whenever the config changes. Falls back
      to the pico-sdk defaults (125 / 48 MHz / 46875 Hz) until `clocks_init`
      writes a generator.
- [ ] Bus wait states (flash XIP latency, SRAM bank contention)
- **Tests**: `tests/unit/test_timing.cpp` (8 cases),
      `tests/unit/test_clock_tree.cpp` (6 cases)
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P1.1
- **Design**: Cortex-M0+ TRM (DDI 0484) Table 3-1; RP2040 datasheet 2.4
- **Files**: `src/core/timing.{h,cpp}`, `Cpu::cycle_count()`

---

### PHASE 2: PIO (Programmable I/O) (Weeks 3-5)

#### P2.1: PIO Block Architecture  [DONE]
- [x] Instruction decoder for all 9 PIO ops (`include/pio_isa.h`,
      `src/pio/pio_decode.cpp`)
- [x] State-machine registers X, Y, OSR, ISR, PC + shift counters
- [x] 4-deep TX/RX FIFO with join support (`src/pio/pio_fifo.h`)
- [x] `StateMachine::tick()` - one instruction per post-divider clock,
      instruction wrapping (WRAP_TOP/BOTTOM), delay field
- [x] `PioBlock`: 4 SMs sharing a 32-word program + 8-bit IRQ register,
      per-SM 16.8 fractional clock divider, round-robin `tick()`
      (`src/pio/pio_block.{h,cpp}`)
- [x] SM wired to `Gpio` (per-block driver) + block IRQ register
- [x] 2 PIO blocks + CPU-facing register block: `Simulator` owns `pio0_`/
      `pio1_` (`PioBlock`) each with its own `PioRegisters` @ 0x50200000 /
      0x50300000, attached to the bus and per-core NVIC (see P2.7)
- **Tests**: test_pio_decode (10), test_pio_sm (18), test_pio_block (7)
- **Effort**: 30 hours
- **Priority**: CRITICAL (40% of Phase 2)
- **Dependencies**: P1.1, P1.3
- **Design**: RP2040 datasheet 3.2-3.5
- **Files**: `include/pio_isa.h`, `src/pio/{pio_decode,state_machine,pio_fifo}`

#### P2.2: PIO ISA - JMP, WAIT, IN, OUT  [DONE]
- [x] JMP: always, !X, X-- (unconditional decrement), !Y, Y--, X!=Y, !OSRE, PIN
- [x] WAIT: GPIO level, PIN (IN_BASE-relative), IRQ (with auto-clear on match)
- [x] IN: X/Y/NULL/ISR/OSR/PINS; left/right shift; ISR accumulation
- [x] OUT: X/Y/NULL/PC/ISR/PINS/PINDIRS; left/right shift from OSR
- [x] side-set (data + optional enable bit), MOV PINS, SET PINS/PINDIRS
- [x] OUT EXEC / MOV EXEC / MOV STATUS: the injected instruction executes via
      a recursive `StateMachine::exec()` call, taking over PC advance and
      (per datasheet 3.4.2) supplying its own delay/side-set in place of the
      OUT/MOV's; MOV STATUS reads TXLEVEL/RXLEVEL vs EXECCTRL.STATUS_N
- **Tests**: test_pio_sm + test_pio_block
- **Effort**: 45 hours
- **Priority**: CRITICAL
- **Dependencies**: P2.1

#### P2.3: PIO ISA - PUSH, PULL, MOV, SET, IRQ  [DONE]
- [x] PUSH: iffull, block/non-block (data-lost on non-block + full), ISR clear
- [x] PULL: ifempty, block/non-block (OSR <- X on non-block + empty), autopull
- [x] autopush / autopull with threshold + mid-instruction stall + resume
- [x] MOV: none / invert / bit-reverse; X/Y/ISR/OSR/NULL/PINS/PC/EXEC; STATUS
      source (TXLEVEL/RXLEVEL vs STATUS_N)
- [x] SET: X, Y, PINS, PINDIRS
- [x] IRQ: set / clear / set+wait (relative-index resolution), block IRQ register
- [x] MOV STATUS + OUT/MOV EXEC (see P2.2)
- **Tests**: test_pio_sm + test_pio_block
- **Effort**: 35 hours

- **Dependencies**: P2.1, P2.2

#### P2.4: FIFO Management  [DONE]
- [x] TX / RX FIFO: 4-deep 32-bit ring, full/empty/level, join to 8-deep
      (`src/pio/pio_fifo.h`)
- [x] Flow control: blocking vs non-blocking PUSH/PULL wired into the SM
- [x] CPU-facing TXF/RXF register windows + FSTAT/FLEVEL (`pio_registers.cpp`;
      TXF/RXF also latch TXOVER/RXUNDER into FDEBUG, see P2.7)
- **Tests**: covered by test_pio_sm
- **Effort**: 20 hours
- **Priority**: CRITICAL
- **Dependencies**: P2.1

#### P2.5: Clock Divider & Execution Timing  [DONE]
- [x] Divide-by-N logic (1-65536): `PioBlock::set_clkdiv()` / `clkacc_`
      (int part 0 means 65536, per datasheet 3.5.5)
- [x] Fractional clock divider: 16.8 fixed-point (int*256 + frac) in
      `PioBlock::tick()`
- [x] Stall detection (when FIFO blocks): `StateMachine::TickOutcome::stalled`
      (see P2.3/P2.4/P2.7)
- [x] Parallel SM execution: `PioBlock::tick()` advances all 4 SMs every
      system clock, each against its own divided clock
- **Tests**: test_pio_block, test_pio_sm
- **Effort**: 15 hours
- **Priority**: CRITICAL (timing is key)
- **Dependencies**: P2.1, P2.2, P2.3

#### P2.6: PIO  GPIO Integration  [DONE]
- [x] OUT / SET / MOV driving GPIO pins, IN reading them (via PINCTRL groups)
- [x] SIDESET (data + optional enable, PINS or PINDIRS)
- [x] JMP PIN / WAIT PIN / WAIT GPIO through the shared Gpio model
- [ ] Input synchroniser bypass, pin override logic (low priority)
- **Tests**: test_pio_block

#### P2.7: PIO  CPU Integration  [DONE]
- [x] `PioRegisters` BusPeripheral @ 0x50200000 / 0x50300000
      (`src/pio/pio_registers.{h,cpp}`)
- [x] CTRL (SM_ENABLE / SM_RESTART / CLKDIV_RESTART)
- [x] TXF0-3 write -> TX FIFO, RXF0-3 read -> RX FIFO; FSTAT, FLEVEL
- [x] INSTR_MEM0-31 program load/read; SMx_INSTR immediate execute
- [x] SMx_CLKDIV / EXECCTRL / SHIFTCTRL / PINCTRL decode into SmConfig;
      SMx_ADDR (PC, read-only); FJOIN
- [x] IRQ register (read / write-1-clear) + IRQ_FORCE
- [x] INTR (RXNEMPTY / TXNFULL / SM-IRQ 0-3) + IRQ0_INTE/INTF/INTS and
      IRQ1_* routed to NVIC PIO0_IRQ_0/1 (IRQ7/8) and PIO1_IRQ_0/1 (IRQ9/10)
      via `poll_interrupts()`, called each `Simulator::step()`
- [x] FDEBUG stall/overflow/underflow bits: RXSTALL/TXSTALL latched per-cycle
      from `StateMachine::tick()`'s outcome (blocking PUSH/PULL or
      autopush/autopull against a full/empty FIFO), RXUNDER/TXOVER latched on
      a CPU-side RXF read of an empty FIFO / TXF write to a full FIFO; all
      four groups write-1-clear
- **Tests**: `tests/unit/test_pio_registers.cpp` (11 cases) - incl. a blink
      program configured entirely through MMIO and an SM-IRQ -> NVIC route
- **Effort**: 20 hours
- **Dependencies**: P2.1-P2.6

#### P2.8: Auto-Push & Auto-Pull  [DONE]
- [x] Auto-push when ISR full: `StateMachine::maybe_autopush()`
- [x] Auto-pull when OSR empty: `StateMachine::do_autopull()`
- [x] Configurable thresholds: `cfg.push_threshold` / `cfg.pull_threshold`
- [x] Edge cases (mid-instruction): `Stall::AutoPush` / `Stall::AutoPull`
      resume the stalled IN/OUT after the FIFO unblocks (see P2.3)
- **Tests**: test_pio_sm ("autopull refills...", "autopull stalls...",
      "autopush moves...")
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P2.3, P2.4

---

### PHASE 3: GPIO + Timer (Week 6)

#### P3.1: GPIO Controller  [DONE - slew/drive-strength deferred]
- [x] 30-pin pad model (`src/peripherals/gpio.{h,cpp}`): FUNCSEL routing
      (SIO / PIO0 / PIO1), per-driver OUT + OE, pad pulls, external stimulus,
      effective pad-driving / pad-level / input-level resolution
- [x] SIO block (`src/peripherals/sio.{h,cpp}`) @ 0xD0000000: CPUID, GPIO_IN,
      GPIO_OUT/OE with SET/CLR/XOR atomic aliases; memory decoder routes SIO
- [x] IO_BANK0 (`src/peripherals/iobank0.{h,cpp}`) @ 0x40014000: GPIOx_CTRL
      FUNCSEL, GPIOx_STATUS level bits
- [x] PADS_BANK0 register window (pulls -> Gpio; drive/schmitt stored) -- see P3.1b
- [x] GPIOx_CTRL OUTOVER / OEOVER / INOVER / IRQOVER (2-bit normal/invert/
      low/high): OUTOVER/OEOVER force the pad; INOVER feeds PIO/SIO inputs
      (`Gpio::func_level`); IRQOVER feeds the IO_BANK0 edge detector
- [ ] Slew / drive strength / glitch filter (behavioural, low priority)
- **Tests**: `tests/unit/test_gpio.cpp` (9 cases)
- **Effort**: 25 hours
- **Priority**: HIGH
- **Dependencies**: P1.3
- **Design**: RP2040 datasheet 2.3.1, 2.19

#### P3.2: GPIO Interrupts  [DONE]
- [x] Per-pin LEVEL_LOW / LEVEL_HIGH / EDGE_LOW / EDGE_HIGH detect in
      `IoBank0::poll()` (called once per `Simulator::step()`); level bits
      track live, edge bits latch until written-1-to-clear via INTR0..3
- [x] Independent PROC0_* and PROC1_* INTE / INTF / INTS register sets
- [x] NVIC integration: PROC0 status -> IO_IRQ_BANK0 (IRQ13) on core 0,
      PROC1 status -> IRQ13 on core 1
- **Tests**: `tests/unit/test_iobank0.cpp` (3 cases: level/edge tracking +
      w1c, per-core routing, INTF force)
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P3.1, P1.4
- **Files**: `src/peripherals/iobank0.{h,cpp}`

#### P3.3a: System TIMER (0x40054000)  [DONE]
- [x] 64-bit microsecond counter; TIMEHW/LW write pair, TIMEHR/LR read pair
      with high-word latching, TIMERAWH/L unlatched
- [x] 4 x 32-bit ALARM: write arms, low-counter match fires + auto-disarms
- [x] ARMED (w1-disarm), INTR (w1c), INTE, INTF, INTS -> TIMER_IRQ_0..3 (NVIC)
- [x] PAUSE; advanced from the system clock via `Timer::on_cycles()` in
      `Simulator::step()`
- **Tests**: `tests/unit/test_timer.cpp` (7 cases)
- **Files**: `src/peripherals/timer.{h,cpp}`
- **Design**: RP2040 datasheet 4.6

#### P3.3: PWM Controller (0x40050000)  [DONE]
- [x] `Pwm` BusPeripheral (`src/peripherals/pwm.{h,cpp}`), 8 slices x 2 channels
- [x] Per-slice CSR/DIV/CTR/CC/TOP; global EN, INTR/INTE/INTF/INTS
- [x] Free-running fractional divider, count-up + PH_CORRECT up/down, TOP wrap
- [x] CC compare -> GPIO level (slice N ch A/B -> GPIO 2N / 2N+1 [+16]),
      A_INV / B_INV; new `Gpio::kPwm` driver + FUNCSEL 4
- [x] Wrap interrupt -> PWM_IRQ_WRAP (IRQ4); wired into Simulator on_cycles()
- [x] B-pin DIVMODE 1-3 (datasheet 4.5.2.1): LEVEL gates the same
      fractional-divider clock on a live B-pin read (held entirely, not
      just un-paced, while B is low); RISE/FALL bypass the divider
      entirely and advance the counter once per detected edge on B
      instead (`Slice::prev_b` edge state). In all three, B's own PWM
      output driver is disabled (`update_outputs()` now also drives OE via
      `driver_set_pindir`, not just the output level - previously never
      set at all outside of what a test manually forced) so it correctly
      reads as an external input through `Gpio::level()`/`func_level()`,
      matching real hardware repurposing the pin
- [x] CSR.PH_ADV / PH_RET (manual +-1 count while running, for phase-
      aligning independently-started slices): applied immediately as
      self-clearing strobes (there's no real hardware latency here to poll
      through) rather than stored state; PH_RET is `advance_slice()`'s
      exact mirror (`retard_slice()`), and doesn't raise the wrap IRQ -
      it's a corrective nudge, not a real wrap event
- [ ] DMA: no PWM_WRAPn DREQ registered with the DMA controller. Unlike
      UART/SPI/I2C/ADC's DREQs (a level check - "is there room/data" -
      that fits `Dma::set_dreq_source()`'s `std::function<bool()>`
      abstraction directly), a PWM wrap DREQ is a one-shot pulse once per
      period with no FIFO behind it - genuinely different shape, not
      implemented here to avoid forcing a wrong abstraction; still falls
      back to DMA's generic `dreq_divisor()` approximation
- **Tests**: `tests/unit/test_pwm.cpp` (13 cases)
- **Design**: RP2040 datasheet 4.5

---

### PHASE 4: UART + SPI (Week 7)

#### P4.1: UART0 / UART1 Controller (PL011)  [DONE]
- [x] `Uart` BusPeripheral (`src/peripherals/uart.{h,cpp}`) @ 0x40034000 /
      0x40038000; UART0_IRQ = IRQ20, UART1_IRQ = IRQ21
- [x] UARTDR TX -> TX FIFO -> output log + on_transmit() callback (paced, see
      below); RX FIFO (32-deep) fed via the wire queue; UARTFR
      TXFE/TXFF/RXFE/RXFF/BUSY
- [x] UARTCR (UARTEN/TXE/RXE), UARTLCR_H store, UARTIMSC / UARTRIS / UARTMIS /
      UARTICR; RXRIS + TXRIS level-driven onto the NVIC
- [x] Wired into Simulator (both instances)
- [x] Baud-rate timing (bit-accurate TX/RX): UARTIBRD/UARTFBRD drive a 16x
      fractional bit-period generator (x64 fixed point, matching the PL011
      hardware's own fractional accumulator) paced from `on_cycles()` against
      clk_peri; a byte takes exactly `bits_per_frame()` bit periods to clock
      in or out. IBRD=0 (the reset value) disables the baud generator
      entirely, matching the datasheet - no data moves until firmware
      configures it.
- [x] Framing/parity/break errors: no physical wire to derive these from, so
      the test bench tags them explicitly via `feed(byte, RxError)`; surfaced
      per-datasheet in UARTDR[11:8], UARTRSR/ECR, and UARTRIS FERIS/PERIS/
      BERIS (approximation: RIS bits are sticky-latched at arrival rather
      than tracking the exact FIFO-head character, see uart.h)
- [x] Overrun (OE) detected for real: a paced RX arrival finds the FIFO
      already full -> byte dropped, OE set (sticky, RSR/ECR + RIS.OERIS)
- [x] Break detection (LCR_H.BRK): while set, TX holds off starting any new
      byte (line held low), matching the "continuous break" behaviour
- [x] DMA request lines: `tx_dreq_ready()`/`rx_dreq_ready()`, gated by
      UARTDMACR.TXDMAE/RXDMAE and real TX/RX FIFO state, registered with
      `Dma::set_dreq_source()` for DREQ_UART0/1_TX/RX (20-23) in
      `Simulator` - a UART-fed DMA channel is now paced by the UART's own
      real baud-rate-driven FIFO draining/filling, not a generic divisor
- **Tests**: `tests/unit/test_uart.cpp` (12 cases)
- **Effort**: 30 hours
- **Priority**: HIGH
- **Dependencies**: P1.3, P1.4

#### P4.2: UART1 Controller  [DONE]
- [x] Second `Uart` instance (see P4.1); UART1_IRQ = IRQ21

#### P4.3: SPI0 / SPI1 Controller (PL022)  [IN PROGRESS]
- [x] `Spi` BusPeripheral (`src/peripherals/spi.{h,cpp}`) @ 0x4003C000 /
      0x40040000; SPI0_IRQ = IRQ18, SPI1_IRQ = IRQ19
- [x] SSPDR write = full-duplex frame transfer: MOSI -> output log +
      on_transfer() callback; MISO from callback / feed() queue / 0xFF idle;
      internal loopback (CR1.LBM)
- [x] SSPSR (TFE/TNF/RNE/RFF/BSY from the real 8-deep TX/RX FIFOs), SSPCR0/CR1,
      SSPIMSC / SSPRIS / SSPMIS; RXRIS + TXRIS -> NVIC
- [x] Bit-rate timing: SSPCPSR x (1 + SSPCR0.SCR) SSPCLK cycles per bit
      (datasheet 4.4.3, plain integer divider - no fractional part, unlike
      UART), paced from `on_cycles()` against clk_peri; a frame takes exactly
      `frame_bits()` bit periods. CPSDVSR < 2 or odd leaves the bit-rate
      generator off (datasheet: CPSDVSR must be even, >= 2) - no data moves.
- [x] Frame size 4-16 (SSPCR0.DSS): the TX/RX FIFOs and SSPDR honour the
      configured width; the test-bench hooks (feed/on_transfer/output)
      remain 8-bit for convenience, zero-extended/truncated at the boundary
- [ ] CPOL/CPHA: stored in CR0 but no observable effect - this is a
      whole-frame behavioral model, not a bit-level clock/data waveform
      simulation, so there is nothing for polarity/phase to change (see
      spi.h)
- [ ] Chip-select lines (software bit-bangs CS via GPIO; out of this
      peripheral's scope)
- [x] DMA request lines: `tx_dreq_ready()`/`rx_dreq_ready()`, gated by
      SSPDMACR.TXDMAE/RXDMAE and real TX/RX FIFO state, registered for
      DREQ_SPI0/1_TX/RX (16-19) in `Simulator`
- **Tests**: `tests/unit/test_spi.cpp` (12 cases)
- **Design**: RP2040 datasheet 4.4

#### P4.4: SPI1 Controller  [DONE]
- [x] Second `Spi` instance (see P4.3); SPI1_IRQ = IRQ19, wired into
      `Simulator` alongside SPI0 (clock pacing + on_cycles)

#### P4.5 / P4.6: I2C0 / I2C1 Controller (DW_apb_i2c)  [DONE]
- [x] `I2c` BusPeripheral (`src/peripherals/i2c.{h,cpp}`) @ 0x40044000 /
      0x40048000; I2C0_IRQ = IRQ23, I2C1_IRQ = IRQ24
- [x] Master-mode model: IC_ENABLE, IC_TAR, IC_DATA_CMD (write byte / read
      command with STOP bit) queued into a real 16-deep TX command FIFO,
      executed against a registered `set_slave(addr7, fn)` callback once its
      byte period elapses; 16-deep RX FIFO
- [x] IC_STATUS (TFNF/TFE/RFNE/RFF from the real FIFOs), IC_TXFLR/IC_RXFLR,
      IC_RAW_INTR_STAT / IC_INTR_MASK / IC_INTR_STAT (RX_FULL, TX_ABRT,
      STOP_DET), IC_TX_ABRT_SOURCE, IC_CLR_* -> I2C IRQ
- [x] Address-NACK and TX-data-NACK aborts (evaluated when the queued command
      executes, not when IC_DATA_CMD is written)
- [x] Bus-level timing: IC_SS/FS_SCL_HCNT/LCNT (selected by CON.SPEED) give
      the SCL period in ic_clk (== clk_sys on the RP2040) cycles; a
      transaction takes 9 SCL periods (8 data bits + ACK), paced from
      `on_cycles()`. HCNT=LCNT=0 (the reset value) leaves the bit-rate
      generator off - commands stay queued, nothing executes.
- [x] Clock stretching: `stretch_next(cycles)` lets the test bench (playing
      the slave) add extra ic_clk cycles to the next transaction only, since
      there's no real open-drain SCL line for a slave to hold low against
- [x] DMA request lines: `tx_dreq_ready()`/`rx_dreq_ready()`, gated by
      IC_DMA_CR.TDMAE/RDMAE (offset 0x88) and real TX-command/RX FIFO state,
      registered for DREQ_I2C0/1_TX/RX (32-35) in `Simulator`
- [x] 10-bit addressing (datasheet 4.3.5): IC_TAR/IC_SAR already stored the
      full 10 bits; address matching now widens its mask from 0x7F to
      0x3FF whenever IC_CON.IC_10BITADDR_MASTER/_SLAVE is set, instead of
      always truncating to 7 bits - `set_slave()`'s address parameter
      widened `uint8_t` -> `uint16_t` to match
- [x] Slave mode (IC_CON.IC_SLAVE_DISABLE clear): a new
      `slave_transfer(addr, is_read, byte)` / `slave_stop()` pair - the
      slave-side counterpart to the existing master-side `set_slave()` -
      lets a test bench (or a hand-written virtual master) simulate an
      external master addressing this controller's own IC_SAR. A write
      lands in the same RX FIFO IC_DATA_CMD already reads from; a read
      with nothing queued raises RD_REQ and returns false (the same
      clock-stretching-by-approximation pattern `stretch_next()` already
      uses on the master side) until firmware answers via IC_DATA_CMD.
      IC_DATA_CMD is genuinely shared with the master command queue in
      real DW_apb_i2c hardware too: a new `rd_req_pending_` flag
      disambiguates a write as "the outstanding slave response" only
      while one is actually outstanding, so a device with *both* master
      and slave enabled at once - a real, valid RP2040 configuration, not
      just a hypothetical - still worked correctly, unlike an earlier
      version of this that (wrongly) disambiguated by the static
      IC_CON.MASTER_MODE bit instead and broke exactly that combination;
      caught by a test built around it
- [ ] Arbitration loss: not modelled, but as a direct consequence of "no
      multi-master arbitration" above (already a pre-existing, documented
      limitation) - there is only ever one bus driver in this simulator,
      so there's nothing to lose arbitration against. Not attempted.
- **Tests**: `tests/unit/test_i2c.cpp` (16 cases)
- **Design**: RP2040 datasheet 4.3

---

### PHASE 5: ADC + Advanced Interrupts (Week 8)

#### P4.4: DMA Controller (0x50000000)  [DONE]
- [x] `Dma` BusPeripheral (`src/peripherals/dma.{h,cpp}`), 12 channels
- [x] READ_ADDR / WRITE_ADDR / TRANS_COUNT / CTRL + all four alias groups,
      trigger on the last register of each alias
- [x] Transfer through the Memory bus: byte/half/word, INCR_READ / INCR_WRITE,
      RING_SIZE/RING_SEL wrap, BSWAP; READ/WRITE_ERROR on bus fault
- [x] CHAIN_TO (0-length loop guard), MULTI_CHAN_TRIGGER, CHAN_ABORT (stops a
      paced transfer mid-flight)
- [x] INTR (w1c) / INTE0/INTF0/INTS0 + INTE1/INTF1/INTS1 -> DMA_IRQ_0 (IRQ11)
      / DMA_IRQ_1 (IRQ12), both routed to both cores; N_CHANNELS
- [x] DREQ pacing: transfers are stepped by `Dma::on_cycles()` from
      `Simulator::step()`. PERMANENT (0x3F) = one element/clock; TIMER0..3
      (0x3B..0x3E) = `sys_clk * X/Y` from the DMA pacing-timer registers
      (0x420..0x42C). Real DREQs (datasheet 2.5.3.1 Table 119) use
      `Dma::set_dreq_source()`: for UART/SPI/I2C/ADC (13 of the 40 DREQ
      numbers - see their own P4.1/P4.3/P4.5-6/P5.1 entries) this is a real
      peripheral-FIFO-state check, up to one transfer/clock while ready - a
      level-check approximation of datasheet 2.5.3.2's credit-based DREQ
      scheme that converges to the same steady-state throughput without the
      short-term burst/credit bookkeeping. Unregistered DREQs (PIO, PWM,
      XIP) still fall back to one element every `dreq_divisor()` clocks.
      TRANS_COUNT reads the live remaining count while BUSY.
- [x] Sniff (SNIFF_CTRL @ 0x434 / SNIFF_DATA @ 0x438): folds every element of
      the selected channel (gated by CTRL.SNIFF_EN + SNIFF_CTRL.DMACH) into the
      accumulator. CALC 0x0/0x1 = CRC-32 / CRC-32R, 0x2/0x3 = CRC-16-CCITT /
      reversed, 0xE = XOR reduction, 0xF = sum; BSWAP pre-swap; OUT_REV /
      OUT_INV on read-back. Verified against zlib crc32("123456789") ==
      0xCBF43926 and CRC-16/CCITT-FALSE == 0x29B1.
- **Tests**: `tests/unit/test_dma.cpp` (15 cases)
- **Files**: `src/peripherals/dma.{h,cpp}`
- **Design**: RP2040 datasheet 2.5

#### P4.7: USB device controller (0x50100000)  [DONE (functional)]
- [x] `UsbCtrl` (`src/peripherals/usb.{h,cpp}`) - one decode window over
      USBCTRL_DPRAM (4 KB of endpoint buffers + buffer-control registers) and
      USBCTRL_REGS (@ +0x10000)
- [x] Registers: ADDR_ENDP, MAIN_CTRL, SIE_CTRL (PULLUP_EN etc.), SIE_STATUS
      (SETUP_REC / TRANS_COMPLETE / BUS_RESET / CONNECTED, w1c events),
      BUFF_STATUS (w1c), EP_STALL_ARM / EP_STATUS_STALL_NAK, USB_MUXING,
      USB_PWR, SOF_WR/RD, INTR (computed) / INTE / INTF / INTS -> USBCTRL_IRQ
      (IRQ 5), routed to both cores
- [x] EP0 buffer-control model (LENGTH / AVAILABLE / FULL / PID in the DPRAM)
- [x] Virtual-host API for enumeration tests: `host_reset()`, `host_setup()`,
      `host_in_ep0()`, `host_out_ep0()`
- [ ] Non-EP0 endpoints, double buffering, host mode, SOF timing, the SIE
      state machine - not modelled (no wire-level link partner)
- **Tests**: `tests/unit/test_usb.cpp` (5 cases: enable/pullup/addr, bus reset
      IRQ, a full control-IN transfer with an 18-byte device descriptor, a
      control-OUT data stage, DPRAM as RAM)
- **Files**: `src/peripherals/usb.{h,cpp}`
- **Design**: RP2040 datasheet 4.1

#### P5.1: ADC Controller  [DONE - no analog voltage model, permanent limit]
- [x] `Adc` BusPeripheral (`src/peripherals/adc.{h,cpp}`) @ 0x4004C000
- [x] 5 inputs (GPIO26-29 + temp sensor, gated by TS_EN); 12-bit RESULT;
      test bench sets raw codes via `set_input()`
- [x] START_ONCE and START_MANY both take the real 96 + DIV_INT ADC clocks
      per sample, paced by the 48 MHz ADC clock from `on_cycles()`
      (CS.READY stays clear for the whole conversion, not just an instant -
      START_ONCE previously completed synchronously, bypassing SAR timing
      entirely); RROBIN
- [x] 4-entry sample FIFO: FCS.EN/SHIFT, LEVEL, EMPTY/FULL, OVER/UNDER (w1c)
- [x] INTR / INTE / INTF / INTS with THRESH -> ADC_IRQ_FIFO (IRQ22)
- [x] DMA DREQ line: `dreq_ready()`, gated by FCS.DREQ_EN ("assert DMA
      requests when FIFO contains data") and real FIFO content, registered
      for DREQ_ADC (36) in `Simulator`
- [ ] Input from real GPIO pad voltage (permanent limit - no analog voltage
      model behind the GPIO pins, not a TODO)
- **Tests**: `tests/unit/test_adc.cpp` (12 cases)
- **Priority**: MEDIUM
- **Design**: RP2040 datasheet 4.9

#### P5.2: Advanced Interrupt Handling  [DONE - entry timing sourced from the real TRM]
- [x] Interrupt priority (0-3): `Cpu::priority_[]` + IPR registers
      (`Scs::read_ipr`/`write_ipr`), `effective_priority()`
- [x] Preemption: `Cpu::highest_pending_exception()` compares every pending
      exception's priority against `current_execution_priority()` (derived
      from the currently-running `regs_.exception_number()`) every
      instruction boundary; a strictly-higher-priority pending exception
      nests correctly (`take_exception()` re-enters while already in
      Handler mode, pushing a second frame on top)
- [x] Pending flag management: `pend_exception`/`clear_pending`/`is_pending`
      (`pending_` bitfield), `Scs` NVIC ISPR/ICPR
- [x] Active flag: `regs_.exception_number()` (== `ICSR.VECTACTIVE`, already
      modeled in `Scs`) is the only NVIC-visible "active" state ARMv6-M
      exposes - there is no IABR in this architecture (that's an
      ARMv7-M/Cortex-M3+ NVIC feature), so there is nothing further to add
- [x] Interrupt stacking (8-word frame): `Cpu::take_exception()`/
      `exception_return()` (see P1.4)
- [x] Exception entry latency: DDI 0484C section 3.6.1 - "the worst case
      interrupt latency ... in a zero wait-state system not using jitter
      suppression, is 15 cycles" - charged in full by `take_exception()` as
      `kExceptionEntryCycles` (`src/core/timing.h`). Verified against the
      real Cortex-M0+ r0p1 TRM, not from memory.
- [x] Late-arriving: satisfied by construction, not a separate feature -
      exception entry is atomic within one `Cpu::step()` call and always
      picks the highest-priority *pending* exception before starting entry,
      so there is no mid-stacking window for a late arrival to interject
- [ ] Tail-chaining's cycle *savings* (skipping the unstack/restack when a
      pending exception is ready right as the current handler returns):
      functionally already happens (the next `step()` naturally takes the
      next highest-priority pending exception), but this TRM edition
      documents no separate cycle figure for it or for exception *return*,
      so neither is cost-optimized - see `timing.h` and ARCHITECTURE.md 5.5
- **Tests**: `tests/unit/test_exceptions.cpp` (10 cases, incl. two exercising
      the 15-cycle entry charge directly)
- **Effort**: 20 hours
- **Priority**: HIGH
- **Dependencies**: P1.4

#### P5.3: Watchdog + RESETS  [DONE]
- [x] `Watchdog` (`src/peripherals/watchdog.{h,cpp}`) @ 0x40058000:
      LOAD/CTRL down-counter (decrements by 2 per us per the HW quirk),
      feed via LOAD, ENABLE, CTRL.TRIGGER force-reset, REASON (TIMER/FORCE),
      SCRATCH0-7 (survive reset), TICK. Timeout -> Simulator resets the CPU.
- [x] `Resets` (`src/peripherals/resets.{h,cpp}`) @ 0x4000C000: RESET /
      RESETS_WDSEL (a real, separate register from PSM_WDSEL below - scopes
      ~25 individual peripherals, e.g. UART0/1) / RESET_DONE (= ~RESET, so
      pico-sdk unreset_block_wait returns) with the +0x1000/2000/3000
      XOR/SET/CLR atomic aliases
- [x] Watchdog-scoped reset via PSM_WDSEL.PROC1 (datasheet 2.13): core 0 is
      always reset; core 1 is additionally reset only if PSM.WDSEL (read via
      the existing `StubPeripheral psm_` @ 0x40010000, offset 0x08) has
      PROC1 (bit 16) set - `Watchdog::set_wdsel_provider()`. This is the one
      PSM.WDSEL bit with meaningful behaviour here; the other 16 (SRAM
      banks, ROM, XIP, clocks, ...) have no "held in reset" concept to model
- [x] RESETS_WDSEL wired for real: `BusPeripheral::reset()` (`core/bus.h`),
      a virtual defaulting to a no-op, overridden by every peripheral class
      that has a RESETS_RESET/WDSEL bit *and* meaningful register state to
      reset - `Adc`, `Dma`, `I2c`, `IoBank0`, `PadsBank0`, `Pll`, `Pwm`,
      `Rtc`, `Spi`, `Timer`, `Uart`, `UsbCtrl`, and `PioRegisters` (which
      also resets the underlying `PioBlock` - program memory, IRQ register,
      both clock dividers, and every SM's *full* datapath via the new
      `StateMachine::full_reset()`, distinct from `restart()` == CTRL.
      SM_RESTART, which datasheet 3.5.4 documents as deliberately leaving
      X/Y, the FIFOs and the config registers alone). Each `reset()` matches
      exactly what a freshly-constructed instance looks like - restoring
      the same defaults the (already-tested) constructor already
      establishes, rather than re-deriving POR values independently and
      risking a transcription error against the datasheet. Simulator
      wiring/callbacks (Gpio&/Cpu*/Memory& references, `on_transmit`/
      `on_transfer`/`set_slave`/`set_dreq_source`-style test-bench hooks,
      clock-Hz values the clock tree pushes) are deliberately left alone -
      they stand in for physical wires/external inputs, which a peripheral
      reset doesn't disconnect on real hardware either.
      `Watchdog::on_peripheral_reset()` (a second callback alongside the
      existing PSM_WDSEL-driven `on_reset()`, fed by a new
      `set_resets_wdsel_provider()`) fires on every watchdog reset with the
      live RESETS_WDSEL value; `Simulator`'s wiring maps each of its bits
      (datasheet 2.14.1's RESETS_RESET field - ADC=0, DMA=2, I2C0/1=3/4,
      IO_BANK0=5, PADS_BANK0=8, PIO0/1=10/11, PLL_SYS/USB=12/13, PWM=14,
      RTC=15, SPI0/1=16/17, TIMER=21, UART0/1=22/23, USBCTRL=24) to the
      matching peripheral's `reset()`. `IoBank0::reset()` goes one step
      further than a plain register-clear: it also re-applies FUNCSEL=0/
      no-override to every pin's *live* `Gpio` state, since firmware may
      already have muxed pins away from GPIO/SIO control before this fires
      - leaving them stuck there (registers claiming "reset" while the pin
      still behaves as configured) would defeat the feature's actual real-
      hardware purpose of recovering a hung peripheral's pins. The 6
      bits with no dedicated register-state class here (BUSCTRL/IO_QSPI/
      JTAG/PADS_QSPI/SYSCFG/TBMAN) and SYSINFO (no mutable state to reset)
      are simply absent from the mapping; `BusPeripheral::reset()`'s
      default no-op covers them if any is ever added.
- [x] Pause-on-debug (PAUSE_DBG0/1/JTAG): investigated, found **not
      applicable** to this simulator's execution model rather than merely
      unimplemented. `Watchdog::on_cycles()` only advances when
      `Simulator::step()` is called, which only happens while something
      (the CLI run loop, GDB stub's continue handler, `DebugSession`'s
      background thread) is actively stepping the CPU - there is no
      independent wall clock (CLAUDE.md/CONTEXT.md: determinism and
      reproducibility are hard requirements, not aspirations). So the
      watchdog is *already* unconditionally paused for the entire duration
      of any debug halt, regardless of what PAUSE_DBG0/1/JTAG say -
      implementing the bits as real hardware does would require decoupling
      the watchdog's tick source from cycle-accounting into a genuine
      wall-clock timer, which would then need gating *off* by these same
      bits to reproduce today's (already-correct-for-this-model) behaviour.
      That's a net-new wall-clock subsystem in service of undoing itself -
      not a missing feature, an architectural mismatch. Left `[ ]` below as
      a documented non-goal rather than closed as done or silently dropped.
- [ ] Pause-on-debug (PAUSE_DBG0/1/JTAG) as literal register bits: see
      above - not planned, kept here only so the datasheet field itself
      isn't mistaken for an oversight
- **Tests**: `tests/unit/test_watchdog.cpp` (6 cases),
      `tests/unit/test_resets_wdsel.cpp` (6 cases: selective per-peripheral
      reset via UART0/1, PWM, DMA, PIO0, and that a plain non-watchdog
      reset leaves RESETS_WDSEL-selected peripherals alone)
- **Design**: RP2040 datasheet 4.7, 2.13, 2.14
- **Files**: `src/core/bus.h` (`BusPeripheral::reset()`), `src/peripherals/
      watchdog.{h,cpp}`, `src/peripherals/resets.h` (`wdsel()` getter),
      `src/simulator.cpp` (the bit-to-peripheral wiring), every peripheral
      listed above, `src/pio/{pio_block,state_machine}.{h,cpp}`

> **Cross-cutting:** `src/core/atomic_peripheral.h` provides the shared
> `AtomicPeripheral` base (span 0x4000; XOR/SET/CLR at +0x1000/2000/3000 as a
> read-modify-write over `reg_read`/`reg_write`). Adopted by `Resets`,
> `IoBank0`, `Pwm`, `Timer`, `Dma`, `Adc`, `PioRegisters`, `Xosc`, `Pll`,
> `Clocks`. Not `Scs` - the PPB (0xE0000000) has no atomic aliases. `Sio`
> (0xD0000000) has its own SET/CLR/XOR *registers* rather than address
> aliases, already handled.

#### P5.4: Real-Time Clock (RTC)  [DONE (functional)]
- [x] `Rtc` (`src/peripherals/rtc.{h,cpp}`) @ 0x4005C000, AtomicPeripheral
- [x] SETUP_0/1 + CTRL.LOAD set the calendar; RTC_0/RTC_1 read it back
- [x] on_cycles() advances one second per (CLKDIV_M1+1) RTC ticks; full
      MM/YYYY roll-over incl. leap years
- [x] Field-masked alarm (IRQ_SETUP_0/1 MATCH bits) -> rising-edge RTC_IRQ
      (IRQ25); INTR w1c
- **Tests**: `tests/unit/test_rtc.cpp` (4 cases)

#### P3.1b: PADS_BANK0  [DONE (functional)]
- [x] `PadsBank0` (`src/peripherals/padsbank0.{h,cpp}`) @ 0x4001C000,
      AtomicPeripheral; PUE/PDE -> Gpio::set_pulls, other pad bits stored
- **Tests**: `tests/unit/test_padsbank0.cpp` (2 cases)
- **Dependencies**: P1.3

---

### PHASE 6: Clock Manager (Week 9)

#### P6.0: Clock tree  [DONE]
- [x] `Xosc` @ 0x40024000: CTRL, STATUS.STABLE/ENABLED once ENABLE = 0xFAB
- [x] `Rosc` @ 0x40060000: boots enabled + STABLE (RP2040 runs from ROSC at
      reset), STATUS.ENABLED/DIV_RUNNING/STABLE track CTRL.ENABLE, FREQA/FREQB
      password-guarded with STATUS.BADWRITE (w1c), RANDOMBIT, DIV/PHASE stored
- [x] `Pll` x2 @ 0x40028000 / 0x4002C000: CS.LOCK once powered + not bypassed,
      PWR / FBDIV_INT / PRIM stored
- [x] `Clocks` @ 0x40008000: 10 generators x CTRL/DIV; SELECTED one-hot of
      CTRL.SRC so `clock_configure()` does not spin
- [x] All on `AtomicPeripheral`; wired into the Simulator
- [x] `ClockTree` derives real clk_sys / clk_peri / clk_adc / clk_rtc from the
      generator muxes + dividers + PLLs, and the microsecond tick from
      WATCHDOG_TICK.CYCLES; `Simulator` pushes these into TIMER/WATCHDOG/ADC/RTC
      (see P1.5). CPU/PIO already run at clk_sys by construction.
- **Tests**: `tests/unit/test_clocks.cpp` (10), `tests/unit/test_clock_tree.cpp` (6)
- **Files**: `src/peripherals/clocks.{h,cpp}`, `src/peripherals/clock_tree.{h,cpp}`
- **Design**: RP2040 datasheet 2.15-2.18

#### P6.1: Oscillators & PLL (accurate)
- [x] XOSC (12 MHz crystal) - functional model (STABLE/ENABLED)
- [x] ROSC (ring oscillator) - functional model (STABLE/BADWRITE/RANDOMBIT)
- [x] CPU/USB PLL FBDIV + POSTDIV1/POSTDIV2 -> `Pll::output_hz(ref)`
      (VCO = ref*FBDIV, out = VCO/(PD1*PD2)); 0 unless locked. pico-sdk's
      125 MHz recipe (FBDIV 125, /6, /2 from 12 MHz) verified
- [x] Feed the derived clk_sys into peripheral pacing (`ClockTree`, see P6.0)
- [ ] CPU PLL
  - [ ] Feedback divider (FBDIV)
  - [ ] Post-dividers (POSTDIV1, POSTDIV2)
  - [ ] Lock detection
- [ ] USB PLL
  - [ ] Same structure as CPU PLL
  - [ ] 48 MHz target
- [ ] Reference clock validation
- **Tests**: 20+ PLL tests
- **Effort**: 20 hours
- **Priority**: HIGH
- **Dependencies**: P1.5

#### P6.2: Glitchless Clock Mux
- [ ] Clock source switching
- [ ] Zero-glitch transition
- [ ] Status monitoring
- **Tests**: 10+ switching tests
- **Effort**: 10 hours
- **Priority**: MEDIUM
- **Dependencies**: P6.1

#### P6.3: Clock Dividers
- [ ] CPU clock division
- [ ] Peripheral clock division
- [ ] Per-peripheral clock gating
- **Tests**: 15+ divider tests
- **Effort**: 10 hours
- **Priority**: MEDIUM
- **Dependencies**: P6.1

---

### PHASE 7: Loaders & Debugging (Week 10)

#### P7.1: ELF Loader  [DONE - pulled forward for CPU validation]
- [x] Parse + validate ELF32 header (magic, ELFCLASS32, LSB, ET_EXEC/DYN, EM_ARM)
- [x] Read program headers, load PT_LOAD segments at p_paddr (LMA) via backdoor
- [x] Zero-fill the BSS tail (p_memsz > p_filesz); bounds-check every range
- [x] Entry point (e_entry) reported; lowest/highest loaded address
- [x] `load_elf_file()` convenience; CLI `rp2040-sim <firmware.elf>` driver
- [x] Symbol table / section names (for the debugger / traces): parses
      SHT_SYMTAB + its linked SHT_STRTAB and every section header's name
      into `ElfImage::symbols`/`sections`; `symbol_at(addr)` resolves a PC to
      the tightest enclosing symbol (Thumb bit masked). Best-effort: absent
      rather than a load failure if the ELF is stripped or the section
      headers are malformed, since only PT_LOAD is required for execution.
      Caught a real off-by-Thumb-bit bug in the validating test itself (not
      in symbol_at()) by exercising it against the real GCC/ld-built sum.elf
      fixture rather than only synthetic ELFs.
- **Tests**: `tests/unit/test_elf_loader.cpp` (synthetic ELFs) +
      `tests/integration/test_firmware.cpp` (real arm-none-eabi-gcc -O2 image
      built by CMake, run end-to-end through the simulator, incl. a symbol
      table / section name check against real linker output)
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P1.3
- **Files**: `src/loaders/elf_loader.{h,cpp}`, `src/main.cpp`, `tests/fixtures/`

#### P7.2: UF2 Loader  [DONE]
- [x] Parse the 512-byte UF2 block format (both start magics + end magic)
- [x] Flash image validation: payload <= 476 B, numBlocks vs file length,
      target range backed by ROM/Flash/SRAM, address-space wrap
- [x] Family-ID check (rejects non-RP2040 0xE48BFF56 when the flag is present)
- [x] "Not main flash" blocks skipped and counted; lowest/highest span reported
- [x] `load_uf2_file()` convenience; `Simulator::load()` dispatches on `.uf2`
      and resets through the image's vector table
- [x] Boot2 skip for flash images: the RP2040 boot ROM always validates and
      runs a mandatory 256-byte stage-2 bootloader (every pico-sdk
      `boot2_*.S`, and any hand-written equivalent - e.g. this repo's picoOS
      fixture - is exactly that size, CRC32 included) before the app's own
      vector table; `Simulator::load()` now sets VTOR to `lowest_addr + 256`
      for images that land in `Memory::kFlash`, matching that boot sequence,
      instead of resetting straight into the boot2 stub as if it were the
      vector table. RAM-resident images (SRAM-targeted, never boot-ROM
      validated) are unaffected - VTOR stays at `lowest_addr`. Found via a
      real firmware (picoOS) that lockup'd at instruction 0 when loaded as
      `.uf2` (worked fine as `.elf --entry`, which bypasses VTOR/reset
      entirely) - `Simulator::load()`'s UF2 branch had no test coverage at
      all before this.
- [ ] Extension-tag / MD5-region parsing (not emitted by elf2uf2; deferred)
- **Tests**: `tests/unit/test_uf2_loader.cpp` (synthetic blocks: two-block image,
      not-main-flash skip, no-family block, 7 malformed-stream subcases, run on
      CPU) + `tests/unit/test_uf2_boot.cpp` (flash image skips boot2, SRAM
      image doesn't) + `tests/integration/test_firmware.cpp` (real sum.elf
      repackaged as UF2 and run end-to-end)
- **Effort**: 10 hours
- **Priority**: MEDIUM
- **Dependencies**: P1.3
- **Files**: `src/loaders/uf2_loader.{h,cpp}`, `src/simulator.cpp`

#### P7.3: PIO Assembler  [DONE - core language]
- [x] pioasm syntax: 9 instructions + `nop`, `.program`, `.define [PUBLIC]`,
      `.origin`, `.side_set N [opt] [pindirs]`, `.wrap_target` / `.wrap`,
      labels (`name:` / `PUBLIC name:`), `; // /* */` comments
- [x] Label resolution (forward + backward) via a two-pass assembler
- [x] Expression evaluation (+ - * , unary - ~ ::, parens, hex/bin/dec,
      symbols); defines and labels share one symbol table
- [x] Instruction encoding cross-checked by round-tripping through pio_decode
- [x] Side-set / delay field packing incl. the optional enable bit and the
      per-width delay-range check
- [x] Error reporting with `line N:` prefixes
- [ ] Code-gen back-ends (C/Python/Ada headers) - not needed by the sim
- **Tests**: `tests/unit/test_pio_assembler.cpp` (squarewave, ws2812-style
      side-set, labels, defines/expressions, every mov/irq/push/pull form,
      comment styles, 6 diagnostic subcases)
- **Effort**: 25 hours
- **Priority**: HIGH
- **Dependencies**: P2.1, P2.2, P2.3
- **Files**: `src/pio/pio_assembler.{h,cpp}`

#### P7.4: GDB Stub (Remote Serial Protocol)  [IN PROGRESS]
- [x] `GdbStub` (`src/debuggers/gdb_stub.{h,cpp}`): pure `handle_packet()`
      protocol handler over a `Simulator&`, plus framing/checksum helpers
- [x] $g / $G / $p / $P (r0-r12, sp, lr, pc, xpsr; also reg 25 = xpsr)
- [x] $m / $M (byte hex, E01 on bus fault)
- [x] $c / $s / vCont;c / vCont;s -> S05 / S0B stop replies
- [x] $Z0 / $z0 software breakpoints (address set, checked on continue)
- [x] $?, qSupported (PacketSize + QStartNoAckMode+), qAttached, qC,
      threadinfo, QStartNoAckMode, H, D, k
- [x] TCP transport `serve(port)` (winsock / BSD sockets); CLI `--gdb <port>`
- [x] Watchpoints ($Z2/$Z3/$Z4 write/read/access, $z2-4 remove): backed by a
      new watchpoint mechanism in `Memory` itself (checked on every
      successful CPU-bus-path access - read_byte/half/word,
      write_byte/half/word - not the loaders' backdoor path), since that's
      the single choke point for both real CPU accesses and this stub's own
      $m/$M reads. `Memory::suppress_watchpoints()` stops the debugger's own
      $m/$M memory inspection from re-triggering the watchpoint it's
      investigating. Reports `T05watch:<addr>;` / `T05rwatch:<addr>;`.
- [ ] $qXfer:features:read (target.xml), per-core thread switching,
      run-length-encoded replies, live arm-none-eabi-gdb integration test
- **Tests**: `tests/unit/test_gdb_stub.cpp` (10 cases), plus 3 new
      `tests/unit/test_memory.cpp` cases for the watchpoint mechanism itself
- **Design**: GDB RSP spec; ARM m-profile register layout
- **Files**: `src/debuggers/gdb_stub.{h,cpp}`, `src/main.cpp`
- **Effort**: 30 hours
- **Priority**: HIGH
- **Dependencies**: P1.1, P1.3

#### P7.5: PIO Debugger  [DONE]
- [x] Per-SM breakpoints keyed by (block, sm, program address); run-until-break
- [x] Single-clock stepping over both blocks (`PioDebugger::step` / `run`)
- [x] Register inspection: PC, X, Y, OSR, ISR + shift counts, enabled, stall
- [x] FIFO state inspection (TX/RX levels)
- [x] Instruction trace (cycle, block, sm, pc, encoded word) + disassembly
- [x] Per-SM retired-instruction counts (`PioBlock::instructions_retired`)
- [x] PIO disassembler (`src/pio/pio_disasm.{h,cpp}`), verified as the inverse
      of the assembler by a round-trip test
- **Tests**: `tests/unit/test_pio_debugger.cpp`, `tests/unit/test_pio_disasm.cpp`
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P2.1, P7.4
- **Files**: `src/debuggers/pio_debugger.{h,cpp}`, `src/pio/pio_disasm.{h,cpp}`

#### P7.6: Profiler & Performance Analysis  [DONE - core]
- [x] Cycle counter + CPI (from the existing Cortex-M0+ timing model)
- [x] Per-PC hot-spot histogram (exec count + cycles), top-N report
- [x] Per-vector exception stats: entry count, total + max handler cycles
      (handler frames tracked on a stack, so nested/preempted handlers are
      attributed correctly)
- [x] `Profiler::run()` mirrors `Simulator::run()` stop conditions; stats
      accumulate across calls until `reset()`
- [ ] Memory-access timeline (needs a bus hook - deferred)
- **Tests**: `tests/unit/test_profiler.cpp`
- **Effort**: 15 hours
- **Priority**: MEDIUM
- **Dependencies**: P1.1, P7.4
- **Files**: `src/debuggers/profiler.{h,cpp}`

---

### PHASE 8: Testing & Validation (Week 11)

#### P8.1: Unit Test Suite
- [ ] CPU instruction tests (30+ per type)
- [ ] PIO instruction tests (40+ per type)
- [ ] Memory tests (15+)
- [ ] GPIO tests (30+)
- [ ] UART tests (30+)
- [ ] SPI tests (25+)
- [ ] Timer tests (20+)
- [ ] ADC tests (15+)
- [ ] Interrupt tests (20+)
- **Total**: 200+ tests
- **Effort**: 40 hours
- **Priority**: CRITICAL
- **Coverage Target**: >90%
- **Dependencies**: All P1-P7

#### P8.2: Integration Test Suite
- [ ] CPU + GPIO interaction (10+)
- [ ] PIO + GPIO interaction (15+)
- [ ] CPU + UART echo (5+)
- [ ] Multi-SM PIO scenarios (10+)
- [ ] Interrupt + peripheral combos (10+)
- **Total**: 50+ tests
- **Effort**: 20 hours
- **Priority**: HIGH
- **Dependencies**: All P1-P7

#### P8.3: Hardware Validation Suite
- [ ] Blink test (GPIO output)
- [ ] UART echo test
- [ ] SPI loopback
- [ ] Timer interrupt test
- [ ] PIO state machine test
- [ ] Multi-SM parallel test
- [ ] Clock switching test
- [ ] Full integration test (10 scenarios)
- **Tests**: 20+ scenarios with oscilloscope + analyzer
- **Effort**: 30 hours
- **Priority**: CRITICAL
- **Requirements**: 2× Pico, Pico Debug Probe, oscilloscope, logic analyzer
- **Expected Accuracy**: ±1% cycle count

#### P8.4: Regression Test Suite
- [ ] Golden traces (known-good outputs)
- [ ] Snapshot comparison
- [ ] Automated regression runner
- [ ] Change-impact analysis
- **Tests**: 20+ regression scenarios
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: All P1-P7

#### P8.5: Performance Benchmarks
- [ ] Simulation speed measurement
- [ ] Memory footprint analysis
- [ ] Bottleneck profiling
- [ ] Optimization recommendations
- **Effort**: 10 hours
- **Priority**: MEDIUM
- **Target**: >5x real-time

---

### PHASE 9: Documentation & Polish (Week 12)

#### P9.1: Code Documentation
- [ ] Inline comments (every complex section)
- [ ] API documentation (Doxygen format)
- [ ] Component descriptions
- [ ] Design rationale
- **Effort**: 20 hours
- **Priority**: HIGH

#### P9.2: Technical Documentation
- [ ] Architecture guide (ARCHITECTURE.md)
- [ ] Design decisions (DESIGN.md)
- [ ] Implementation notes
- [ ] Known limitations
- [ ] Fidelity matrix
- **Effort**: 15 hours
- **Priority**: HIGH

#### P9.3: User Guide
- [ ] Installation instructions
- [ ] Quick start guide
- [ ] Usage examples
- [ ] Debugging workflow
- [ ] Troubleshooting
- **Effort**: 10 hours
- **Priority**: MEDIUM

#### P9.4: Code Review & Polish
- [ ] Consistency check
- [ ] Error handling audit
- [ ] Performance review
- [ ] Memory leak check
- [ ] Test coverage report
- **Effort**: 15 hours
- **Priority**: HIGH

#### P9.5: Thesis Manuscript
- [ ] Introduction & motivation
- [ ] Literature review
- [ ] Architecture & design
- [ ] Implementation details
- [ ] Evaluation & validation
- [ ] Results & conclusions
- [ ] Appendices
- **Effort**: 40 hours (outside this schedule)
- **Priority**: CRITICAL

#### P10.1: Local Web Lab - Backend (`tools/lab_server`)  [DONE]
- [x] `DebugSession` (`debug_session.{h,cpp}`): JSON-friendly wrapper around
      one `Simulator`, near-copy of `src/debuggers/gdb_stub.cpp`'s `run()`
      breakpoint-checking pattern - PC breakpoint `std::set`, watchpoint hits
      via `Memory::take_watchpoint_hit()` - but running the continue loop on
      a background `std::thread` (batches of 2000 steps per mutex lock) so
      `/state` polling stays responsive while firmware runs
- [x] `Compiler` (`compiler.{h,cpp}`): spawns `arm-none-eabi-gcc` via a real
      argv vector (`_spawnv`/`posix_spawn` - no shell, so no `cmd.exe`
      quoting bugs), same flags/linker script as `tests/fixtures/firmware.ld`;
      also runs `arm-none-eabi-objdump -dl` and parses its interleaved
      source/disassembly output into a source-line -> PC-address map (not a
      full DWARF `.debug_line` parser - good enough for the single-file v1
      firmware model) so the editor's gutter clicks can set real breakpoints
- [x] `main.cpp`: cpp-httplib server, routes `/health`, `/compile`, `/load`,
      `/run`, `/pause`, `/step`, `/state`, `/breakpoints`,
      `/gpio/:pin/external`, `/uart/:n/feed`; base64 for ELF/UF2 transport
- [x] v1 firmware model matches `tests/fixtures/sum.c`: freestanding C with a
      `_start`, no libc, no pico-sdk - real pico-sdk compilation is a
      separate `mode`, added in P10.3
- [x] Vendored `cpp-httplib` (v0.54.1) and `nlohmann/json` (v3.12.0) under
      `tools/lab_server/vendor/` (MIT, single-header, same pattern as
      `tests/vendor/doctest.h`) - isolated to this target; `rp2040_core`
      stays dependency-free
- **Tests**: `tests/unit/test_debug_session.cpp` (6 cases: load/error, step,
      breakpoint hit, pause mid-run, breakpoint removal) plus a manual
      `curl`-based `/compile` -> `/load` -> `/run` -> `/state` smoke test
- **Design**: see `ARCHITECTURE.md` "Local web lab"
- **Files**: `tools/lab_server/{main,debug_session,compiler}.{h,cpp}`,
      `tools/lab_server/vendor/{httplib.h,json.hpp,README.md}`,
      `tools/lab_server/CMakeLists.txt`
- **Effort**: 12 hours
- **Priority**: MEDIUM (explicit scope addition, 2026-08-31 - not part of
      the original thesis plan; see `CONTEXT.md` Decision 6)
- **Dependencies**: P1.1, P1.3, P7.4 (watchpoint mechanism reused as-is)

#### P10.2: Local Web Lab - Frontend (`web/`)  [DONE]
- [x] Vite + React + TypeScript scaffold, `@monaco-editor/react` for the
      code editor
- [x] `Editor.tsx`: Monaco editor with real gutter breakpoints - glyph-margin
      clicks map source line -> PC address via the backend's objdump-derived
      line map, wired to `POST /breakpoints`; renders breakpoint dots and a
      current-PC line highlight. `automaticLayout: true` plus one deferred
      `editor.layout()` call on mount work around a real Monaco-in-React
      sizing bug (verified live in-browser: Monaco's first layout pass can
      race the surrounding CSS grid and lock in a near-zero size that its own
      resize observer then never corrects, since the *container* never
      resizes afterward - only Monaco's stale internal measurement is wrong)
- [x] `Console.tsx`: UART0/1 output stream (polled from `/state`) + an input
      box wired to `/uart/:n/feed`
- [x] `PinPanel.tsx`: all 30 GPIO rows (level/direction/function), clickable
      when a pin is configured as input, wired to `/gpio/:pin/external`
- [x] `DebugToolbar.tsx` + `RegisterView.tsx`: compile/load/upload, run/
      pause/step controls, register dump, current PC/status
- [x] `api.ts`: typed `fetch()` client for every backend route
- **Verified live in-browser** (not just typechecked): compile -> load ->
      set a gutter breakpoint -> run -> execution stops exactly there with
      correct PC/registers -> step advances one instruction -> PinPanel
      toggle reflected in `/state`. Caught and fixed two real bugs this way
      that a type-check alone would have missed: (1) a duplicated
      `Access-Control-Allow-Origin` header on CORS preflight responses
      (`set_default_headers` + an explicit `Options` handler both setting
      it) that Chrome silently rejects for every POST route while GET
      polling kept working, masking the failure; (2) the Monaco sizing race
      above
- **Design**: see `ARCHITECTURE.md` "Local web lab"
- **Files**: `web/src/{App.tsx,api.ts}`, `web/src/components/*.tsx`
- **Effort**: 10 hours
- **Priority**: MEDIUM (same scope addition as P10.1)
- **Dependencies**: P10.1

#### P10.3: Local Web Lab - pico-sdk Compile Support  [DONE]
- [x] `/compile` gains a `mode` field (`freestanding` default, matching
      P10.1 unchanged; `pico_sdk` new) routing to a real pico-sdk CMake+Ninja
      build instead of a bare `arm-none-eabi-gcc` invocation
- [x] `compile_pico_sdk_firmware()` (`compiler.{h,cpp}`): a **persistent**
      project/build directory pair (`<temp>/rp2040lab_pico_{project,build}`,
      not a fresh one per request like P10.1's freestanding path) so
      Ninja's incremental build stays fast after the first compile - pico-sdk
      builds its own core libraries (`pico_runtime`, `hardware_gpio`, ...)
      from scratch the first time (~10-20s observed), but a source-only
      change afterward rebuilds in under a second. `PICO_SDK_PATH`/bundled
      CMake+Ninja are auto-detected in `CMakeLists.txt` the same
      graceful-degradation way `ARM_NONE_EABI_GCC` already was (matches this
      machine's `~/.pico-sdk/{sdk,cmake,ninja,toolchain}/<version>/` layout,
      the official pico-vscode extension's install convention)
- [x] Fixed `CMakeLists.txt` template: one `main.c`, `pico_stdlib` +
      the `hardware_*` libraries this simulator implements
      (pwm/adc/dma/i2c/spi/rtc/watchdog), `pico_enable_stdio_uart(1)` /
      `..._usb(0)` so `printf()` reaches the existing UART0 console - not a
      general multi-file CMake project (that's the still-deferred item
      below)
- [x] `PICO_DEFAULT_{BIT_OPS,DIVIDER,DOUBLE,FLOAT,MEM_OPS}_IMPL` forced to
      each library's `compiler` (libgcc) variant instead of pico-sdk's
      default bootrom-lookup variant, and
      `PICO_RUNTIME_SKIP_INIT_{BOOTROM_RESET,PER_CORE_BOOTROM_RESET}` set -
      this simulator's ROM is an empty 16 KiB block (no proprietary,
      unredistributable Raspberry Pi bootrom image - see `ARCHITECTURE.md`
      "Local web lab"), so every pico-sdk code path that looks up an
      optimized routine there via `rom_func_lookup()` must be steered to its
      documented non-ROM fallback instead. An approximation worth being
      explicit about: real hardware's bootrom-provided bit-ops/divide/float
      routines are faster than libgcc's; firmware built through this lab
      trades a little runtime speed for actually booting in simulation
- **Known characteristic, not a bug**: `sleep_ms()`/busy-wait delays feel
      slow in the browser because this simulator is cycle-accurate but not
      wall-clock-synced (CLAUDE.md: accuracy over speed) - `--profile`
      against the example blink+printf firmware measured ~3.5M retired
      instructions/sec, with 99.6% of them spent spinning in `sleep_ms`'s
      timer-polling loop alone, so a `sleep_ms(500)` call takes several real
      seconds rather than 500ms. `DebugSession::run_loop()` adds no
      meaningful overhead on top of that ceiling (confirmed via the same
      profiling) - the frontend's `DebugToolbar.tsx` now says so explicitly
      (a note next to Run while running) rather than leaving it looking
      broken. Raw interpreter throughput is a separate, real optimization
      target (`BACKLOG.md` P8.5 "Performance Benchmarks") if ever revisited
- **Three real core bugs found and fixed while getting an actual pico-sdk
  binary to boot** (not hypothetical - each reproduced via a live
  `arm-none-eabi-gdb` backtrace against the simulator, see git history for
  the session this landed in):
  - `Simulator::load(path, from_entry=false)` used the lowest loaded PT_LOAD
    segment's address as VTOR, which is wrong whenever a flash image has a
    boot-stage stub segment before its real vector table (pico-sdk's 256-byte
    `.boot2`) - it now prefers the ELF's `__vectors`/`__VECTOR_TABLE`/
    `__Vectors` symbol when present (`elf_loader.{h,cpp}`'s new
    `ElfImage::symbol_named()`, `simulator.cpp`). General ELF-loading fix,
    not lab-server-specific - also fixes `rp2040-sim`'s own CLI boot path for
    any similarly-laid-out flash image
  - `DebugSession::step_locked()` didn't handle `ExecStatus::Breakpoint`
    from a *real* `bkpt` instruction in the executed code (as opposed to
    this session's own address-matched breakpoints) - it silently continued
    past it into whatever memory followed, which for pico-sdk's default
    "unhandled interrupt" ISR stubs meant executing garbage data as
    instructions. Now halts with `RunStatus::Breakpoint`, matching
    `gdb_stub.cpp`'s already-correct handling of the same `ExecStatus`
  - `Memory::write_scalar()` didn't implement RP2040's atomic register
    aliasing (datasheet 2.1.3: a write to a peripheral's base address +
    0x1000/0x2000/0x3000 XORs/SETs/CLEARs the register at the base instead
    of storing directly) - `hw_set_bits()`/`hw_clear_bits()`/`hw_xor_bits()`,
    used throughout every pico-sdk peripheral driver, compile straight to
    this and faulted immediately. Implemented in `memory.cpp` as a
    read-modify-write fallback when a direct address match fails
- **Tests**: `tests/unit/test_memory.cpp` (atomic aliasing: XOR/SET/CLEAR,
  the 16 KiB-slot addressing model, faults on an unmapped alias base),
  `tests/unit/test_debug_session.cpp` ("a live bkpt instruction halts a
  background run"), `tests/unit/test_elf_loader.cpp` (`symbol_named()`)
- **Verified live**: compiled the example below through the actual server,
  loaded with `fromEntry:false`, ran it in-browser - reached `main()`,
  `printf()` output appeared in the UART console, GP25 correctly showed
  `driving:true, funcsel:5` (real `gpio_init()` sets the pin's IO_BANK0
  function to SIO, unlike P10.2's hand-written freestanding demo firmware,
  which pokes `SIO_GPIO_OE` directly and never sets it - a small honest gap
  in that demo, not in the simulator)
- **Files**: `tools/lab_server/compiler.{h,cpp}`, `tools/lab_server/main.cpp`,
  `tools/lab_server/CMakeLists.txt`, `src/loaders/elf_loader.{h,cpp}`,
  `src/simulator.cpp`, `tools/lab_server/debug_session.cpp`,
  `src/core/memory.cpp`, `web/src/{App.tsx,api.ts}`,
  `web/src/components/DebugToolbar.tsx`
- **Effort**: 8 hours
- **Priority**: MEDIUM (same scope addition as P10.1/P10.2)
- **Dependencies**: P10.1, P10.2

#### P10.4: Local Web Lab - Multi-File Projects  [DONE]
- [x] `/compile`'s `source` field replaced by `files: [{name, content}]`
      (both modes) - flat (no subdirectories), `.c`/`.h` only, matching this
      item's own original framing ("not a general multi-file CMake
      project"). `compiler.h`'s `LineAddr` gained a `file` field (objdump's
      `-dl` markers already carried the source path per entry; the parser
      just wasn't keeping it), so breakpoints resolve per-(file, line), not
      just per-line
- [x] Freestanding mode (`compile_firmware()`): writes the whole file set
      into a fresh per-request subdirectory (real names, not the old
      `rp2040lab_src_<id>.c`), invokes gcc with every `.c` file as a source
      argument - headers resolve the same way `#include "..."` does in any
      multi-TU C build, no extra step needed
- [x] pico-sdk mode (`compile_pico_sdk_firmware()`): `CMakeLists.txt`'s
      `add_executable(labfw main.c)` became `file(GLOB SOURCES
      CONFIGURE_DEPENDS *.c)` + `add_executable(labfw ${SOURCES})` -
      `CONFIGURE_DEPENDS` (CMake ≥3.12) makes `cmake --build` notice when a
      file was added/removed and reconfigure automatically, so the
      persistent build dir (P10.3's speed trick) still works with a
      changing file set. Also deletes any `*.c`/`*.h` left in the project
      dir from a previous compile that isn't in the current file set before
      writing - otherwise a file the user deleted client-side would linger
      and stay linked. Verified directly: compiled a 2-file project, then
      recompiled with one file removed while the other still referenced
      it - correctly failed to link (confirms the stale copy was actually
      gone, not just hidden from the editor)
- [x] `Editor.tsx` reworked from controlled `value`/`onChange` (one Monaco
      model, implicit) to imperative multi-model management - one
      `monaco.editor.ITextModel` per file, created/disposed/synced against
      `files` in an effect, `editor.setModel()` swaps which one is visible.
      A model's `setValue()` only fires when its content actually differs
      from the incoming prop, so this editor's own edits (which round-trip
      back through React state unchanged) never reset the cursor/undo stack
      mid-keystroke - only genuinely external changes (mode switch,
      localStorage restore) do
- [x] New `FileTabs.tsx`: tab strip (filename + × per tab, click to open,
      double-click to rename, trailing "+" to add) - `window.prompt()`/
      `window.alert()` for naming, the simplest reasonable v1 UX rather than
      a new modal component
- [x] Whole project (mode + all files + active tab) auto-persisted to
      `localStorage` (`rp2040lab.project.v1`, ~500ms debounced), restored on
      load - one working set, not several named projects (that's a natural
      v2 if ever wanted). Verified live: added a file, wrote a cross-file
      function call, reloaded the page, both files and the active tab came
      back exactly as left
- **Real bug found writing the frontend, not just this feature**: Monaco's
  `onMount` fires exactly once, but `@monaco-editor/react` loads Monaco
  itself asynchronously - a model-sync effect that only runs on `files`/
  `activeFile` changes can execute *before* `onMount` ever fires (editor ref
  still null, effect no-ops), with nothing left to re-trigger it once the
  editor is finally ready, since refs alone don't cause a re-render. Fixed
  by calling the same sync logic once directly from `onMount`, reading
  through refs kept current every render (the established pattern from this
  file's earlier stale-closure fix) rather than depending on the effect
  re-running.
- **Tests**: `ctest` unaffected (no `rp2040_core` changes); verified via the
  existing curl/node smoke-test pattern (2-file freestanding compile with a
  cross-file call, 2-file pico-sdk compile, stale-file-removal recompile)
  and a full browser pass (tabs, cross-file breakpoint, reload persistence).
- **Files**: `tools/lab_server/compiler.{h,cpp}`, `tools/lab_server/main.cpp`,
  `web/src/api.ts`, `web/src/App.tsx`, `web/src/components/Editor.tsx`,
  `web/src/components/FileTabs.tsx` (new)
- **Effort**: 6 hours
- **Priority**: MEDIUM (same scope addition as P10.1-P10.3)
- **Dependencies**: P10.1, P10.2, P10.3

#### P10.6: Local Web Lab - Drag-and-drop circuit editor  [DONE]
- [x] Wokwi-style component palette + visual wiring on `@xyflow/react` -
      originally listed under P10.5 as explicitly deferred at
      scope-decision time (2026-08-31, `CONTEXT.md` Decision 6); the author
      asked for it anyway and it was built incrementally, each component
      verified end-to-end against real firmware before the next was added
- [x] Components: LED, Button, Potentiometer (`Adc::set_input` via a new
      `POST /adc/:channel/external`), Buzzer (single-GPIO); ST7789 and
      ILI9341 SPI TFTs and SSD1306 I2C OLED (multi-pin virtual devices,
      dynamically attached/detached against whichever SPI/I2C instance the
      wiring implies - see `ARCHITECTURE.md` §12.6). ILI9341 shares
      ST7789's command decoder (same MIPI DBI Type C opcodes, different
      default resolution) rather than duplicating it.
- [x] Polish pass (frontend-only): non-overlapping new-node placement,
      invalid-wiring/attach-slot-conflict feedback, free-text canvas notes,
      a rotary-knob potentiometer visual, handle/selection theming - see
      `ARCHITECTURE.md` §12.6 "Polish pass"
- [x] Pico board node redrawn as a faithful physical-header diagram
      (`PicoNode.tsx`, against the board's own datasheet pinout image):
      left edge = header pins 1-20 top to bottom, right edge = pins 40-21
      top to bottom, GND interspersed exactly where the silkscreen puts
      it, VBUS/VSYS/3V3(OUT)/3V3_EN/ADC_VREF/RUN in their real slots, a
      3-pin SWD debug header (SWCLK/GND/SWDIO) below, and an "LED (GP25)"
      marker near the USB connector - all replacing the earlier version's
      simplified GP0..14-left/GP29..15-right wrap layout (still used
      as-is by `PinPanel.tsx`'s compact table, a deliberately different,
      non-physical view - see `kNumGpio`/`kLeftCount` in `picoPinout.ts`).
      GP23/24/25/29 are no longer wireable pins on the board: none of them
      is actually routed to the 40-pin header on real hardware (GP25 is
      the onboard LED only), so the previous all-30-GPIO board let a
      circuit be wired in a way no real Pico could reproduce. Each GPIO
      row also shows its I2C/SPI/UART/ADC alt-functions as small badges
      (`boardPinLabels()`, factored out of the existing `pinCapabilities()`
      tooltip logic so the two views can't drift apart) - PWM and the
      reserved-pin note are left off the board itself (matching the
      datasheet diagram) but still in `pinCapabilities()`'s tooltip.
      Verified live: reloaded a circuit saved under the old layout - every
      existing wire re-resolved to the correct pin, since `gp<N>` handle
      ids didn't change, only their on-screen position did.
- **Design**: see `ARCHITECTURE.md` §12.6
- **Files**: `src/peripherals/st7789.{h,cpp}`, `ili9341.h`, `ssd1306.{h,cpp}`,
      `src/peripherals/i2c.{h,cpp}` (`on_stop()` hook), `tools/lab_server/
      debug_session.{h,cpp}`, `main.cpp`, `web/src/components/circuit/
      PicoNode.tsx`, `web/src/picoPinout.ts`, `web/src/index.css`,
      `useCircuitWiring.ts`, `web/src/api.ts`
- **Priority**: was LOW (nice-to-have); promoted by explicit author request
- **Dependencies**: P10.1, P10.2, P10.3, P10.4

#### P10.5: Local Web Lab - Still Deferred  [NOT STARTED]
- [ ] `.debug_line`-accurate breakpoint mapping (P10.1's objdump-output
      parsing is good enough for this project's flat firmware model but not
      a real DWARF reader - heavy inlining could still produce a source
      line with no matching disassembly line)
- [ ] Named, multiple saved projects (P10.4 auto-persists one working set
      per browser, not a project picker/manager)
- [ ] Boot ROM modelling (would remove the need for P10.3's
      `PICO_RUNTIME_SKIP_INIT_BOOTROM_RESET`/compiler-impl workarounds, but
      the real RP2040 bootrom is proprietary and unredistributable - at best
      a from-scratch reimplementation of its documented behavior, a
      substantial project on its own)
- **Priority**: LOW (nice-to-have, not blocking the thesis)
- **Dependencies**: P10.1, P10.2, P10.3, P10.4

---

## Weekly Sprints

### Sprint 1 (Week 1): CPU Core
**Goal**: ARM Cortex-M0+ CPU with basic instruction set

**Tasks**:
- [ ] P1.1.1: Implement register file
- [ ] P1.1.2: Implement PC management
- [ ] P1.1.3: Implement pipeline
- [ ] P1.2.1: Load/Store instructions
- [ ] P1.2.2: Arithmetic instructions
- [ ] P1.3.1: Memory subsystem
- [ ] P1.4.1: Basic NVIC
- [ ] P1.5.1: Clock management

**Definition of Done**:
- [ ] All unit tests passing
- [ ] Can execute simple ARM programs (loops, arithmetic)
- [ ] Memory access working correctly
- [ ] Cycle counter accurate

**Review Criteria**:
- [ ] Code style consistent
- [ ] Tests have >80% coverage
- [ ] No memory leaks
- [ ] Performance acceptable

---

### Sprint 2 (Week 2): More Instructions + Interrupts
**Goal**: Complete ARMv6-M Thumb ISA (execute stage), interrupt handling

**Tasks**:
- [ ] P1.2.3: Branch instructions
- [ ] P1.2.4: Conditional execution (IT)
- [ ] P1.2.5: Special instructions
- [ ] P1.4.2: Exception entry/exit
- [ ] P1.4.3: SysTick integration
- [ ] Integration tests (CPU + memory + interrupts)

**Definition of Done**:
- [ ] All Phase 1 unit tests passing
- [ ] Can handle exceptions & ISRs
- [ ] Real ARM programs execute correctly

---

### Sprint 3 (Week 3): PIO Block Architecture
**Goal**: PIO foundation (no instructions yet)

**Tasks**:
- [ ] P2.1.1: PIO block structure
- [ ] P2.1.2: State machine registers
- [ ] P2.1.3: Program memory
- [ ] P2.5.1: Clock divider implementation
- [ ] P2.4.1: FIFO structure
- [ ] P2.1.4: SM execution loop (fetch-decode-execute)

**Definition of Done**:
- [ ] PIO architecture tests passing
- [ ] Can load programs into instruction memory
- [ ] Clock dividers working correctly

---

### Sprint 4 (Week 4): PIO Instructions (Part 1)
**Goal**: JMP, WAIT, IN, OUT instructions

**Tasks**:
- [ ] P2.2.1: JMP with all conditions
- [ ] P2.2.2: WAIT instruction
- [ ] P2.2.3: IN instruction implementation
- [ ] P2.2.4: OUT instruction implementation
- [ ] P2.5.2: Stall detection
- [ ] Unit tests for each instruction

**Definition of Done**:
- [ ] 40+ instruction tests passing
- [ ] Each instruction behavior matches datasheet
- [ ] Hardware comparison tests begun

---

### Sprint 5 (Week 5): PIO Instructions (Part 2) + Integration
**Goal**: PUSH, PULL, MOV, SET, IRQ + PIOGPIO + PIOCPU

**Tasks**:
- [ ] P2.3.1: PUSH instruction
- [ ] P2.3.2: PULL instruction
- [ ] P2.3.3: MOV instruction
- [ ] P2.3.4: SET instruction
- [ ] P2.3.5: IRQ instruction
- [ ] P2.6.1: GPIO integration (OUT, SET, IN)
- [ ] P2.7.1: CPU integration (FIFO, configuration)
- [ ] P2.8.1: Auto-push/pull
- [ ] P2.5.3: Parallel SM execution

**Definition of Done**:
- [ ] All 9 PIO instructions working
- [ ] PIO can drive GPIO
- [ ] CPU can read/write FIFO
- [ ] Multi-SM programs execute in parallel
- [ ] Hardware validation tests passing

---

### Sprint 6 (Week 6): GPIO + Timer
**Goal**: GPIO controller and Timer/PWM

**Tasks**:
- [ ] P3.1.1: GPIO controller (28 pins)
- [ ] P3.1.2: Pull-up/pull-down
- [ ] P3.1.3: Glitch filter
- [x] P3.2.1: Edge detection (IoBank0::poll)
- [ ] P3.3.1: Timer/PWM controller
- [ ] P3.3.2: PWM mode
- [ ] P3.4.1: Timer interrupts
- [ ] Integration with P2 (PIO + GPIO)

**Definition of Done**:
- [ ] GPIO tests passing
- [ ] Timer tests passing
- [ ] GPIO interrupts working
- [ ] PWM waveforms correct

---

### Sprint 7 (Week 7): Serial Interfaces (UART, SPI)
**Goal**: UART0/1 and SPI0/1 with timing accuracy

**Tasks**:
- [ ] P4.1.1: UART0 architecture
- [ ] P4.1.2: TX/RX bit-banging
- [ ] P4.1.3: Baud rate configuration
- [ ] P4.1.4: Error detection (framing, overrun)
- [ ] P4.2.1: UART1 (copy UART0)
- [ ] P4.3.1: SPI0 architecture
- [ ] P4.3.2: Clock modes (CPOL/CPHA)
- [ ] P4.4.1: SPI1 (copy SPI0)
- [x] P4.5.1: I2C0

**Definition of Done**:
- [ ] UART echo test working
- [ ] SPI loopback test working
- [ ] Bit timing accurate (±0.1%)

---

### Sprint 8 (Week 8): ADC + Advanced Interrupts
**Goal**: ADC controller and interrupt refinement

**Tasks**:
- [ ] P5.1.1: ADC controller (4 channels + temperature)
- [ ] P5.1.2: 12-bit conversion
- [ ] P5.1.3: Sample rate timing
- [ ] P5.2.1: Interrupt priority & preemption
- [ ] P5.2.2: Tail-chaining
- [ ] P5.3.1: Watchdog timer
- [ ] Hardware validation (ADC readings)

**Definition of Done**:
- [ ] ADC conversions accurate (±2%)
- [ ] Interrupt nesting working correctly
- [ ] All interrupt scenarios handled

---

### Sprint 9 (Week 9): Clock Manager
**Goal**: PLL, oscillators, clock switching

**Tasks**:
- [x] P6.1.1: XOSC & ROSC (functional models)
- [x] P6.1.2: CPU PLL (FBDIV + post-dividers -> output_hz)
- [x] P6.1.3: USB PLL (same Pll class)
- [x] P6.1.4: Lock detection
- [ ] P6.2.1: Glitchless mux
- [ ] P6.3.1: Clock dividers
- [ ] PIO clock divider refinement

**Definition of Done**:
- [ ] Clock switching glitch-free
- [ ] Frequency measurement accurate (±0.1%)
- [ ] PIO clock dividers working perfectly

---

### Sprint 10 (Week 10): Loaders & Debugging
**Goal**: ELF/UF2 loaders, GDB stub, PIO debugger

**Tasks**:
- [ ] P7.1.1: ELF loader (parsing)
- [ ] P7.1.2: ELF loader (loading into memory)
- [ ] P7.1.3: Symbol table extraction
- [x] P7.2.1: UF2 loader
- [x] P7.3.1: PIO assembler (pioasm syntax)
- [x] P7.3.2: Label resolution
- [ ] P7.4.1: GDB stub TCP server
- [ ] P7.4.2: Register read/write (RSP)
- [ ] P7.4.3: Memory access (RSP)
- [ ] P7.4.4: Execution control (continue, step)
- [ ] P7.4.5: Breakpoint management
- [x] P7.5.1: PIO debugger (per-SM inspection)
- [x] P7.6.1: Profiler

**Definition of Done**:
- [ ] Can load real RP2040 binaries
- [ ] GDB can connect and control execution
- [ ] Breakpoints work correctly
- [ ] Profiler reports reasonable numbers

---

### Sprint 11 (Week 11): Testing & Validation
**Goal**: 200+ tests, hardware validation, regression suite

**Tasks**:
- [ ] P8.1.1: Finish unit tests (all components)
- [ ] P8.2.1: Integration tests (multi-component)
- [ ] P8.3.1: Hardware validation (real Pico)
  - [ ] Blink test
  - [ ] UART echo
  - [ ] SPI loopback
  - [ ] PIO state machine
  - [ ] Oscilloscope timing checks
- [ ] P8.4.1: Regression test suite
- [ ] P8.5.1: Performance benchmarks
- [ ] Coverage report generation
- [ ] Bug fixes

**Definition of Done**:
- [ ] 200+ tests passing
- [ ] >90% code coverage
- [ ] Hardware validation passing (±1% tolerance)
- [ ] No regressions from previous sprints

---

### Sprint 12 (Week 12): Documentation & Release
**Goal**: Complete documentation, final polish, thesis-ready

**Tasks**:
- [ ] P9.1.1: Inline code documentation
- [ ] P9.1.2: API documentation (Doxygen)
- [ ] P9.2.1: Architecture guide update
- [ ] P9.2.2: Design decisions document
- [ ] P9.3.1: User guide
- [ ] P9.4.1: Code review & refactoring
- [ ] P9.4.2: Memory leak audit
- [ ] P9.4.3: Final testing run
- [ ] P9.5.1: Thesis manuscript

**Definition of Done**:
- [ ] All documentation complete
- [ ] Code ready for publication
- [ ] Thesis manuscript draft complete
- [ ] Final presentation ready

---

## Dependency Graph

```
P1.1 (Register file)
  ├─ P1.2 (ISA decoder)
  │    ├─ P7.4 (GDB stub)
  │    └─ P8.1 (Unit tests)
  ├─ P1.3 (Memory)
  │    ├─ P1.4 (NVIC)
  │    ├─ P3.1 (GPIO)
  │    ├─ P4.1 (UART)
  │    ├─ P4.3 (SPI)
  │    ├─ P5.1 (ADC)
  │    └─ P7.1 (ELF loader)
  ├─ P1.4 (NVIC)
  │    ├─ P1.5 (Clock)
  │    ├─ P3.2 (GPIO interrupts)
  │    ├─ P3.4 (Timer interrupts)
  │    ├─ P4.1 (UART interrupts)
  │    ├─ P5.2 (Advanced interrupts)
  │    └─ P5.3 (Watchdog)
  └─ P1.5 (Clock)
       ├─ P2.5 (PIO clock divider)
       ├─ P3.3 (Timer prescaler)
       ├─ P6.1 (PLL & oscillators)
       └─ P6.3 (Clock dividers)

P2.1 (PIO architecture)
  ├─ P2.2 (JMP, WAIT, IN, OUT)
  ├─ P2.3 (PUSH, PULL, MOV, SET, IRQ)
  ├─ P2.4 (FIFO)
  ├─ P2.5 (Clock divider)
  ├─ P2.6 (GPIO integration)
  ├─ P2.7 (CPU integration)
  ├─ P2.8 (Auto-push/pull)
  ├─ P7.3 (PIO assembler)
  ├─ P7.5 (PIO debugger)
  └─ P8.1 (Unit tests)

P3.1 (GPIO)  P3.2 (GPIO interrupts)  P8.1 (Unit tests)
P3.3 (Timer)  P3.4 (Timer interrupts)  P8.1 (Unit tests)
P4.1 (UART)  P4.2 (UART1)  P8.1 (Unit tests)
P4.3 (SPI)  P4.4 (SPI1)  P8.1 (Unit tests)
P5.1 (ADC)  P8.1 (Unit tests)
P5.2 (Advanced Int)  P8.1 (Unit tests)

P6.1 (PLL)  P6.2 (Mux)  P6.3 (Dividers)  P8.3 (Hardware validation)

P7.1 (ELF) ─ P8.1 (Unit tests)
P7.2 (UF2) ─ P8.1 (Unit tests)
P7.3 (PIO asm) ─ P8.1 (Unit tests)
P7.4 (GDB) ─ P8.1 (Unit tests)
P7.5 (PIO dbg) ─ P8.1 (Unit tests)

All P1-P7  P8.1 (Unit tests)
All P1-P7  P8.2 (Integration tests)
All P1-P7  P8.3 (Hardware validation)
All P1-P7  P8.4 (Regression tests)

All P1-P8  P9 (Documentation & release)
```

---

## Success Metrics

### Must-Have
- [ ]  Execute real RP2040 binaries (C/C++, ASM)
- [ ]  Cycle count accurate to ±1%
- [ ]  All 9 PIO instructions working
- [ ]  GPIO, UART, SPI functional
- [ ]  Interrupts with correct latency
- [ ]  GDB debugger integration
- [ ]  200+ automated tests
- [ ]  >90% code coverage

### Should-Have
- [ ] Timing accurate to ±10ns (GPIO, UART, SPI)
- [ ] Performance >5x real-time
- [ ] Hardware validation tests passing
- [ ] Complete documentation
- [ ] PIO assembler support

### Nice-to-Have
- [ ] GUI debugger with waveform display
- [ ] Python scripting API
- [ ] I2C full support
- [ ] ADC realistic values

---

## Timeline Summary

| Week | Phase | Duration | Key Milestones |
|------|-------|----------|---|
| 1-2 | CPU + Memory | 2w | M1.1, M1.2, M1.3 |
| 3-5 | PIO | 3w | M2.1-M2.7 (CRITICAL) |
| 6 | GPIO + Timer | 1w | M3.1, M3.2 |
| 7 | UART + SPI | 1w | M4.1, M4.2 |
| 8 | ADC + Interrupts | 1w | M5.1, M5.2 |
| 9 | Clock Mgr | 1w | M6.1, M6.2 |
| 10 | Loaders + Debug | 1w | M7.1-M7.3 |
| 11 | Testing | 1w | M8.1-M8.3 |
| 12 | Docs + Polish | 1w | M9.1-M9.3 |
| **TOTAL** | | **12 weeks** | **9 milestones** |

---

## Risk Mitigation

### Risk: PIO complexity underestimated
- **Mitigation**: Sprint 4-5 focus, early hardware validation
- **Contingency**: Reduce I2C scope, defer to Phase 2

### Risk: Timing accuracy proving difficult
- **Mitigation**: Frequent hardware comparison tests
- **Contingency**: Accept ±5% tolerance instead of ±1%

### Risk: Hardware not available for validation
- **Mitigation**: Start with QEMU comparison, order Picos early
- **Contingency**: Use simulation-only validation metrics

### Risk: Debugging too complex
- **Mitigation**: Start with simple GDB stub, iterate
- **Contingency**: Focus on CLI interface, skip GUI

### Risk: Time management
- **Mitigation**: Strict sprint planning, daily 30min standup
- **Contingency**: Reduce testing depth, deprioritize I2C/ADC

---

## Contact & Escalation

- **Daily Standup**: 10 minutes (if in team)
- **Weekly Review**: Friday, 30 minutes
- **Blocker Escalation**: ASAP (via email/chat)

---

**Last Updated**: 2024-08-28
**Status**: Ready for Phase 1 launch
**Next Review**: End of Sprint 1
