# CLAUDE.md - Project Context & Instructions for AI-Assisted Development

> This document is designed for Claude or other AI assistants to understand the full context and constraints of this thesis project. Read this before generating code or architecture.

## Project Essence

**What**: Build a scientifically rigorous, cycle-accurate simulator of the RP2040 microcontroller suitable for thesis publication.

**Why**: The RP2040's Programmable I/O (PIO) subsystem is complex, parallel, and has poor support in existing simulators (QEMU). This project fills that gap.

**Who**: Single developer (thesis author) implementing over 12 weeks full-time.

**Success Criteria**:
- Execute real RP2040 firmware (C/C++, ASM, MicroPython)
- Cycle-accurate to within ±1% of hardware
- All peripherals functional (GPIO, UART, SPI, I2C, Timer, ADC)
- PIO with 8 independent state machines running in parallel
- GDB integration for debugging
- 200+ automated tests with >90% coverage
- Reproducible, deterministic execution traces
- Publishable as thesis

---

## Critical Constraints (READ CAREFULLY)

### 1. NO SHORTCUTS FOR THESIS
-  DON'T suggest "simplified PIO" or "mock implementation"
-  DO implement exact hardware behavior, even if complex
-  DON'T skip timing details
-  DO make cycle counts matter

### 2. HARDWARE FIDELITY IS NON-NEGOTIABLE
When implementing a feature, ask:
- Does this match the RP2040 datasheet **exactly**?
- Would a hw engineer reviewing this accept it?
- Can I write a test that fails if behavior changes?

### 3. PIO IS CRITICAL (40% of complexity)
- PIO has 8 state machines running **in parallel** with CPU
- Each SM is independent, with own program counter
- All 4 SMs in a block share a FIFO
- Timing must be cycle-accurate
- This is not a peripheral like GPIO—it's a co-processor

### 4. DOCUMENTATION FIRST
- Write design doc before code
- Add test cases in parallel with implementation
- Hardware validation traces are mandatory
- Thesis quality: peer-reviewable

### 5. NO EXTERNAL DEPENDENCIES FOR CORE
- Core simulator must be self-contained (C++17 minimum)
- Optional: Qt/wxWidgets for GUI (separate from core)
- Must compile with arm-none-eabi-gcc and gdb

---

## How to Think About This Project

### The Execution Model

```
Every clock cycle:
1. Each CPU core (2x Cortex-M0+, ARMv6-M) executes ONE instruction (Thumb)
2. PIO Block 0: Each of 4 SMs executes ONE instruction (PIO ISA)
3. PIO Block 1: Each of 4 SMs executes ONE instruction (PIO ISA)
4. Peripherals update (GPIO, UART, Timer, ADC)
5. Interrupts are checked and dispatched if needed
6. Clock advanced by 1 cycle
7. Repeat until program halts or breakpoint hit

This is NOT sequential—PIO runs in parallel with CPU.
```

### State Machine Concept

Don't think of PIO as a "GPIO controller". Think of it as:
- **4 independent co-processors** per PIO block
- Each can be executing a completely different program
- They communicate through a shared FIFO
- They have direct access to GPIO pins
- They can interrupt the CPU

Example: While CPU is running C code, SM0 might be reading SPI data, SM1 generating PWM, SM2 counting pulses. All simultaneously.

### Memory Model

```
CPU reads/writes memory  triggers update in all systems

Example: CPU writes to GPIO output register
 GPIO driver updates pin levels
 PIO can read the pin via IN instruction
 External circuit sees the pin change (simulated)
```

---

## What to Ask Before Writing Code

When asked to implement something, I should confirm:

### 1. **Fidelity Level**
"Should this match the datasheet exactly, or is behavioral approximation ok?"
- **Answer**: Always match exactly for thesis. Only approximation for UI.

### 2. **Timing**
"How important is cycle-accurate timing for this component?"
- **ARM CPU**: Critical (±1 cycle)
- **PIO**: Critical (±0 cycles, always exact)
- **GPIO**: Important (±10ns acceptable)
- **UART**: Important (bit-accurate)
- **ADC**: Moderate (conversion timing realistic)

### 3. **Test Cases**
"What are the test vectors for this?"
- Ask for examples BEFORE implementing
- Build test-driven from datasheet

### 4. **Hardware Validation**
"Do we have a trace from real hardware?"
- If yes: compare against it
- If no: need to capture one before claiming accuracy

---

## Development Workflow

### For Each Component:

1. **Read Datasheet** (RP2040 PDF, specific section)
2. **Write Design Doc** (explain architecture in DESIGN.md)
3. **Design API** (headers in include/)
4. **Write Tests** (before implementation)
5. **Implement** (src/)
6. **Validate** (run against hardware)
7. **Document** (update ARCHITECTURE.md)
8. **Integrate** (connect to simulator main loop)

