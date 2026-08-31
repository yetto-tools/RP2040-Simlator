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
- [ ] Exact instruction-interleave timing (round-robin is a simplification)
- **Tests**: `tests/unit/test_multicore.cpp` (5 cases)
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

#### P3.3: PWM Controller (0x40050000)  [IN PROGRESS]
- [x] `Pwm` BusPeripheral (`src/peripherals/pwm.{h,cpp}`), 8 slices x 2 channels
- [x] Per-slice CSR/DIV/CTR/CC/TOP; global EN, INTR/INTE/INTF/INTS
- [x] Free-running fractional divider, count-up + PH_CORRECT up/down, TOP wrap
- [x] CC compare -> GPIO level (slice N ch A/B -> GPIO 2N / 2N+1 [+16]),
      A_INV / B_INV; new `Gpio::kPwm` driver + FUNCSEL 4
- [x] Wrap interrupt -> PWM_IRQ_WRAP (IRQ4); wired into Simulator on_cycles()
- [ ] B-pin gated/edge DIVMODEs, phase advance/retard, DMA
- **Tests**: `tests/unit/test_pwm.cpp` (6 cases)
- **Design**: RP2040 datasheet 4.5

#### P3.3-OLD: PWM Controller (original checklist)
- [ ] 4 timer slices
- [ ] 2 channels per slice (A, B)
- [ ] 16-bit counter
- [ ] Prescaler (divide by 2^N)
- [ ] Period & compare registers
- [ ] PWM mode (with duty cycle)
- [ ] One-shot mode
- [ ] Auto-reload
- **Tests**: 20+ timer tests
- **Effort**: 20 hours
- **Priority**: HIGH
- **Dependencies**: P1.3

#### P3.4: Timer Interrupts
- [ ] Compare match interrupt
- [ ] Counter overflow interrupt
- [ ] Period reached interrupt
- [ ] NVIC integration
- **Tests**: 10+ interrupt tests
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P3.3, P1.4

---

### PHASE 4: UART + SPI (Week 7)

#### P4.1: UART0 / UART1 Controller (PL011)  [IN PROGRESS]
- [x] `Uart` BusPeripheral (`src/peripherals/uart.{h,cpp}`) @ 0x40034000 /
      0x40038000; UART0_IRQ = IRQ20, UART1_IRQ = IRQ21
- [x] UARTDR TX -> output log + on_transmit() callback; RX FIFO (32-deep)
      fed by feed(); UARTFR TXFE/RXFE/RXFF
- [x] UARTCR (UARTEN/TXE/RXE), UARTLCR_H store, UARTIMSC / UARTRIS / UARTMIS /
      UARTICR; RXRIS + TXRIS level-driven onto the NVIC
- [x] Wired into Simulator (both instances)
- [ ] Baud-rate timing (bit-accurate TX/RX), framing/parity/overrun errors,
      break detection, DMA request lines
- **Tests**: `tests/unit/test_uart.cpp` (5 cases)
- **Effort**: 30 hours
- **Priority**: HIGH
- **Dependencies**: P1.3, P1.4

#### P4.2: UART1 Controller  [DONE]
- [x] Second `Uart` instance (see P4.1); UART1_IRQ = IRQ21

#### P4.3: SPI0 / SPI1 Controller (PL022)  [IN PROGRESS]
- [x] `Spi` BusPeripheral (`src/peripherals/spi.{h,cpp}`) @ 0x4003C000 /
      0x40040000; SPI0_IRQ = IRQ18, SPI1_IRQ = IRQ19
- [x] SSPDR write = full-duplex byte transfer: MOSI -> output log +
      on_transfer() callback; MISO from callback / feed() queue / 0xFF idle;
      internal loopback (CR1.LBM)
- [x] SSPSR (TFE/TNF always, RNE/RFF from the 8-deep RX FIFO), SSPCR0/CR1,
      SSPIMSC / SSPRIS / SSPMIS; RXRIS + TXRIS -> NVIC
- [ ] CPOL/CPHA modes, frame size 4-16, bit-rate timing, chip-select lines
- **Tests**: `tests/unit/test_spi.cpp` (7 cases)
- **Design**: RP2040 datasheet 4.4

#### P4.4: SPI1 Controller
- [ ] Identical to SPI0
- **Tests**: 10+ differential tests
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P4.3

#### P4.5 / P4.6: I2C0 / I2C1 Controller (DW_apb_i2c)  [IN PROGRESS]
- [x] `I2c` BusPeripheral (`src/peripherals/i2c.{h,cpp}`) @ 0x40044000 /
      0x40048000; I2C0_IRQ = IRQ23, I2C1_IRQ = IRQ24
- [x] Master-mode functional model: IC_ENABLE, IC_TAR, IC_DATA_CMD
      (write byte / read command with STOP bit) against a registered
      `set_slave(addr7, fn)` callback; 16-deep RX FIFO
