# DESIGN.md - Architectural Decisions & Design Patterns

> This document explains the "why" behind the simulator's architecture, not the "what" (see ARCHITECTURE.md for that).

## Design Philosophy

### First Principle: Cycle-Accuracy
Every design decision prioritizes **cycle-accurate simulation** over simplicity.

-  Trade-off: Simulation speed vs accuracy
-  Always choose accuracy; optimize speed secondarily

### Second Principle: Hardware Faithfulness
The simulator mimics hardware behavior, not abstractions.

-  Don't model "what the programmer sees"
-  Model "what the hardware does"

### Third Principle: Testability
Each component must be independently testable against hardware.

-  No black boxes or opaque state
-  Expose internal state for validation

---

## Major Architectural Decisions

### Decision 1: Monolithic Core Simulator vs Modular Components

**Question**: Should the core be one big class, or split into modules?

**Decision**: **Modular components with central orchestrator**

**Rationale**:
- Individual components (CPU, PIO, GPIO, etc.) are complex
- Each needs independent testing
- Allows parallel development
- Easier to validate against hardware specs

**Implementation**:
```cpp
class RP2040Simulator {
    CPU cpu;
    Memory memory;
    PIOBlock pio0, pio1;
    GPIO gpio;
    UART uart[2];
    // ...

    void tick() {
        cpu.execute();
        pio0.execute_cycle();
        pio1.execute_cycle();
        update_peripherals();
        handle_interrupts();
    }
};
```

---

### Decision 2: Separate CPU and PIO Execution

**Question**: Should PIO run in the same execution loop as CPU, or separately?

**Decision**: **Same execution loop, but truly parallel**

**Rationale**:
- RP2040 hardware: CPU and PIO are independent processors
- They execute simultaneously
- Both increment their counters every cycle
- They synchronize only through shared memory/FIFO

**Wrong approach**:
```cpp
//  Sequential: finishes CPU, then PIO
cpu.execute_instruction();
pio.execute_instruction();  // Waits for CPU to finish
```

**Correct approach**:
```cpp
//  Parallel: both execute in one tick
void tick() {
    cpu.execute_instruction();      // Fetch  Decode  Execute (1 cycle)
    pio0.execute_all_sm();          // All 4 SMs execute (1 cycle each)
    pio1.execute_all_sm();          // All 4 SMs execute (1 cycle each)
    // CPU is ready for next instruction
    // PIO SMs are ready for next instruction
}
```

---

### Decision 3: Clock Divider Implementation

**Question**: How to handle PIO clock dividers without running SM multiple times?

**Decision**: **External clock counter with skip logic**

**Rationale**:
- Each SM can have independent clock divider (1-65536)
- If divider is 4, SM should only execute every 4th cycle
- Running SM 4 times per cycle wastes performance
- Better: track "time since last execute" and skip if not ready

**Implementation**:
```cpp
struct StateMachine {
    uint32_t clock_divider = 1;  // Divide CPU clock by this
    uint32_t cycle_counter = 0;

    void execute_if_ready() {
        cycle_counter++;
        if (cycle_counter >= clock_divider) {
            fetch_decode_execute();
            cycle_counter = 0;
        }
    }
};
```

---

### Decision 4: FIFO Blocking vs Non-Blocking

**Question**: When SM executes PULL on empty FIFO, should it stall or continue?

**Decision**: **Stall (blocking behavior), with conditional non-blocking flag**

**Rationale**:
- RP2040 datasheet specifies PULL can block or not-block
- Program can set `PULL ifempty` (non-blocking) or `PULL` (blocking)
- Blocking is more common in practice
- Must simulate stall: SM waits for data, increments cycle count

**Edge case**: CPU writes to FIFO while SM is stalled
- SM should immediately resume in same cycle
- No race condition in hardware (synchronous)

---

### Decision 5: Instruction Decoding Strategy

**Question**: Use lookup table, switch/case, or virtual dispatch for ISA?

**Decision**: **Hybrid: switch/case for main opcode, helper methods for operands**

**Rationale**:
- ARM Thumb-2: 177+ instructions  huge switch statement
- Group by opcode patterns  smaller, faster
- Each group has dedicated method
- Easier to document and validate

