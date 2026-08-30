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

#### P1.1: ARM Cortex-M0+ CPU Architecture  [IN PROGRESS]
- [x] Register file (R0-R15, banked MSP/PSP, APSR/IPSR/EPSR, CONTROL, PRIMASK)
- [x] Program counter management (raw PC store, bit-0 masking, advance)
- [x] Condition code logic (N, Z, C, V) + full ARMv6-M ConditionPassed()
- [ ] Pipeline simulation (3 stages) - needs the decoder (P1.2)
- [ ] Exception vector table - moved to P1.4 (NVIC / exceptions.h)
- **Tests**: 20+ unit tests -> `tests/unit/test_registers.cpp` (17 cases)
- **Effort**: 40 hours
- **Priority**: CRITICAL
- **Design**: ARCHITECTURE.md 1.1-1.2, 1.5 (rewritten for ARMv6-M, not Thumb-2)
- **Files**: `src/core/registers.{h,cpp}`

#### P1.2: Thumb-2 Instruction Decoder
- [ ] Load/Store instructions (LDR, STR, LDRB, STRB, etc.)
- [ ] Arithmetic (ADD, SUB, MUL, CMP, etc.)
- [ ] Logic operations (AND, OR, XOR, LSL, LSR, etc.)
- [ ] Branch instructions (B, BL, BX, Bcc)
- [ ] Conditional execution (IT, CBZ, CBNZ)
- [ ] Shift & rotate (LSL, LSR, ASR, ROR)
- **Tests**: 30+ per instruction category
- **Effort**: 60 hours
- **Priority**: CRITICAL
- **Dependencies**: P1.1

#### P1.3: Memory Subsystem  [IN PROGRESS]
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

#### P1.4: Basic Interrupt Controller (NVIC)
- [ ] Vector table lookup
- [ ] Exception entry (save stack frame)
- [ ] ISR dispatch
- [ ] Return from exception
- [ ] SysTick integration
- **Tests**: 10+ interrupt scenarios
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P1.1, P1.3

#### P1.5: Clock Management (Basic)
- [ ] CPU clock source (125 MHz)
- [ ] Cycle counter
- [ ] Timing primitives
- **Tests**: 5+ tests
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P1.1

---

### PHASE 2: PIO (Programmable I/O) (Weeks 3-5)

#### P2.1: PIO Block Architecture
- [ ] 2 PIO blocks (PIO0, PIO1)
- [ ] 4 State Machines per block
- [ ] Shared program memory (32 instructions)
- [ ] State machine registers (X, Y, OSR, ISR, PC)
- [ ] Clock divider (1-65536)
- **Tests**: 10+ architecture tests
- **Effort**: 30 hours
- **Priority**: CRITICAL (40% of Phase 2)
- **Dependencies**: P1.1, P1.3

#### P2.2: PIO ISA - JMP, WAIT, IN, OUT
- [ ] JMP instruction with conditions
  - [ ] always, !X, X--, !Y, Y--, X!=Y, PIN, !PIN
- [ ] WAIT instruction (GPIO level, IRQ)
- [ ] IN instruction (PINS, X, Y, NULL, ISR)
  - [ ] Bit shifting
  - [ ] ISR accumulation
- [ ] OUT instruction (PINS, X, Y, NULL, PC, ISR)
  - [ ] Bit shifting from OSR
  - [ ] Destination routing
- **Tests**: 40+ (one per condition/source)
- **Effort**: 45 hours
- **Priority**: CRITICAL
- **Dependencies**: P2.1

#### P2.3: PIO ISA - PUSH, PULL, MOV, SET, IRQ
- [ ] PUSH instruction
  - [ ] iffull flag
  - [ ] block/non-block
  - [ ] FIFO push
- [ ] PULL instruction
  - [ ] ifempty flag
  - [ ] block/non-block
  - [ ] FIFO pull
- [ ] MOV instruction
  - [ ] Register moves
  - [ ] Operations (none, invert, bitrev, shift)
- [ ] SET instruction (PINS, X, Y)
- [ ] IRQ instruction
  - [ ] Set/clear IRQ
  - [ ] Wait mode
- **Tests**: 30+ (each instruction variant)
- **Effort**: 35 hours
- **Priority**: CRITICAL
- **Dependencies**: P2.1, P2.2

#### P2.4: FIFO Management
- [ ] TX FIFO (CPU  SM)
  - [ ] 4-deep queue per SM
  - [ ] Full flag
  - [ ] Empty flag
  - [ ] Level counting
- [ ] RX FIFO (SM  CPU)
  - [ ] 4-deep queue per SM
  - [ ] Data pushing
  - [ ] CPU reading
- [ ] Flow control (blocking vs non-blocking)
- [ ] Status registers
- **Tests**: 15+ FIFO scenarios
- **Effort**: 20 hours
- **Priority**: CRITICAL
- **Dependencies**: P2.1