### Example: Implementing GPIO

```cpp
// Step 1: Read datasheet section 2.19 "GPIO"
//  Learn: 28 pins, pull-up/pull-down, edge detect, override

// Step 2: Add to DESIGN.md
//  Why this design choice? What's the API?

// Step 3: Design header (include/gpio.h)
//  class GPIO { public: set_level(), get_level(), ... }

// Step 4: Write tests (tests/unit/test_gpio.cpp)
//  TEST(GPIO, SetOutputHigh) { ... }
//  TEST(GPIO, EdgeDetectRising) { ... }

// Step 5: Implement (src/peripherals/gpio.cpp)
//  Make tests pass

// Step 6: Validate
//  Capture real hardware trace
//  Compare: sim output vs hw output
//  Assert: ±10ns accuracy

// Step 7: Document (ARCHITECTURE.md)
//  Explain implementation choices
//  Note any workarounds or limitations

// Step 8: Integrate
//  Wire GPIO into simulator main loop
//  Test with other components (CPU + GPIO, PIO + GPIO)
```

---

## Common Mistakes to Avoid

### Mistake 1: "We can simplify PIO"
> "State machines are complex. Can we just mock a few instructions?"

**NO.** PIO is the thesis's main contribution. Invest the 40% of effort it deserves.

### Mistake 2: "Timing doesn't matter"
> "We'll count cycles approximately, not exactly."

**NO.** Any timing inaccuracy invalidates the entire simulator for embedded systems research.

### Mistake 3: "Testing can come later"
> "Let's build features first, test afterward."

**NO.** Unit tests should exist before implementation. This is test-driven development.

### Mistake 4: "Ignore edge cases"
> "This instruction is rarely used, skip it."

**NO.** Coverage must be >90%. Every instruction matters.

### Mistake 5: "Don't validate against hardware"
> "If it passes our tests, it must be right."

**NO.** Hardware comparison is mandatory for thesis credibility.

---

## Current Phase: Planning

**Status**: Architecture & design phase.
**Deliverables**: Design docs (this file, ARCHITECTURE.md, BACKLOG.md)
**Next**: Implementation phase starts after docs are finalized.

### What I Should Do Now:

1.  Read ALL documentation (README.md, DESIGN.md, ARCHITECTURE.md, BACKLOG.md)
2.  Understand the execution model (CPU + PIO parallel)
3.  Understand the constraint (100% fidelity, no shortcuts)
4.  Ask clarifying questions about scope
5.  Be ready to guide implementation phase

### What I Should NOT Do Now:

-  Generate code without design approval
-  Suggest simplifications to the scope
-  Skip documentation
-  Assume existing simulator code is adequate

---

## Key Concepts You Must Understand

### 1. ARM Cortex-M0+ CPU (2x on the RP2040)
- 32-bit RISC processor, **ARMv6-M** architecture
- ISA: the full 16-bit Thumb set + a 6-instruction slice of Thumb-2's 32-bit
  encodings (BL, MSR, MRS, DSB, DMB, ISB). NOT the full Thumb-2 of ARMv7-M
  (M3/M4) - no 32-bit data processing, IT, LDRD, UMULL, etc.
- The 16-bit data ops are the flag-setting forms: MOVS, ADDS, SUBS, ANDS, LSLS,
  MULS, ... (no IT block, so no non-S 16-bit forms except high-reg MOV/ADD, ADR, ADD/SUB SP)
- NOT present: IT, CBZ/CBNZ, LDRD/STRD, LDM.W/STM.W, UMULL/SMULL, SDIV/UDIV,
  MOVW/MOVT, the Q and GE flags, 32-bit data processing
- 2-stage pipeline (Fetch, Decode+Execute) - Cortex-M0+, not the M0's 3;
  PC still reads architecturally as (instr addr + 4)
- 16 registers (R0-R15; R13=SP banked MSP/PSP, R14=LR, R15=PC)
- APSR: N, Z, C, V only. CONTROL (nPRIV, SPSEL), PRIMASK. VTOR present on M0+.
- Exception handling with vector table (NVIC); 26 external IRQ on RP2040, 2 priority bits

### 2. PIO (Programmable I/O)
- Co-processor with 8 state machines (4 per block)
- ISA: 9 instruction types (JMP, WAIT, IN, OUT, PUSH, PULL, MOV, SET, IRQ)
- Program memory: 32 × 16-bit instructions per block
- FIFO: 4-deep per SM (TX from CPU, RX to CPU)
- Clock divider: each SM can run at different frequency
- Direct GPIO access: OUT, SET, IN, SIDESET instructions