**Example**:
```cpp
void CPU::decode_and_execute(uint16_t instr) {
    uint8_t opcode = (instr >> 11) & 0x1F;

    switch (opcode) {
        case 0x00: return execute_lsl_lsr_asr(instr);
        case 0x01: return execute_add_sub(instr);
        case 0x02: return execute_mov_cmp_add_sub_imm(instr);
        // ...
        default: return fault(UNDEFINED_INSTRUCTION);
    }
}
```

---

### Decision 6: Memory Subsystem Architecture

**Question**: Treat memory as flat array, or route through peripheral map?

**Decision**: **Unified memory with peripheral dispatch**

**Rationale**:
- CPU reads/writes to 0x40000000+ trigger peripheral register side-effects
- Example: write to GPIO OUT register  pin changes
- Must route writes to correct peripheral
- Must support byte/half/word accesses at any alignment

**Implementation**:
```cpp
class Memory {
    uint8_t rom[16KB];
    uint8_t flash[2MB];
    uint8_t sram[264KB];

    uint32_t read_word(uint32_t addr) {
        if (addr < 0x10000000) return read_flash(addr);
        if (addr < 0x20000000) return read_ram(addr);
        if (addr < 0x40000000) return read_peripheral(addr);
        // ...
    }
};
```

---

### Decision 7: GPIO Pin State Representation

**Question**: Simple bool for each pin, or struct with full state?

**Decision**: **Struct with complete state (direction, level, pull, interrupt)**

**Rationale**:
- GPIO has complex state (input/output, high/low, pull-up/down, edge-detect)
- Different components read different aspects:
  - CPU reads GPIO input level
  - PIO reads input for IN instruction
  - GPIO controller reads output level for pins
- Better to keep all state in one place

**Implementation**:
```cpp
struct GPIOPin {
    bool output_level : 1;       // Current level (0 or 1)
    bool output_enable : 1;      // Driving pin (output mode)
    bool pull_up : 1;
    bool pull_down : 1;
    bool interrupt_enabled : 1;
    uint8_t interrupt_type : 2;  // low, high, rising, falling

    uint32_t get_effective_level() {
        if (output_enable) return output_level;
        if (pull_up) return 1;
        if (pull_down) return 0;
        return external_input_level;  // Simulated external value
    }
};
```

---

### Decision 8: Interrupt Handling Strategy

**Question**: Process interrupts at end of cycle, or check after every instruction?

**Decision**: **Check after every instruction, but buffer in pending register**

**Rationale**:
- Hardware checks for interrupts between instructions
- Interrupt can arrive mid-instruction (asynchronously)
- But don't execute ISR mid-instruction
- More accurate: check after each instruction completes

**Execution flow**:
```
1. Fetch instruction
2. Decode instruction
3. Execute instruction
4. [INTERRUPT CHECK]  Check for pending interrupts
5. If pending: save state, jump to ISR
6. Otherwise: increment PC, next instruction
```

---

### Decision 9: PIO State Machine Context Switching

**Question**: How to handle SM stalls (PULL/PUSH on full/empty FIFO)?

**Decision**: **Keep SM in stalled state, try again next cycle**

**Rationale**:
- No context switching overhead
- SM's cycle counter still advances (instruction timing)
- When FIFO becomes available, SM resumes seamlessly
- Matches hardware behavior exactly

**Example**: SM executes PULL on empty FIFO
```
Cycle 1: PULL attempts execution
         FIFO is empty
         SM stalls (PC doesn't advance)

Cycle 2: SM checks again
         FIFO still empty
         SM stalls again

Cycle 3: CPU writes data to TX FIFO
         SM resumes execution
         PULL completes
         PC advances
```

---

### Decision 10: Trace Generation Format

**Question**: Binary format, text CSV, or standard VCD?

**Decision**: **VCD (Value Change Dump) with custom binary extension**

**Rationale**:
- VCD is standard for digital circuit simulation
- Can open in free tools (GTKWave, Python)
- Human-readable
- Self-documenting
- Easy to parse
- Can compare sim vs hardware traces

**Included signals**:
- CPU PC, registers, flags
- Memory bus (address, data, read/write)
- GPIO pin levels
- PIO SM PC, registers, FIFO status
- Interrupt signals
- Clock ticks

---

### Decision 11: Determinism & Reproducibility

**Question**: Allow floating-point arithmetic, OS threads, or system time?

**Decision**: **Strict determinism: no floating-point, no threads, no system time dependency**

**Rationale**:
- Thesis requires reproducible results
- Same input must produce identical trace
- Enables "record and replay" capability
- Allows trace comparison without variance