#### P2.5: Clock Divider & Execution Timing
- [ ] Divide-by-N logic (1-65536)
- [ ] Fractional clock divider
- [ ] Stall detection (when FIFO blocks)
- [ ] Parallel SM execution
- **Tests**: 20+ timing tests
- **Effort**: 15 hours
- **Priority**: CRITICAL (timing is key)
- **Dependencies**: P2.1, P2.2, P2.3

#### P2.6: PIO  GPIO Integration
- [ ] OUT driving GPIO pins
- [ ] SET driving GPIO pins
- [ ] SIDESET functionality
- [ ] IN reading GPIO pins
- [ ] Pin override logic
- **Tests**: 15+ GPIO interaction tests
- **Effort**: 15 hours
- **Priority**: CRITICAL
- **Dependencies**: P2.1, P2.2, P3.1

#### P2.7: PIO  CPU Integration
- [ ] TX FIFO write (CPU  PIO)
- [ ] RX FIFO read (PIO  CPU)
- [ ] Configuration registers (CTRL, EXECCTRL, PINCTRL)
- [ ] Program loading (INSTR_MEM)
- [ ] Status polling
- **Tests**: 20+ CPU-PIO interaction
- **Effort**: 20 hours
- **Priority**: CRITICAL
- **Dependencies**: P2.1-P2.6

#### P2.8: Auto-Push & Auto-Pull
- [ ] Auto-push when ISR full
- [ ] Auto-pull when OSR empty
- [ ] Configurable thresholds
- [ ] Edge cases (mid-instruction)
- **Tests**: 10+ auto-push/pull tests
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P2.3, P2.4

---

### PHASE 3: GPIO + Timer (Week 6)

#### P3.1: GPIO Controller
- [ ] 28 GPIO pins (GPIO0-GPIO27)
- [ ] Input/Output mode
- [ ] Pull-up/pull-down
- [ ] Slew rate control
- [ ] Drive strength (2mA, 4mA, 8mA, 12mA)
- [ ] Output enable override
- [ ] Glitch filter (5-cycle delay)
- **Tests**: 30+ GPIO tests
- **Effort**: 25 hours
- **Priority**: HIGH
- **Dependencies**: P1.3

#### P3.2: GPIO Interrupts
- [ ] Edge detection (low, high, rising, falling)
- [ ] Per-pin interrupt enable
- [ ] Status register
- [ ] NVIC integration
- **Tests**: 15+ edge detection tests
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P3.1, P1.4

#### P3.3: Timer/PWM Controller
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

#### P4.1: UART0 Controller
- [ ] Configurable baud rate (300-3M)
- [ ] Data bits (5-8)
- [ ] Stop bits (1-2)
- [ ] Parity (none, even, odd)
- [ ] TX FIFO (16 deep)
- [ ] RX FIFO (16 deep)
- [ ] Bit-accurate transmission timing
- [ ] Bit-accurate reception timing
- [ ] Line break detection
- [ ] Framing error detection
- [ ] Overrun detection
- **Tests**: 30+ UART tests
- **Effort**: 30 hours
- **Priority**: HIGH
- **Dependencies**: P1.3, P1.4

#### P4.2: UART1 Controller
- [ ] Identical to UART0
- **Tests**: 10+ (differential tests vs UART0)
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P4.1

#### P4.3: SPI0 Controller
- [ ] Master/Slave mode
- [ ] Clock polarity (CPOL)
- [ ] Clock phase (CPHA) - modes 0-3
- [ ] Frame size (4-16 bits)
- [ ] Baud rate prescaler
- [ ] TX FIFO (8 deep)
- [ ] RX FIFO (8 deep)
- [ ] Chip select control
- [ ] Loopback mode
- [ ] Bit-accurate transmission timing
- **Tests**: 25+ SPI tests
- **Effort**: 25 hours
- **Priority**: HIGH
- **Dependencies**: P1.3

#### P4.4: SPI1 Controller
- [ ] Identical to SPI0
- **Tests**: 10+ differential tests
- **Effort**: 10 hours
- **Priority**: HIGH
- **Dependencies**: P4.3

#### P4.5: I2C0 Controller
- [ ] Master/Slave mode
- [ ] Standard (100 kHz) and Fast (400 kHz)
- [ ] Open-drain SDA/SCL
- [ ] START condition
- [ ] STOP condition
- [ ] Repeated START
- [ ] Clock stretching (slave holds SCL low)
- [ ] 7-bit addressing
- [ ] ACK/NACK detection
- [ ] Arbitration loss
- **Tests**: 20+ I2C tests
- **Effort**: 20 hours
- **Priority**: MEDIUM
- **Dependencies**: P1.3

#### P4.6: I2C1 Controller
- [ ] Identical to I2C0
- **Tests**: 10+ differential tests
- **Effort**: 10 hours
- **Priority**: MEDIUM
- **Dependencies**: P4.5

---

### PHASE 5: ADC + Advanced Interrupts (Week 8)

