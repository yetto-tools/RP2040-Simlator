# ARCHITECTURE.md - Technical Specifications & Component Details

> This document provides complete technical specifications for every component. Use this as a reference for implementation.

---

## Table of Contents

1. [Core CPU Architecture](#1-core-cpu-architecture)
2. [Memory Subsystem](#2-memory-subsystem)
3. [PIO (Programmable I/O)](#3-pio-programmable-io)
4. [Peripheral Controllers](#4-peripheral-controllers)
5. [Interrupt System](#5-interrupt-system)
6. [Clock & Timing](#6-clock--timing)
7. [Debugging Interface](#7-debugging-interface)
8. [Program Loading](#8-program-loading)

---

## 1. Core CPU Architecture

### 1.1 ARM Cortex-M0+ Overview

| Property | Value |
|----------|-------|
| **Architecture** | ARMv6-M |
| **ISA** | Full 16-bit Thumb set + a 6-instruction slice of Thumb-2 (`BL`, `DSB`, `DMB`, `ISB`, `MRS`, `MSR`); not the full ARMv7-M Thumb-2 |
| **Pipeline Stages** | 3 (Fetch, Decode, Execute) |
| **Registers** | 16 × 32-bit + special registers |
| **Clock** | 125 MHz (configurable) |
| **Endianness** | Little-endian |
| **Exception Handling** | NVIC (nested vectored) |

> **Datasheet deviation:** the Cortex-M0+ is **ARMv6-M**. Its ISA is the full
> 16-bit Thumb set plus a 6-instruction slice of Thumb-2's 32-bit encodings
> (`BL`, `MSR`, `MRS`, `DSB`, `DMB`, `ISB`) - *not* the full Thumb-2 of
> ARMv7-M. So there are **no** `IT` blocks, `CBZ`/`CBNZ`, `LDRD`/`STRD`,
> `UMULL`/`SMULL`, `TBB`/`TBH`, 32-bit data processing, or the `Q`/`GE` status
> flags. Sections 1.4-1.5 below still list some ARMv7-M-only instructions;
> those are pruned as the decoder (P1.2) lands. `APSR` holds only `N,Z,C,V`.

### 1.2 Register File

Implemented by `rp2040::RegisterFile` (`src/core/registers.{h,cpp}`).

```cpp
// R0-R12   general purpose
// R13 (SP) banked: MSP / PSP, selected by mode + CONTROL.SPSEL; bits [1:0] RAZ/WI
// R14 (LR) link register; reset value 0xFFFFFFFF
// R15 (PC) program counter; bit 0 RAZ/WI in storage (Thumb state via BX/EPSR.T)

// Program status register (xPSR = APSR | IPSR | EPSR):
//   APSR  [31:28] = N, Z, C, V           (no Q, no GE on ARMv6-M)
//   IPSR  [8:0]   = current exception number (0 => Thread mode)
//   EPSR  [24]    = T (Thumb) bit; always 1 on Cortex-M

// Special registers:
//   CONTROL  bit0 nPRIV (unprivileged Thread), bit1 SPSEL (use PSP in Thread)
//   PRIMASK  bit0     global interrupt mask (PendSV/SysTick/IRQ)
```

Reset: R0-R12 = 0, LR = 0xFFFFFFFF, PC = 0, MSP = PSP = 0, Thread mode,
EPSR.T = 1, flags clear, CONTROL = 0, PRIMASK = 0. (The CPU core later
overrides MSP and PC from vector-table entries 0 and 1.)

A program read of R15 architecturally yields *(current instruction + 4)*;
that pipeline offset is applied by the execute stage, not the register file,
which stores the raw PC.

### 1.3 Instruction Pipeline

```
Stage 1 (Fetch):
├─ Read instruction from memory at PC
├─ Handle 16/32-bit decoding (Thumb variable-length; ARMv6-M has six 32-bit ops)
└─ Increment PC (handling Branch/Trap/Exception cases)

Stage 2 (Decode):
├─ Parse opcode
├─ Identify instruction type
├─ Extract operands
└─ Evaluate condition codes (if conditional instruction)

Stage 3 (Execute):
├─ Perform ALU operation (if applicable)
├─ Access memory (load/store)
├─ Update registers
├─ Update status flags
└─ Handle exceptions/interrupts
```

**Pipeline forwarding**: Minimal (only for certain instructions). Not critical for M0+.

### 1.4 Thumb ISA Coverage

> This section originally listed the full Thumb-2 (ARMv7-M) set. The Cortex-M0+
> implements only **ARMv6-M**: every 16-bit Thumb encoding plus six 32-bit ones
> (`BL`, `MSR`, `MRS`, `DSB`, `DMB`, `ISB`). The lists below are kept for
> reference but entries marked *(v7-M)* are **not decoded** - the decoder
> (`src/core/thumb_decode.cpp`, `include/thumb_isa.h`) returns `UNDEFINED` for
> them, which the CPU escalates to HardFault.
>
> Not in ARMv6-M: `IT`/`ITxyz`, `CBZ`/`CBNZ`, `LDRD`/`STRD`, `LDM.W`/`STM.W`,
> `UMULL`/`SMULL`, `TBB`/`TBH`, `MLA`/`MLS`, `SDIV`/`UDIV`, `BFI`/`UBFX`,
> `MOVW`/`MOVT`, `PLD`, coprocessor access, and all 32-bit data processing.
>
> The decoder is a pure function `(halfword[s]) -> DecodedInstr`; the execute
> stage that applies effects is tracked separately (P1.2 tail / P1.1 pipeline).
>
> **Flag-setting convention (ARMv6-M has no `IT`):** the 16-bit data-processing
> encodings *are* the flag-setting forms - `MOVS`, `ADDS`, `SUBS`, `ANDS`,
> `LSLS`, `MULS`, ... all write `APSR.{N,Z,C,V}`. The only non-flag-setting
> data ops are the high-register `MOV`/`ADD` (encoding T1/T2 in the "special
> data" group), `ADR`, and `ADD`/`SUB SP,SP,#imm`. `CMP`/`CMN`/`TST` always
> update flags. `DecodedInstr.setflags` carries this per instruction.

**Total instructions**: ~56 base (ARMv6-M) - every 16-bit Thumb encoding plus
`BL`, `MSR`, `MRS`, `DSB`, `DMB`, `ISB`

#### Load/Store Instructions (15 types)
```
LDR   - Load 32-bit word
LDRB  - Load 8-bit byte
LDRH  - Load 16-bit halfword
LDRSB - Load signed byte
LDRSH - Load signed halfword
STR   - Store 32-bit word
STRB  - Store 8-bit byte
STRH  - Store 16-bit halfword
LDRD  - Load dual (32-bit pair)
STRD  - Store dual
LDM   - Load multiple
STM   - Store multiple
PUSH  - Push registers onto stack
POP   - Pop registers from stack
LDR (literal) - Load from PC-relative address
```

#### Arithmetic Instructions (20+ types)
```
ADD, ADDS - Addition (with/without flags)
SUB, SUBS - Subtraction
MUL, MULS - Multiplication (32-bit)
UMULL, SMULL - 64-bit multiplication
RSB - Reverse subtract
ADC - Add with carry
SBC - Subtract with carry
NEG - Negate
CMP, CMN - Compare
```

#### Logic Instructions (10+ types)
```
AND, ANDS - Bitwise AND
ORR, ORRS - Bitwise OR
EOR, EORS - Bitwise XOR
BIC, BICS - Bit clear
LSL - Logical shift left
LSR - Logical shift right
ASR - Arithmetic shift right
ROR - Rotate right
MVN - Move NOT
```

#### Branch Instructions (5 types)
```
B      - Unconditional branch
BL     - Branch with link (subroutine call)
BX     - Branch and exchange (switch mode)
BLX    - Branch, link, and exchange
Bcc    - Conditional branch
```

#### Conditional Instructions (via IT)
```
IT       - If-Then (up to 4 instructions)
ITT      - If-Then-Then
ITE      - If-Then-Else
ITTE     - If-Then-Then-Else
etc.
```

#### Special Instructions (8 types)
```
NOP      - No operation
CPS      - Change processor state
CPSID    - Disable interrupts
CPSIE    - Enable interrupts
MRS      - Move from PSR
MSR      - Move to PSR
BKPT     - Breakpoint
SVC      - Supervisor call
```

#### Data Processing (10 types)
```
MOV, MOVS - Move
MVNS      - Move NOT
MOV (shift) - Move with shift
```

### 1.5 Condition Codes

| Code | Meaning | Flag Test |
|------|---------|-----------|
| 0000 | EQ (Equal) | Z set |
| 0001 | NE (Not equal) | Z clear |
| 0010 | CS/HS (Carry set/Unsigned >=) | C set |
| 0011 | CC/LO (Carry clear/Unsigned <) | C clear |
| 0100 | MI (Minus/Negative) | N set |
| 0101 | PL (Plus/Positive or zero) | N clear |
| 0110 | VS (Overflow set) | V set |
| 0111 | VC (Overflow clear) | V clear |
| 1000 | HI (Unsigned >) | C set AND Z clear |
| 1001 | LS (Unsigned <=) | C clear OR Z set |
| 1010 | GE (Signed >=) | N == V |
| 1011 | LT (Signed <) | N != V |
| 1100 | GT (Signed >) | Z clear AND (N == V) |
| 1101 | LE (Signed <=) | Z set OR (N != V) |
| 1110 | AL (Always) | (no test) |
| 1111 | NV (Never) | (not executed) |

---

## 2. Memory Subsystem

### 2.1 Memory Map

| Region | Start | End | Size | Type | Purpose |
|--------|-------|-----|------|------|---------|
| **ROM** | 0x00000000 | 0x00003FFF | 16 KB | Read-only | RP2040 bootloader |
| **Flash** | 0x10000000 | 0x101FFFFF | 2 MB | Read/write | Program code |
| **SRAM** | 0x20000000 | 0x20041FFF | 264 KB | Read/write | Data/Stack |
| **Register Space** | 0x40000000 | 0x5FFFFFFF | ~512 MB | (varies) | Periph. registers |
| **Generic Space** | 0x60000000 | 0xFFFFFFFF | - | - | (unused) |

### 2.2 Register Space Allocation

| Address | Size | Component | Notes |
|---------|------|-----------|-------|
| 0x40000000 - 0x40003FFF | 16 KB | System Control Space (SCS) | NVIC, MPU, DCB |
| 0x40004000 - 0x40013FFF | 64 KB | (Reserved) | |
| 0x40014000 - 0x40017FFF | 16 KB | GPIO | Controls 28 GPIO pins |
| 0x40018000 - 0x4001BFFF | 16 KB | Watchdog | Watchdog timer |
| 0x4001C000 - 0x4001FFFF | 16 KB | Clock | Clock manager, PLL |
| 0x40020000 - 0x4002BFFF | 48 KB | (Reserved) | |
| 0x4002C000 - 0x4002FFFF | 16 KB | RTC | Real-time clock |
| 0x40030000 - 0x40033FFF | 16 KB | (Reserved) | |
| 0x40034000 - 0x40037FFF | 16 KB | UART0 | Serial interface 0 |
| 0x40038000 - 0x4003BFFF | 16 KB | UART1 | Serial interface 1 |
| 0x4003C000 - 0x4003FFFF | 16 KB | SPI0 | SPI interface 0 |
| 0x40040000 - 0x40043FFF | 16 KB | SPI1 | SPI interface 1 |
| 0x40044000 - 0x40047FFF | 16 KB | I2C0 | I2C interface 0 |
| 0x40048000 - 0x4004BFFF | 16 KB | I2C1 | I2C interface 1 |
| 0x4004C000 - 0x4004FFFF | 16 KB | ADC | Analog-to-Digital |
| 0x40050000 - 0x40053FFF | 16 KB | PWM | PWM controller |
| 0x40054000 - 0x40057FFF | 16 KB | Timer | Timers/Counters |
| 0x40058000 - 0x4005BFFF | 16 KB | (Reserved) | |
| 0x4005C000 - 0x4005FFFF | 16 KB | DMA | DMA controller |
| 0x40060000 - 0x4013FFFF | ~3 MB | (Reserved) | |
| 0x50000000 - 0x5000BFFF | 48 KB | USB DPRAM | USB data RAM |
| 0x5000C000 - 0x500FFFFF | ~960 KB | (Reserved) | |
| 0x50100000 - 0x501FFFFF | 1 MB | USBCTRL | USB controller |
| 0x50200000 - 0x502FFFFF | 1 MB | PIO0 | Programmable I/O block 0 |
| 0x50300000 - 0x503FFFFF | 1 MB | PIO1 | Programmable I/O block 1 |
| 0x50400000 - 0xDFFFFFFF | - | (Reserved) | |

### 2.3 Access Sizes & Alignment

The bus exposes three access widths. The RP2040 core is a Cortex-M0+
(ARMv6-M), which is **little-endian only** and **traps every unaligned
access unconditionally** - there is no `CCR.UNALIGN_TRP` configurability as
in ARMv7-M.

| Method | Width | Alignment rule | On violation |
|--------|-------|----------------|--------------|
| `read_byte` / `write_byte` | 8-bit | any address | - |
| `read_half` / `write_half` | 16-bit | `addr % 2 == 0` | `MisalignedAccess` |
| `read_word` / `write_word` | 32-bit | `addr % 4 == 0` | `MisalignedAccess` |

> **Note (datasheet deviation):** `LDRD`/`STRD` (64-bit) do **not** exist in
> the ARMv6-M Thumb instruction set, so no `read_dword`/`write_dword` is
> provided on the CPU bus path. 64-bit values (e.g. the TIMER latched
> counter) are read by firmware as two separate 32-bit accesses. An earlier
> draft of this section listed them; that was an ARMv7-M assumption.

```cpp
enum class BusWidth { Byte = 1, Half = 2, Word = 4 };

enum class BusStatus {
    Ok,
    MisalignedAccess,   // width > 1 and address not naturally aligned
    InvalidAddress,     // not backed by ROM/Flash/SRAM and no peripheral mapped
    WriteToReadOnly,    // store into ROM or the XIP flash window
    PeripheralError,    // a mapped peripheral rejected the access
};

template <typename T>
struct BusResult {
    T value{};
    BusStatus status = BusStatus::Ok;
    bool ok() const { return status == BusStatus::Ok; }
};
```

Reads return `BusResult<uintN_t>`; writes return `BusStatus`. No exceptions
are thrown on the hot path - the CPU maps any non-`Ok` status onto a
HardFault (ARMv6-M has no UsageFault; misaligned/bus errors escalate
directly). This keeps the common case branch-light and fully deterministic
(DESIGN.md Decision 6, Decision 13).

### 2.4 Address Decode

| Range | Backing | Direct CPU write |
|-------|---------|------------------|
| `0x00000000 - 0x00003FFF` | ROM (16 KiB) | fault (`WriteToReadOnly`) |
| `0x10000000 - 0x101FFFFF` | Flash / XIP window (2 MiB) | fault (`WriteToReadOnly`) [1] |
| `0x20000000 - 0x20041FFF` | SRAM (264 KiB, flat) [2] | ok |
| `0x40000000 - 0x5FFFFFFF` | peripheral routing table | delegated to peripheral |
| everything else | - | fault (`InvalidAddress`) |

- [1] The XIP flash window is read-only to direct stores on real hardware;
  programming goes through the SSI/boot2 path. The ELF/UF2 loaders and tests
  populate flash through the **backdoor API** (`load()` / `dump()`), which
  bypasses write protection, alignment rules and peripheral side effects.
- [2] SRAM is physically 4x64 KiB striped banks + 2x4 KiB non-striped banks
  with additional non-striped aliases at `0x21000000+`. The functional model
  is a single flat 264 KiB block; bank striping only affects bus-contention
  timing and is deferred (documented limitation).

Peripherals register themselves on the bus:

```cpp
class BusPeripheral {
public:
    virtual ~BusPeripheral() = default;
    // offset is relative to the registered base; width/alignment already checked.
    virtual BusResult<uint32_t> bus_read(uint32_t offset, BusWidth w) = 0;
    virtual BusStatus bus_write(uint32_t offset, uint32_t value, BusWidth w) = 0;
};

void Memory::attach_peripheral(uint32_t base, uint32_t size, BusPeripheral* p);
```

Register-space atomic alias offsets (`+0x1000` XOR, `+0x2000` set,
`+0x3000` clear) are handled inside the peripheral base class, not the bus
decoder, and land with the first real peripheral (Phase 3).

---

## 3. PIO (Programmable I/O)

### 3.1 PIO Block Architecture

| Feature | Spec |
|---------|------|
| **Blocks** | 2 (PIO0 @ 0x50200000, PIO1 @ 0x50300000) |
| **State Machines** | 4 per block (SM0-SM3), 8 total |
| **Program Memory** | 32 × 16-bit instructions per block |
| **FIFO** | 4-deep per SM (16 words total) |
| **Clock Divider** | 1-65536 (CPU clock / N) |
| **GPIO Access** | Direct (OUT, SET, IN, SIDESET) |
| **Interrupts** | 8 per block (4 SM + 4 global) |

### 3.2 State Machine Registers

```cpp
struct StateMachine {
    // Program execution
    uint32_t pc;              // Program counter (0-31)

    // Data registers
    uint32_t x;               // X register (32-bit)
    uint32_t y;               // Y register (32-bit)

    // Shift registers
    uint32_t osr;             // Output Shift Register (32-bit)
    uint32_t isr;             // Input Shift Register (32-bit)

    // Shift counts
    uint8_t osr_shift_count;  // Bits shifted out of OSR
    uint8_t isr_shift_count;  // Bits shifted into ISR

    // Configuration
    uint32_t clock_divider;   // Clock division factor (1-65536)
    uint8_t pin_base;         // Base GPIO pin number for OUT/SET
    uint8_t pin_count;        // Number of pins (1-32)

    // State
    bool enabled;             // Running?
    bool stalled;             // Waiting on FIFO/external event
};
```

### 3.3 PIO Instruction Set (ISA)

**Format**: 16-bit instruction words

#### Instruction 1: JMP

```
Opcode: 000 (bits 15-13)
Condition: 3 bits (bits 12-10)
Address: 5 bits (bits 4-0)

Conditions:
  00000 = Always
  00001 = !X (X == 0)
  00010 = X-- (X non-zero, post-decrement)
  00011 = !Y (Y == 0)
  00100 = Y-- (Y non-zero, post-decrement)
  00101 = X != Y
  00110 = PIN (GPIO input level)
  00111 = !PIN (NOT GPIO input)
```

#### Instruction 2: WAIT

```
Opcode: 001 (bits 15-13)
Polarity: 1 bit (bit 7) [0=low, 1=high]
Source: 2 bits (bits 5-4) [00=GPIO, 01=IRQ, 10-11=reserved]
Index: 5 bits (bits 4-0)

Examples:
  WAIT 1 GPIO 3  = wait for GPIO3 to be high
  WAIT 0 IRQ 5   = wait for IRQ5 to be triggered
```

#### Instruction 3: IN

```
Opcode: 010 (bits 15-13)
Source: 3 bits (bits 7-5)
  000 = PINS (read GPIO)
  001 = X
  010 = Y
  011 = NULL (push 0s)
  100 = ISR
  101 = OSR
  110-111 = STATUS

Bit count: 5 bits (bits 4-0) [1-32, 0 means 32]

Effect:
  - Shift bit_count bits from source into ISR (right shift)
  - Increment ISR shift counter
  - If auto-push enabled and counter >= threshold: push ISR to RX FIFO
```

#### Instruction 4: OUT

```
Opcode: 011 (bits 15-13)
Destination: 3 bits (bits 7-5)
  000 = PINS
  001 = X
  010 = Y
  011 = NULL (discard)
  100 = PC (jump)
  101 = ISR
  110-111 = STATUS

Bit count: 5 bits (bits 4-0) [1-32, 0 means 32]

Effect:
  - Shift bit_count bits from OSR to destination
  - Decrement OSR shift counter
  - If auto-pull enabled and counter == 0: pull from TX FIFO to OSR
```

#### Instruction 5: PUSH

```
Opcode: 100 (bits 15-13)
If-full: 1 bit (bit 6) [0=always, 1=only if full]
Block: 1 bit (bit 5) [0=non-blocking, 1=block if full]

Effect:
  - Push ISR to RX FIFO
  - Clear ISR shift counter
  - If block=1 and FIFO full: stall SM
  - If if-full=1 and counter < threshold: don't push (but clear counter)
```

#### Instruction 6: PULL

```
Opcode: 101 (bits 15-13)
If-empty: 1 bit (bit 6) [0=always, 1=only if empty]
Block: 1 bit (bit 5) [0=non-blocking, 1=block if empty]

Effect:
  - Pull from TX FIFO to OSR
  - Reset OSR shift counter to 0
  - If block=1 and FIFO empty: stall SM
  - If if-empty=1 and OSR shift counter < 32: don't pull
```

#### Instruction 7: MOV

```
Opcode: 110 (bits 15-13)
Destination: 3 bits (bits 7-5) [same as OUT]
Operation: 2 bits (bits 4-3)
  00 = none
  01 = ~(invert)
  10 = <<(shift left, used for bitrev at LSBs)
  11 = >>(shift right)

Source: 3 bits (bits 2-0) [same as IN]

Effect:
  - Read from source
  - Apply operation
  - Write to destination
  - No FIFO access
```

#### Instruction 8: SET

```
Opcode: 111 (bits 15-13)
Destination: 3 bits (bits 7-5) [only PINS, X, Y allowed]
Immediate: 5 bits (bits 4-0) [0-31]

Effect:
  - Load immediate value into destination
  - If PINS: drive GPIO pins
```

#### Instruction 9: IRQ

```
Opcode: 110, bits [7:5] = 110 (combined = 11110...)
Clear: 1 bit (bit 6) [0=set, 1=clear]
Wait: 1 bit (bit 5) [0=don't wait, 1=wait]
Index: 3 bits (bits 2-0) [IRQ 0-7]

Effect:
  - Trigger IRQ (global IRQ set)
  - If clear=1: clear the IRQ flag
  - If wait=1: stall SM until IRQ acknowledged
```

### 3.4 FIFO Behavior

```cpp
// TX FIFO (CPU  SM): Depth = 4
class TXFIFO {
    uint32_t data[4];
    uint8_t head = 0, tail = 0;

    // CPU writes here (TX[0-3] registers)
    void write(uint32_t value) {
        if (is_full()) {
            if (blocking_mode) {
                stall_cpu = true;  // Signal to CPU to wait
            } else {
                discard_oldest_value();  // Overflow
            }
        }
        data[tail] = value;
        tail = (tail + 1) & 3;
    }

    // SM reads here (via PULL instruction)
    uint32_t read() {
        if (is_empty()) {
            if (blocking_mode) {
                stall_sm = true;  // Stall SM
            } else {
                return last_value;  // Return last pulled value
            }
        }
        uint32_t val = data[head];
        head = (head + 1) & 3;
        return val;
    }
};

// RX FIFO (SM  CPU): Depth = 4
class RXFIFO {
    // SM writes here (via PUSH instruction)
    // CPU reads here (RX[0-3] registers)
};
```

### 3.5 Clock Divider

```cpp
// Clock division: CPU clock / (clock_divider / 256)
// So if CPU is 125 MHz and divider is 256:
// SM clock = 125 MHz / 1 = 125 MHz

// If divider is 512:
// SM clock = 125 MHz / 2 = 62.5 MHz

class ClockDivider {
    uint32_t divider;  // Format: 16-bit int + 8-bit fractional
    uint32_t accumulator = 0;

    bool should_execute() {
        accumulator += divider;
        if (accumulator >= 256) {
            accumulator -= 256;
            return true;  // Execute this cycle
        }
        return false;  // Skip this cycle
    }
};
```

### 3.6 Auto-Push & Auto-Pull

```cpp
// Auto-push: when ISR shift count >= threshold, auto-push to RX FIFO
struct AutoPush {
    bool enabled;
    uint8_t threshold;  // 1-32, typically 32
};

// Auto-pull: when OSR shift count == 0, auto-pull from TX FIFO
struct AutoPull {
    bool enabled;
    uint8_t threshold;  // Typically 32
};
```

### 3.7 Sideset

```
Sideset pins are additional GPIO driven by OUT/SET instructions.
Not a separate instruction, but modifier to OUT/SET/WAIT.

.sideset N [OPT]    # Define sideset pins
  N = number of sideset pins (1-5)
  OPT = optional (pins driven even if instruction doesn't execute)

Example:
.program blink
.sideset 1

  set pins, 1  [sideset 1]  # Set GPIO to high, then sideset pin also high
  jmp 0        [sideset 0]  # Jump, sideset pin low
```

---

## 4. Peripheral Controllers

### 4.1 GPIO Controller

```cpp
struct GPIOController {
    // 28 pins (GPIO0-GPIO27)
    struct Pin {
        bool output_level;       // Current output (0 or 1)
        bool output_enable;      // Driving? (output mode)
        bool pull_up;
        bool pull_down;
        uint8_t slew_rate;       // Fast(0) or slow(1)
        uint8_t drive_strength;  // 2mA, 4mA, 8mA, 12mA

        // Interrupts
        bool int_enabled;
        uint8_t int_type;  // 0=low, 1=high, 2=falling, 3=rising

        // Overrides
        bool pio_override;   // PIO1 or PIO0 override
        bool usb_override;   // USB override
    } pins[28];

    // Register interface (0x40014000+)
    void write_gpio_out(uint32_t addr, uint32_t value);   // GPIO output
    void write_gpio_oe(uint32_t addr, uint32_t value);    // Output enable
    void write_gpio_set(uint32_t addr, uint32_t value);   // Set bits
    void write_gpio_clr(uint32_t addr, uint32_t value);   // Clear bits
    void write_gpio_xor(uint32_t addr, uint32_t value);   // XOR bits

    uint32_t read_gpio_in();                              // Read input levels
    uint32_t read_gpio_status();                          // Status register
};
```

### 4.2 UART Controller (UART0/UART1)

```cpp
struct UART {
    // Configuration
    uint32_t baudrate;         // 300 - 3M baud
    uint8_t data_bits;         // 5, 6, 7, 8
    uint8_t stop_bits;         // 1, 2
    bool parity_enabled;       // Enable parity check?
    bool parity_odd;           // Odd(1) or even(0)?
    bool fifo_enabled;
    bool flow_control;         // RTS/CTS

    // FIFOs
    std::queue<uint8_t> tx_fifo;  // Transmit (CPU  serial line)
    std::queue<uint8_t> rx_fifo;  // Receive (serial line  CPU)

    // Status
    bool tx_busy;
    bool rx_busy;
    bool overrun;     // RX FIFO overflowed
    bool framing_err; // Stop bit wasn't 1
    bool parity_err;
    bool break_detected;

    // Implementation: bit-banging simulation
    // - TX: serialize bits at baudrate
    // - RX: deserialize bits from simulated input
    // - Timing: cycle-accurate to ±1 bit period
};
```

### 4.3 SPI Controller (SPI0/SPI1)

```cpp
struct SPI {
    // Configuration
    bool master_mode;
    uint8_t mode;              // 0-3 (CPOL/CPHA)
    uint32_t baudrate_prescale;
    uint8_t frame_size;        // 4-16 bits
    bool lsb_first;

    // FIFOs
    std::queue<uint32_t> tx_fifo;
    std::queue<uint32_t> rx_fifo;

    // Protocol: cycle-accurate bit transmission
    // - Every bit takes N cycles based on clock
    // - CPOL=0 means idle clock low
    // - CPHA=0 means sample on leading edge
    // - Supports SPI modes 0, 1, 2, 3
};
```

### 4.4 I2C Controller (I2C0/I2C1)

```cpp
struct I2C {
    // Configuration
    bool master_mode;
    uint32_t frequency;  // Standard (100kHz) or Fast (400kHz)
    uint8_t slave_address;

    // Open-drain I/O
    bool scl_high;  // SCL line state (open-drain)
    bool sda_high;  // SDA line state (open-drain)

    // State machine
    enum State { IDLE, START, ADDR, DATA, ACK, STOP };
    State state;

    // Protocol: bit-banging with timing
    // - SCL stretching (slave holds clock low)
    // - START condition: SDA goes low while SCL high
    // - STOP condition: SDA goes high while SCL high
    // - Repeated START
    // - ACK/NACK detection
};
```

### 4.5 Timer / PWM

```cpp
struct TimerSlice {
    // 16-bit counter
    uint16_t counter;
    uint16_t period;      // Auto-reload value
    uint8_t prescaler;    // Divide clock by 2^prescaler
    bool enabled;

    // Two channels per slice (A, B)
    struct Channel {
        uint16_t compare_value;
        bool pwm_mode;        // PWM or one-shot?
        bool output_high;     // Current output level
    } channels[2];

    // Timing
    // - Counter increments every (prescaler + 1) cycles
    // - When counter == compare_value: pulse output or interrupt
    // - When counter == period: reset and reload
};
```

### 4.6 ADC (Analog-to-Digital Converter)

```cpp
struct ADC {
    // Configuration
    uint8_t channel_select;    // 0-4 (GPIO26-GPIO29, temperature)
    bool free_running;
    uint16_t sampling_interval; // Cycles between samples

    // State
    uint16_t current_value;    // Last conversion result (12-bit)
    bool conversion_busy;

    // Conversion timing: ~2 microseconds per sample
    // For simulation: 2us * 125 MHz = 250 cycles per sample

    // Channels
    // 0 = GPIO26 (ADC0)
    // 1 = GPIO27 (ADC1)
    // 2 = GPIO28 (ADC2)
    // 3 = GPIO29 (ADC3)
    // 4 = Temperature sensor (internal)
};
```

### 4.7 Interrupt Controller (NVIC)

```cpp
struct NVIC {
    // 32 interrupt sources (0-31)
    struct IRQ {
        bool enabled;
        uint8_t priority;  // 0-3 (lower number = higher priority)
        bool pending;
        bool active;
    } irq[32];

    // Exception handlers
    std::array<uint32_t, 16> exception_vectors;  // Hardwired addresses

    // SysTick
    struct SysTick {
        uint32_t reload;
        uint32_t counter;
        bool enabled;
        bool interrupt_enabled;
    } sys_tick;
};
```

---

## 5. Interrupt System

### 5.1 Exception Types

| Number | Name | Priority | Vector |
|--------|------|----------|--------|
| 1 | Reset | -3 (highest) | 0x00000004 |
| 2 | Non-maskable Interrupt (NMI) | -2 | 0x00000008 |
| 3 | Hard Fault | -1 | 0x0000000C |
| 11 | SVCall | 0-3 (variable) | 0x0000002C |
| 14 | PendSV | 0-3 (variable) | 0x00000038 |
| 15 | SysTick | 0-3 (variable) | 0x0000003C |
| 16+ | External IRQ | 0-3 (variable) | 0x00000040+ |

### 5.2 Exception Entry

When exception fires:
1. Save context (8 words on stack)
2. Look up vector table entry
3. Jump to ISR address
4. Set exception number in xPSR

**Stack frame** (8 words):
```
R0, R1, R2, R3, R12, LR, PC, xPSR
```

### 5.3 Interrupt Latency

| Event | Cycles |
|-------|--------|
| Interrupt asserted  latency point | 1-3 |
| Latency point  exception entry | ~6 |
| Total (best case) | ~10-15 cycles |

---

## 6. Clock & Timing

### 6.1 Clock Sources

```
XOSC (12 MHz crystal)

  Reference Clock (12 MHz)

  ├─ CPU PLL (multiply by 40)  480 MHz
  │     └ Post-divider (/4)  125 MHz (CPU clock)
  │
  └─ USB PLL (multiply by 40)  480 MHz
        └ Post-divider (/10)  48 MHz (USB clock)
```

### 6.2 Clock Configuration

```cpp
struct ClockManager {
    enum ClockSource {
        ROSC,    // Ring oscillator (~6 MHz)
        XOSC,    // Crystal (12 MHz)
        PLL_SYS, // System PLL (125 MHz)
        PLL_USB, // USB PLL (48 MHz)
    };

    // Typical configuration for RP2040
    uint32_t sys_clock = 125'000'000;  // Hz
    uint32_t usb_clock = 48'000'000;   // Hz

    // PLL parameters
    uint16_t fbdiv;         // Feedback divider
    uint8_t postdiv1;       // Post-divider 1
    uint8_t postdiv2;       // Post-divider 2
};
```

### 6.3 Cycle Counting

```cpp
class Clock {
    uint64_t cycle_count = 0;
    uint32_t cpu_freq_hz = 125'000'000;

    void tick() {
        cycle_count++;
    }

    double get_time_us() const {
        return (double)cycle_count * 1e6 / cpu_freq_hz;
    }
};
```

---

## 7. Debugging Interface

### 7.1 GDB Stub

Implements GDB Remote Serial Protocol (RSP):

```
Target: arm-none-eabi (32-bit ARM)
Debugger: arm-none-eabi-gdb

Remote Protocol packets:
  $g#XX           - Read all registers
  $p0#XX          - Read register 0
  $G...#XX        - Write all registers
  $P0=...#XX      - Write register 0
  $m addr,size#XX - Read memory
  $M addr,size:data#XX - Write memory
  $c#XX           - Continue execution
  $s#XX           - Step one instruction
  $z0,addr,kind#XX - Insert breakpoint
  $Z0,addr,kind#XX - Remove breakpoint
```

### 7.2 Breakpoint Types

- **Software breakpoints**: Replace instruction with BKPT
- **Watchpoints**: (optional) monitor memory reads/writes
- **Hardware breakpoints**: (N/A for simulator, use software)

### 7.3 PIO Inspector

Additional commands for PIO debugging:

```
$Qpio:dump_sm0#XX      - Dump SM0 state
$Qpio:fifo_status#XX   - Show FIFO levels
$Qpio:set_breakpoint#XX - Break when SM reaches address
```

---

## 8. Program Loading

### 8.1 ELF Format

```
ELF Header
├─ Magic (0x7F ELF)
├─ Architecture (ARM)
├─ Entry point (typically 0x10000000)
└─ Program headers
    └─ Segments (code, data, etc.)
```

Loader must:
1. Parse ELF header
2. Verify ARM 32-bit
3. Read program headers
4. Load segments into memory
5. Set PC = entry point

### 8.2 UF2 Format

Raspberry Pi's bootloader format:

```
UF2 Block Structure:
├─ Start marker (0x0A324655)
├─ Target address
├─ Data (256 bytes)
├─ Block size
└─ End marker (0x0AB16F30)
```

### 8.3 PIO Assembly

```
.program blink
.sideset 1

  set pins, 1  [3]    side 1
  set pins, 0  [3]    side 0
  jmp 0
```

Must compile to:
- Machine instructions
- Symbol resolution
- Address calculation

---

## 9. Test Framework

### 9.1 Unit Test Structure

```cpp
#include <gtest/gtest.h>
#include "simulator.h"

class CPUTest : public ::testing::Test {
protected:
    RP2040Simulator sim;

    void SetUp() override {
        sim.reset();
    }
};

TEST_F(CPUTest, AddInstructionSetsFlags) {
    sim.get_cpu().set_register(0, 0x80000000);
    sim.get_cpu().set_register(1, 0x80000000);

    sim.load_code(0x10000000, {0x09, 0x44});  // add r1, r0
    sim.run_one_instruction();

    EXPECT_EQ(sim.get_cpu().get_register(1), 0x00000000);
    EXPECT_TRUE(sim.get_cpu().get_flags().C);  // Carry flag set
}
```

### 9.2 Hardware Validation

```bash
# On real Pico with Pico Debug Probe
$ picoprobe --trace elf_test_program.elf > hardware.vcd

# In simulator
$ rp2040-sim elf_test_program.elf --trace-vcd > simulated.vcd

# Compare
$ python3 tools/compare_traces.py simulated.vcd hardware.vcd
  Error rate: 0.1%
  Cycle accuracy: 99.9%
```

---

## 10. Performance & Metrics

### 10.1 Simulation Speed

| Component | Speed | Overhead |
|-----------|-------|----------|
| CPU only | ~50x real-time | Minimal |
| CPU + PIO | ~10-20x real-time | Variable by program |
| Full system | ~5-10x real-time | Peripheral-heavy |

**Target**: >5x real-time for all workloads

### 10.2 Memory Usage

| Component | Size |
|-----------|------|
| Simulator executable | ~10 MB |
| Runtime (minimal program) | ~50 MB |
| Runtime (full system) | ~200 MB |

### 10.3 Accuracy Metrics

| Metric | Target | Method |
|--------|--------|--------|
| CPU cycle count | ±0.1% | vs GDB trace |
| Instruction timing | ±0 cycles | exact |
| GPIO timing | ±10ns | vs oscilloscope |
| Interrupt latency | ±1 cycle | vs hardware |
| UART bitrate | ±0.1% | vs protocol analyzer |

---

## 11. Fidelity Matrix

| Component | Level | Notes |
|-----------|-------|-------|
| ARM Cortex-M0+ (2x, ARMv6-M) | 5 (Exact) | Full ARMv6-M Thumb ISA; Cortex-M0+ instruction timings |
| PIO State Machines | 5 (Exact) | Cycle-accurate parallel execution |
| GPIO | 4 (Precise) | Timing ±10ns, edge detect exact |
| UART | 4 (Precise) | Bit-level protocol simulation |
| SPI | 4 (Precise) | Mode 0-3, clock stretching |
| I2C | 4 (Precise) | Arbitration, clock sync |
| Timer/PWM | 3 (Accurate) | Timing ±5%, waveforms correct |
| ADC | 3 (Accurate) | Conversion timing realistic |
| Clock Mgr | 3 (Accurate) | Frequency calculation exact |
| USB | 2 (Functional) | Enumeration only (Phase 1) |

---

## References

- **RP2040 Datasheet**: https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf
- **ARM Cortex-M0+ Reference**: https://developer.arm.com/documentation/100165/0201/
- **Thumb ISA**: https://developer.arm.com/documentation/ddi0487/
- **GDB Remote Protocol**: https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html

---

**Last Updated**: 2024-08-28
**Status**: Complete (Phase 1 specifications)