**Constraints**:
-  No random number generation (use seeded PRNG)
-  No multithreading (single-threaded event loop)
-  No floating-point (fixed-point where needed)
-  All state changes logged deterministically

---

### Decision 12: Testing Strategy

**Question**: Unit tests, integration tests, hardware comparison, or all three?

**Decision**: **Three-tier pyramid: unit > integration > hardware**

**Rationale**:
- Unit tests: 80% (fast, comprehensive)
- Integration tests: 15% (catch interaction bugs)
- Hardware tests: 5% (validate against real RP2040)

**Test pyramid**:
```
           ▲
          /|\
         / | \
        /  |  \  Hardware Validation Tests (5%)
       /   |   \ - Real Pico + oscilloscope
      /    |    \- Cycle count comparison
     /─────┼─────\
    /      |      \
   / Integration   \ Integration Tests (15%)
  /  Tests (15%)   \- Multi-component scenarios
 /─────────┼────────\
/          |         \
Unit       |       Unit Tests (80%)
Tests      |   - CPU instructions
(80%)      |   - PIO operations
───────────┼────── - Peripheral functions
           |
```

---

### Decision 13: Register Access Size Handling

**Question**: ARM can load/store byte, half-word, or word. How to handle misalignment?

**Decision**: **Support all sizes, check alignment according to ARM spec**

**Rationale**:
- LDR/STR access 32-bit (aligned to 4)
- LDRH/STRH access 16-bit (aligned to 2)
- LDRB/STRB access 8-bit (any alignment)
- Misalignment  fault (depends on arm mode)

**Implementation**:
```cpp
uint32_t Memory::read_byte(uint32_t addr) {
    // Always allowed, any alignment
    return data[addr];
}

uint16_t Memory::read_half(uint32_t addr) {
    if (addr & 1) fault(MISALIGNED_ACCESS);  // Only even addresses
    return *(uint16_t*)&data[addr];
}

uint32_t Memory::read_word(uint32_t addr) {
    if (addr & 3) fault(MISALIGNED_ACCESS);  // Only 4-byte aligned
    return *(uint32_t*)&data[addr];
}
```

---

### Decision 14: ISR (Interrupt Service Routine) Tail-Chaining

**Question**: If interrupt fires while ISR is running, how to handle?

**Decision**: **Tail-chaining: save context, jump to higher-priority ISR immediately**

**Rationale**:
- Hardware optimizes this: no stack frame overhead
- More common in real firmware
- Matches hardware behavior
- Improves simulation performance

---

### Decision 15: Floating-Point Support

**Question**: Cortex-M0+ doesn't have FPU. Simulate software FP emulation?

**Decision**: **Skip for Phase 1. Flag as limitation if needed later.**

**Rationale**:
- M0+ has no hardware FP
- FP operations are rare in embedded (especially RP2040 projects)
- Software FP is complex to emulate
- Can add in Phase 2 if thesis requires it

---

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────┐
│           Execution Tick (Every Cycle)              │
├─────────────────────────────────────────────────────┤
│                                                     │
│  1. [CPU]                                           │
│     ├─ Fetch instruction from memory                │
│     ├─ Decode opcode, operands                      │
│     ├─ Execute (ALU, register updates, memory I/O)  │
│     └─ Update flags (N, Z, C, V)                    │
│                                                     │
│  2. [PIO Block 0] (runs in parallel)                │
│     └─ For each State Machine (0-3):                │
│        ├─ Check if time to execute (clock divider)  │
│        ├─ Fetch instruction from program memory     │
│        ├─ Decode PIO opcode                         │
│        ├─ Execute (may read/write GPIO or FIFO)     │
│        └─ Update PC (unless JMP)                    │
│                                                     │
│  3. [PIO Block 1] (same as Block 0)                 │
│                                                     │
│  4. [Memory Bus] (all writes queue up)              │
│     └─ CPU write to GPIO  GPIO updates            │
│     └─ PIO OUT to GPIO  GPIO updates              │
│     └─ CPU write to FIFO  SM can read             │
│                                                     │
│  5. [Interrupt Check]                               │
│     ├─ Check if any interrupt pending               │
│     ├─ Check if CPU maskable (no higher priority)   │
│     └─ If yes: save state, jump to ISR             │
│                                                     │
│  6. [Clock Advance]                                 │
│     └─ Increment global cycle counter               │
│                                                     │
│  7. [Repeat] next tick                              │
│                                                     │
└─────────────────────────────────────────────────────┘
```

---

## Key Design Patterns

### Pattern 1: Observer for Component Interactions
When GPIO changes, notify all observers (PIO, interrupts, debug tracer).

```cpp
class GPIO {
    std::vector<std::function<void(uint8_t pin, bool level)>> observers;