#### P5.1: ADC Controller
- [ ] 4 GPIO channels (GPIO26-GPIO29)
- [ ] Temperature sensor (VBE)
- [ ] 12-bit conversion
- [ ] Configurable sample rate
- [ ] Free-running mode
- [ ] Single-shot mode
- [ ] Channel selection
- [ ] Round-robin mode
- [ ] Conversion timing (~2 µs per sample)
- **Tests**: 15+ ADC tests
- **Effort**: 15 hours
- **Priority**: MEDIUM
- **Dependencies**: P1.3, P1.4

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

#### P5.3: Watchdog Timer
- [ ] Configurable timeout
- [ ] Watchdog kick (reset counter)
- [ ] System reset on timeout
- [ ] Pause on debug
- **Tests**: 10+ watchdog tests
- **Effort**: 10 hours
- **Priority**: MEDIUM
- **Dependencies**: P1.3, P1.4

#### P5.4: Real-Time Clock (RTC)
- [ ] Date/time registers
- [ ] Alarm functionality
- [ ] (Optional for Phase 1)
- **Effort**: 10 hours
- **Priority**: LOW
- **Dependencies**: P1.3

---

### PHASE 6: Clock Manager (Week 9)

#### P6.1: Oscillators & PLL
- [ ] XOSC (12 MHz crystal)
- [ ] ROSC (ring oscillator, configurable)
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

#### P7.1: ELF Loader
- [ ] Parse ELF header (magic, architecture)
- [ ] Verify ARM 32-bit
- [ ] Read program headers
- [ ] Load segments into memory
- [ ] Symbol table extraction (for debugging)
- [ ] Entry point determination
- [ ] Section mapping
- **Tests**: 10+ ELF files
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P1.3

#### P7.2: UF2 Loader
- [ ] Parse UF2 block format
- [ ] Flash image validation
- [ ] Checksum verification
- [ ] Boot mode detection
- **Tests**: 10+ UF2 files
- **Effort**: 10 hours
- **Priority**: MEDIUM
- **Dependencies**: P1.3

#### P7.3: PIO Assembler
- [ ] Support pioasm syntax
- [ ] Label resolution
- [ ] Expression evaluation
- [ ] Instruction encoding
- [ ] .sideset directives
- [ ] .define constants
- [ ] Error reporting
- **Tests**: 20+ assembly programs
- **Effort**: 25 hours
- **Priority**: HIGH
- **Dependencies**: P2.1, P2.2, P2.3

#### P7.4: GDB Stub (Remote Serial Protocol)
- [ ] TCP server (port 3333)
- [ ] RSP packet parsing
- [ ] Register read/write
  - [ ] $g (read all)
  - [ ] $p (read one)
  - [ ] $G (write all)
  - [ ] $P (write one)
- [ ] Memory read/write
  - [ ] $m addr,size
  - [ ] $M addr,size:data
- [ ] Execution control
  - [ ] $c (continue)
  - [ ] $s (step)
  - [ ] $C (continue with signal)
  - [ ] $S (step with signal)
- [ ] Breakpoint management
  - [ ] $z0 (insert software)
  - [ ] $Z0 (remove software)
- [ ] Watchpoint support (optional)
- **Tests**: 25+ GDB scenarios
- **Effort**: 30 hours
- **Priority**: HIGH
- **Dependencies**: P1.1, P1.3

#### P7.5: PIO Debugger
- [ ] Per-SM breakpoints
- [ ] PC control per SM
- [ ] Register inspection (X, Y, OSR, ISR)
- [ ] FIFO state inspection
- [ ] Instruction trace
- [ ] Cycle counting
- **Tests**: 15+ PIO debugging scenarios
- **Effort**: 15 hours
- **Priority**: HIGH
- **Dependencies**: P2.1, P7.4

#### P7.6: Profiler & Performance Analysis
- [ ] Cycle counter
- [ ] Per-instruction timing
- [ ] Memory access timeline
- [ ] Interrupt latency measurement
- [ ] Performance report generation
- **Tests**: 10+ profiling tests
- **Effort**: 15 hours
- **Priority**: MEDIUM
- **Dependencies**: P1.1, P7.4

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
**Goal**: Complete ARM Thumb-2 ISA, interrupt handling

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
- [ ] P3.2.1: Edge detection
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
- [ ] P4.5.1: I2C0 (optional, defer to Phase 2)

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
- [ ] P6.1.1: XOSC & ROSC
- [ ] P6.1.2: CPU PLL (1200 MHz  125 MHz)
- [ ] P6.1.3: USB PLL (480 MHz  48 MHz)
- [ ] P6.1.4: Lock detection
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
- [ ] P7.2.1: UF2 loader
- [ ] P7.3.1: PIO assembler (pioasm syntax)
- [ ] P7.3.2: Label resolution
- [ ] P7.4.1: GDB stub TCP server
- [ ] P7.4.2: Register read/write (RSP)
- [ ] P7.4.3: Memory access (RSP)
- [ ] P7.4.4: Execution control (continue, step)
- [ ] P7.4.5: Breakpoint management
- [ ] P7.5.1: PIO debugger (per-SM inspection)
- [ ] P7.6.1: Profiler

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