### 3. Memory Map
```
0x00000000 - 0x00003FFF    ROM (bootloader, 16KB)
0x10000000 - 0x101FFFFF    Flash (program, 2MB)
0x20000000 - 0x20041FFF    SRAM (data/stack, 264KB)
0x40000000 - 0x40FFFFFF    Register space (peripherals)
```

### 4. Peripherals (in register space)
- GPIO: 0x40014000
- Timer: 0x40054000
- UART0: 0x40034000, UART1: 0x40038000
- SPI0: 0x4003C000, SPI1: 0x40040000
- I2C0: 0x40044000, I2C1: 0x40048000
- ADC: 0x4004C000
- PIO0: 0x50200000, PIO1: 0x50300000

### 5. Interrupts
- NVIC (Nested Vectored Interrupt Controller)
- 32 interrupt sources
- Priority levels 0-3
- Each peripheral can generate interrupts
- PIO can generate 8 interrupts per block

---

## How to Review Code for This Project

When I generate code, you should check:

### Code Quality
- [ ] Matches RP2040 datasheet exactly
- [ ] Cycle counting is accurate
- [ ] Memory access patterns are correct
- [ ] Timing constants match hardware

### Testing
- [ ] Unit tests exist before implementation
- [ ] >90% code coverage targeted
- [ ] Edge cases tested
- [ ] Hardware comparison tests included

### Documentation
- [ ] Design decisions explained
- [ ] API is clear and well-commented
- [ ] Limitations documented
- [ ] Datasheet references included

### Architecture
- [ ] Follows project structure
- [ ] Doesn't introduce external dependencies
- [ ] Integrates cleanly with other components
- [ ] No shortcuts or simplified implementations

---

## How to Communicate with Me (Claude)

### Good Requests
 "Implement the ARMv6-M ADDS (register) instruction (ARM ARM A6.7.4). Which flags does it write - N,Z,C,V all?"
 "Review this PIO state machine implementation. Does it handle the FIFO blocking correctly?"
 "What tests should we write for SPI mode 0-3 mode switching?"

### Vague Requests
 "Make the simulator faster"
 "Implement debugging"
 "Add PIO support"

 Instead, be specific:
- "Optimize memory access by implementing caching"
- "Add GDB stub with software breakpoints (no watchpoints yet)"
- "Implement PIO JMP instruction with condition evaluation"

### When I Push Back
If I say "This seems like we're cutting corners on fidelity", you should:
1. Explain why it's necessary
2. Provide datasheet evidence
3. Suggest validation method
4. Update documentation accordingly

---

## Academic Rigor Checklist

Before any component is "done", confirm:

- [ ] Datasheet compliance verified
- [ ] Test suite passes (unit + integration + hardware)
- [ ] Code coverage >90%
- [ ] Hardware trace comparison done (±1% accuracy)
- [ ] Design decisions documented
- [ ] Edge cases handled
- [ ] Performance acceptable (>5x real-time)
- [ ] Ready for peer review

---

## Project Metrics

As development progresses, track:

| Metric | Target | Current |
|--------|--------|---------|
| CPU ISA coverage | 100% | TBD |
| PIO ISA coverage | 100% | TBD |
| Peripheral coverage | 95% | TBD |
| Code coverage | >90% | TBD |
| Hardware accuracy | ±1% | TBD |
| Test count | 200+ | TBD |
| Documentation completeness | 100% | TBD |

---

## Open Questions for Author

1. **MicroPython Support**: Just bytecode, or full runtime?
2. **GUI**: CLI-only initially, or include Qt visualizer?
3. **USB**: Full support, or skip for Phase 1?
4. **Thesis Timeline**: 12 weeks? Longer?
5. **Hardware Access**: Do you have 2× Pico boards + debugger + oscilloscope?
6. **Publication Target**: Journal? Conference? Just thesis defense?

---

## Final Reminders

1. **This is a thesis, not a hobby project**. Quality > speed.
2. **PIO is the hard part**. Don't underestimate it.
3. **Validation is mandatory**. Every claim must be testable.
4. **Documentation is code**. Keep it up-to-date.
5. **Ask questions early**. Clarifying scope saves time.

---

## Related Documentation

- [README.md](README.md) - Project overview
- [DESIGN.md](DESIGN.md) - Architecture decisions
- [ARCHITECTURE.md](ARCHITECTURE.md) - Technical specifications
- [BACKLOG.md](BACKLOG.md) - Development roadmap

---

**Last Updated**: 2024-08-28
**Audience**: Claude (AI assistant), developers, thesis reviewers