    void set_level(uint8_t pin, bool level) {
        pins[pin].level = level;
        for (auto& cb : observers) cb(pin, level);
    }
};
```

### Pattern 2: State Snapshot for Debugging
Capture full system state at each cycle for trace export.

```cpp
struct SimulationState {
    uint64_t cycle;
    Registers cpu_regs;
    uint32_t cpu_pc;
    std::array<PIOMachineState, 8> pio_state;
    std::array<uint32_t, 28> gpio_levels;
};
```

### Pattern 3: Command Queue for Deferred Actions
Some operations (interrupt dispatch, FIFO writes) need to happen atomically.

```cpp
class SimulationQueue {
    std::vector<std::function<void()>> pending_operations;

    void enqueue(std::function<void()> op) {
        pending_operations.push_back(op);
    }

    void flush() {
        for (auto& op : pending_operations) op();
        pending_operations.clear();
    }
};
```

### Pattern 4: Fidelity Levels for Optional Features
Mark components with how accurately they're simulated.

```cpp
enum class FidelityLevel {
    EXACT = 5,       // ±0 cycles, 100% compliant
    PRECISE = 4,     // ±10ns, protocol exact
    ACCURATE = 3,    // ±1%, timing realistic
    FUNCTIONAL = 2,  // ±10%, behavior correct
    MOCK = 1         // Approximate
};

class Component {
    static constexpr FidelityLevel FIDELITY = FidelityLevel::EXACT;
};
```

---

## Validation Strategy

### Before Calling Something "Done":

1. **Datasheet Compliance**
   - Read spec section
   - Check behavior matches exactly
   - Document any deviations

2. **Unit Tests**
   - Boundary conditions
   - Edge cases
   - Normal operation
   - Error cases

3. **Hardware Validation**
   - Compile test program for real RP2040
   - Run on Pico + oscilloscope
   - Capture trace with Pico Debug Probe
   - Compare: simulator output vs hardware output
   - Calculate error margin (must be < threshold)

4. **Integration Tests**
   - Test with CPU reading GPIO
   - Test with PIO writing GPIO
   - Test with interrupts
   - Test multi-component scenarios

5. **Performance Validation**
   - Measure simulation speed
   - Ensure >5x real-time (adjustable)
   - Profile bottlenecks
   - Optimize if needed

---

## Fidelity Matrix

| Component | Fidelity | Justification | Validation Method |
|-----------|----------|---------------|-------------------|
| ARM CPU | EXACT (5) | Every cycle must match | vs gdb trace |
| PIO | EXACT (5) | Thesis focus | vs logic analyzer |
| GPIO | PRECISE (4) | Timing important | vs oscilloscope |
| UART | PRECISE (4) | Protocol critical | vs protocol analyzer |
| SPI | PRECISE (4) | Bit-level timing | vs logic analyzer |
| I2C | PRECISE (4) | Clock sync matters | vs logic analyzer |
| Timer | ACCURATE (3) | Timing realistic enough | tolerance ±5% |
| ADC | ACCURATE (3) | Conversion timing | tolerance ±10% |
| Clock Mgr | ACCURATE (3) | Frequency matters | vs frequency counter |
| USB | FUNCTIONAL (2) | Enumeration only | basic tests |

---

## Future Design Considerations

### Phase 2 (If Needed)
- Hardware floating-point emulation
- Full USB device support
- Wireless (802.11n if Pico W)
- Power consumption estimation

### Phase 3 (Post-Thesis)
- GUI debugger with waveform viewer
- Python/Lua scripting API
- Integration with formal verification tools
- Distributed simulation (multiple Picos)

---

## References

- RP2040 Datasheet: [Official PDF](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- ARM Cortex-M0+ Reference: [Developer Guide](https://developer.arm.com/documentation/100165/0201/)
- PIO Book: [Section 3.22, RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf#page=395)

---

**Last Updated**: 2024-08-28
**Status**: Complete (for Phase 1)