- [x] IC_STATUS (TFNF/TFE/RFNE/RFF), IC_RXFLR, IC_RAW_INTR_STAT / IC_INTR_MASK
      / IC_INTR_STAT (RX_FULL, TX_ABRT, STOP_DET), IC_TX_ABRT_SOURCE,
      IC_CLR_* -> I2C IRQ
- [x] Address-NACK and TX-data-NACK aborts
- [ ] Bus-level timing, clock stretching, 10-bit addressing, slave mode,
      arbitration loss, DMA
- **Tests**: `tests/unit/test_i2c.cpp` (5 cases)
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
      (0x420..0x42C); a peripheral DREQ is approximated as one element every
      `dreq_divisor()` clocks (no FIFO-level handshake). TRANS_COUNT reads the
      live remaining count while BUSY.
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

#### P5.1: ADC Controller  [IN PROGRESS]
- [x] `Adc` BusPeripheral (`src/peripherals/adc.{h,cpp}`) @ 0x4004C000
- [x] 5 inputs (GPIO26-29 + temp sensor, gated by TS_EN); 12-bit RESULT;
      test bench sets raw codes via `set_input()`
- [x] START_ONCE (immediate), START_MANY free-running paced by the 48 MHz
      ADC clock from `on_cycles()` (96 + DIV_INT clocks/sample), RROBIN
- [x] 4-entry sample FIFO: FCS.EN/SHIFT, LEVEL, EMPTY/FULL, OVER/UNDER (w1c)
- [x] INTR / INTE / INTF / INTS with THRESH -> ADC_IRQ_FIFO (IRQ22)
- [ ] Bit-accurate SAR timing detail; DMA DREQ line; input from real GPIO
      pad voltage
- **Tests**: `tests/unit/test_adc.cpp` (8 cases)
- **Priority**: MEDIUM
- **Design**: RP2040 datasheet 4.9

#### P5.2: Advanced Interrupt Handling
- [ ] Interrupt priority (0-3)
- [ ] Preemption (higher priority interrupts higher-priority)
- [ ] Pending flag management
- [ ] Active flag management
- [ ] Tail-chaining optimization
- [ ] Interrupt stacking (8-word frame)
- **Tests**: 20+ interrupt scenarios
- **Effort**: 20 hours
- **Priority**: HIGH
- **Dependencies**: P1.4

#### P5.3: Watchdog + RESETS  [IN PROGRESS]
- [x] `Watchdog` (`src/peripherals/watchdog.{h,cpp}`) @ 0x40058000:
      LOAD/CTRL down-counter (decrements by 2 per us per the HW quirk),
      feed via LOAD, ENABLE, CTRL.TRIGGER force-reset, REASON (TIMER/FORCE),
      SCRATCH0-7 (survive reset), TICK. Timeout -> Simulator resets the CPU.
- [x] `Resets` (`src/peripherals/resets.{h,cpp}`) @ 0x4000C000: RESET / WDSEL
      / RESET_DONE (= ~RESET, so pico-sdk unreset_block_wait returns) with the
      +0x1000/2000/3000 XOR/SET/CLR atomic aliases
- [ ] Pause-on-debug; watchdog-scoped resets via WDSEL
- **Tests**: `tests/unit/test_watchdog.cpp` (5 cases)
- **Design**: RP2040 datasheet 4.7, 2.14

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

#### P7.1: ELF Loader  [IN PROGRESS - pulled forward for CPU validation]
- [x] Parse + validate ELF32 header (magic, ELFCLASS32, LSB, ET_EXEC/DYN, EM_ARM)
- [x] Read program headers, load PT_LOAD segments at p_paddr (LMA) via backdoor
- [x] Zero-fill the BSS tail (p_memsz > p_filesz); bounds-check every range
- [x] Entry point (e_entry) reported; lowest/highest loaded address
- [x] `load_elf_file()` convenience; CLI `rp2040-sim <firmware.elf>` driver
- [ ] Symbol table / section names (for the debugger / traces)
- **Tests**: `tests/unit/test_elf_loader.cpp` (synthetic ELFs) +
      `tests/integration/test_firmware.cpp` (real arm-none-eabi-gcc -O2 image
      built by CMake, run end-to-end through the simulator)
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
- [ ] Extension-tag / MD5-region parsing (not emitted by elf2uf2; deferred)
- **Tests**: `tests/unit/test_uf2_loader.cpp` (synthetic blocks: two-block image,
      not-main-flash skip, no-family block, 7 malformed-stream subcases, run on
      CPU) + `tests/integration/test_firmware.cpp` (real sum.elf repackaged as
      UF2 and run end-to-end)
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
- [ ] Watchpoints ($Z2-4), $qXfer:features:read (target.xml), per-core
      thread switching, run-length-encoded replies, live arm-none-eabi-gdb
      integration test
- **Tests**: `tests/unit/test_gdb_stub.cpp` (7 cases)
- **Design**: GDB RSP spec; ARM m-profile register layout
- **Files**: `src/debuggers/gdb_stub.{h,cpp}`, `src/main.cpp`
  - [ ] $Z0 (remove software)
- [ ] Watchpoint support (optional)
- **Tests**: 25+ GDB scenarios
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
