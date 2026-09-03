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
| **Pipeline Stages** | 2 (Fetch, Decode+Execute) - Cortex-M0+, *not* the M0's 3 |
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

The Cortex-M0+ has a **two-stage** pipeline (the Cortex-M0 has three). The
second stage folds decode and execute together, which is why M0+ branches are
comparatively cheap. The functional model does not pipeline - `Cpu::step()`
fetches, decodes and executes one instruction atomically - but the **cycle
counts** it charges (`src/core/timing.{h,cpp}`) are the M0+ figures, so a
taken branch still costs its pipeline-reload cycles.

```
Stage 1 (Fetch):
├─ Read halfword from memory at PC
├─ If a 32-bit prefix, read the second halfword (ARMv6-M has six 32-bit ops)
└─ PC advances by 2 or 4 (branches/exceptions override)

Stage 2 (Decode + Execute):
├─ Decode -> DecodedInstr (thumb_decode.cpp)
├─ Evaluate the condition (Bcc)
├─ ALU / shift (alu.cpp), memory access (memory.cpp), register + flag updates
└─ Raise HardFault / SVC / BKPT via ExecStatus (exception entry: P1.4)
```

**Pipeline forwarding**: not modelled; not architecturally visible on M0+.

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

#### Conditional Execution

ARMv6-M has **no IT block** (that is ARMv7-M / Cortex-M3+). Conditional
execution on the M0+ is limited to `Bcc` (conditional branch). The
simulator's decoder rejects `IT`/`ITT`/`ITE`/... encodings as UNDEFINED,
along with the other ARMv7-M-only encodings (`CBZ`/`CBNZ`, `LDRD`/`STRD`,
`.W` LDM/STM, `UMULL`/`SMULL`, `MOVW`/`MOVT`).

#### Special / System Instructions
```
NOP      - No operation           WFI  - Wait for interrupt (sleep)
CPS      - Change processor state WFE  - Wait for event (sleep)
CPSID/IE - Disable/enable IRQs    SEV  - Send event (to both cores)
MRS/MSR  - Move from/to PSR       YIELD- Hint (no-op here)
BKPT     - Breakpoint             DSB/DMB/ISB - Barriers (32-bit)
SVC      - Supervisor call        BL   - Branch-link (32-bit)
```

The six 32-bit encodings (`BL`, `MRS`, `MSR`, `DSB`, `DMB`, `ISB`) are the
*only* Thumb-2 slice ARMv6-M implements.

#### Data Processing (flag-setting forms)

Because there is no IT block, the 16-bit data-processing instructions are
the **flag-setting** forms: `MOVS`, `ADDS`, `SUBS`, `ANDS`, `ORRS`, `EORS`,
`LSLS`, `LSRS`, `ASRS`, `RORS`, `MULS`, `MVNS`, `BICS`, `ADCS`, `SBCS`,
`RSBS`, plus the always-flag-setting `CMP`/`CMN`/`TST`. The non-flag forms
that exist are the high-register `MOV`/`ADD`/`CMP`, `ADR`, and `ADD`/`SUB SP`.

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

Base addresses per the RP2040 datasheet 2.2. A **bold** row is modelled by the
simulator; the rest are decoded as unmapped register space.

| Base | Component | Modelled | Notes |
|------|-----------|----------|-------|
| **0x40000000** | **SYSINFO** | yes | CHIP_ID / PLATFORM / GITREF (read-only) |
| 0x40004000 | SYSCFG | stub | decodes, stores writes |
| **0x40008000** | **CLOCKS** | yes | 10 clock generators (functional) |
| **0x4000C000** | **RESETS** | yes | RESET / RESET_DONE (all peripherals "ready") |
| 0x40010000 | PSM | stub | DONE reads "all powered" |
| **0x40014000** | **IO_BANK0** | yes | GPIOx_CTRL FUNCSEL + overrides + per-pin interrupts |
| 0x40018000 | IO_QSPI | stub | |
| **0x4001C000** | **PADS_BANK0** | yes | pulls -> Gpio; drive/schmitt stored |
| 0x40020000 | PADS_QSPI | stub | |
| **0x40024000** | **XOSC** | yes | STABLE/ENABLED once the enable magic is written |
| **0x40028000** | **PLL_SYS** | yes | CS.LOCK + `output_hz` from FBDIV/POSTDIV |
| **0x4002C000** | **PLL_USB** | yes | same `Pll` class |
| 0x40030000 | BUSCTRL | stub | bus priority / perf counters |
| **0x40034000 / 0x40038000** | **UART0 / UART1** | yes | PL011 functional; -> IRQ20 / IRQ21 |
| **0x4003C000 / 0x40040000** | **SPI0 / SPI1** | yes | PL022 functional; -> IRQ18 / IRQ19 |
| **0x40044000 / 0x40048000** | **I2C0 / I2C1** | yes | DW_apb_i2c master; -> IRQ23 / IRQ24 |
| **0x4004C000** | **ADC** | yes | 5 inputs, FIFO, 48 MHz pacing; -> IRQ22 |
| **0x40050000** | **PWM** | yes | 8 slices, PH_CORRECT; -> IRQ4 |
| **0x40054000** | **TIMER** | yes | 64-bit us counter, 4 alarms -> IRQ0..3 |
| **0x40058000** | **WATCHDOG** | yes | down-counter + SCRATCH0..7, TICK |
| **0x4005C000** | **RTC** | yes | full date/time rollover; -> IRQ25 |
| **0x40060000** | **ROSC** | yes | boots enabled + stable |
| 0x40064000 | VREG_AND_CHIP_RESET | stub | |
| 0x4006C000 | TBMAN | stub | PLATFORM reads ASIC |
| **0x50000000** | **DMA** | yes | 12 channels, DREQ pacing; -> IRQ11 / IRQ12 |
| **0x50100000 / 0x50110000** | **USBCTRL_DPRAM / _REGS** | yes | device controller, EP0 only (§4.9); -> IRQ5 |
| **0x50200000 / 0x50300000** | **PIO0 / PIO1** | yes | 4 SMs each; -> IRQ7..10 |
| 0x50400000 | XIP_AUX | no | |
| **0xD0000000** | **SIO** | yes | CPUID, GPIO, inter-core FIFO, 32 spinlocks, integer divider |
| **0xE000E000** | **SCS** (per core) | yes | SysTick, NVIC, SCB - banked per core |

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

See §5.3 for the per-core NVIC model. RP2040 IRQ numbers used by the
simulator: TIMER 0-3, PWM 4, USBCTRL 5, PIO0 7-8, PIO1 9-10, DMA 11-12,
IO_BANK0 13, SIO_PROC0/1 15-16, SPI0/1 18-19, UART0/1 20-21, ADC 22,
I2C0/1 23-24, RTC 25.

### 4.9 USB device controller (0x50100000 / 0x50110000)

A functional model - there is no wire-level SIE. `UsbCtrl` decodes one window
over USBCTRL_DPRAM (4 KB of endpoint buffers + buffer-control words) and
USBCTRL_REGS. It stores MAIN_CTRL / SIE_CTRL / ADDR_ENDP / USB_PWR / MUXING,
computes INTR from SIE_STATUS + BUFF_STATUS + EP_STATUS_STALL_NAK, and drives
USBCTRL_IRQ (IRQ 5) through INTE/INTF.

A **virtual-host** API drives enumeration for tests: `host_reset()` asserts
SIE_STATUS.BUS_RESET; `host_setup()` writes the 8-byte packet to DPRAM 0x00
and sets SETUP_REC; `host_in_ep0()` consumes whatever the device queued in the
EP0 IN buffer (checking the AVAILABLE/FULL/LENGTH fields of
EP0_IN_BUFFER_CONTROL) and sets BUFF_STATUS + TRANS_COMPLETE; `host_out_ep0()`
delivers an OUT data stage. Non-EP0 endpoints, double buffering, host mode and
SOF timing are out of scope.

### 4.8 DMA (12 channels, 0x50000000)

Each channel: `READ_ADDR`, `WRITE_ADDR`, `TRANS_COUNT`, `CTRL` mirrored through
four alias groups; a write to the last register of a group is the trigger.

A trigger **arms** the transfer; `Dma::on_cycles()` (called from
`Simulator::step()`) then moves elements according to `CTRL.TREQ_SEL`:

| TREQ_SEL | Rate |
|----------|------|
| 0x3F (PERMANENT) | 1 element / clock |
| 0x3B-0x3E (TIMER0-3) | `X / Y` elements per clock, from the pacing-timer register at 0x420 + 4*n (`X<<16 | Y`) |
| 0x00-0x3A (a peripheral DREQ) | 1 element every `dreq_divisor()` clocks (approximation - no FIFO-level handshake) |

Per element: size (1/2/4 B), `INCR_READ`/`INCR_WRITE`, `RING_SIZE`/`RING_SEL`
wrap, `BSWAP`; a bus fault sets `READ_ERROR`/`WRITE_ERROR` and ends the
transfer. On completion: `TRANS_COUNT` reads back 0, `INTR` bit set (unless
`IRQ_QUIET`), then `CHAIN_TO` triggers the next channel. `CHAN_ABORT` stops a
running transfer immediately; `TRANS_COUNT` reads the live remaining count
while `BUSY`.

**Sniff** (`SNIFF_CTRL` 0x434 / `SNIFF_DATA` 0x438): when `SNIFF_CTRL.EN` and
`SNIFF_CTRL.DMACH == ch` and the channel's `CTRL.SNIFF_EN`, every transferred
element is folded into `SNIFF_DATA`. `CALC` selects CRC-32 (`0x0`), CRC-32
bit-reversed (`0x1`), CRC-16-CCITT (`0x2` / `0x3` reversed), XOR reduction
(`0xE`) or sum (`0xF`); `BSWAP` byte-swaps first; `OUT_REV` / `OUT_INV`
transform the value on read-back. Seed by writing `SNIFF_DATA`. The reversed
CRC-32 with seed `0xFFFFFFFF` + `OUT_INV` matches zlib `crc32()`.

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

When an exception is taken (`Cpu::take_exception`):
1. Stack the 8-word frame, forcing 8-byte alignment and recording the
   realignment in stacked xPSR[9].
2. Write `EXC_RETURN` (0xFFFFFFF1 / F9 / FD) into LR.
3. Set IPSR = exception number (-> Handler mode).
4. Fetch the handler address from `VTOR + 4*exc` and branch.

Exception *return* decodes the `EXC_RETURN` value from a `BX LR` / `POP {PC}`,
unstacks the frame, restores the mode/flags/SP bank, and (if
`SCR.SLEEPONEXIT` is set and we return to Thread mode) re-enters WFI sleep.

**Stack frame** (8 words): `R0, R1, R2, R3, R12, LR, ReturnAddr, xPSR`

### 5.3 Per-core NVIC (dual core)

Each Cortex-M0+ has its **own** SCS at 0xE000E000. The simulator models both
with one `Scs` object that switches register banks (`set_active_core()`, the
same trick used for the SIO); SysTick, the NVIC enables/pending, IPR and SCR
are therefore per-core. Every peripheral holds an `InterruptController` that
fans its IRQ line out to *both* cores - each core's NVIC then independently
decides whether to take it (per-core enable + priority + PRIMASK).

### 5.4 Sleep (WFI / WFE / SEV)

`Cpu` has an `event` register and an `asleep` flag. `WFI` sleeps until any
interrupt is pending; `WFE` consumes a pending event or sleeps until one
arrives; `SEV` sets the event on both cores; exception entry is a wake event.
`Simulator::run()` keeps peripheral time advancing through a sleep so a
timer/GPIO IRQ can still wake the core.

### 5.5 Interrupt Latency

| Event | Cycles |
|-------|--------|
| Interrupt asserted -> checked (between instructions) | 0-1 instr |
| Exception entry (stacking + vector fetch) | 15, charged in full by `take_exception()` |

The 15-cycle figure is DDI 0484C section 3.6.1's documented worst-case
interrupt latency for the Cortex-M0+ ("the worst case interrupt latency, for
the highest priority active interrupt in a zero wait-state system not using
jitter suppression, is 15 cycles") - verified against the real TRM, not
estimated. The functional model takes the exception at the next `step()`
boundary; the profiler's per-vector "handler cycles" measure covers
entry-to-return.

Not costed: exception *return* and the cycle *savings* of tail-chaining
(back-to-back handler entry skipping the unstack/restack) - this TRM edition
documents no separate figure for either, only the combined entry-latency
number above. Both are functionally correct today (a pending exception is
always taken at the next `step()` boundary, tail-chained or not), just not
cycle-optimized. See BACKLOG.md P5.2.

---

## 6. Clock & Timing

### 6.1 Clock Sources

```
ROSC (ring oscillator, boot default)   XOSC (12 MHz crystal)
                                             |
                                    Reference Clock (12 MHz)
                                             |
   ┌── SYS PLL: VCO = 12 MHz * FBDIV(125) = 1500 MHz
   │            out = VCO / (POSTDIV1(6) * POSTDIV2(2)) = 125 MHz  -> clk_sys
   │
   └── USB PLL: VCO = 12 MHz * FBDIV(40)  = 480 MHz
                out = VCO / (5 * 2) = 48 MHz  -> clk_usb
```

Modelled peripherals (functional, not cycle-exact for the analog parts):
`XOSC` and `ROSC` report STABLE/ENABLED once their enable magic is written
(ROSC boots enabled - the RP2040 runs from it out of reset); `Pll::output_hz`
computes the formula above from the FBDIV / POSTDIV register fields and
returns 0 unless the PLL is locked.

`ClockTree` (`src/peripherals/clock_tree.{h,cpp}`) resolves the ten CLOCKS
generators into concrete Hz: each generator's `CTRL.SRC` (glitchless mux) and
`CTRL.AUXSRC` select a source (a PLL, XOSC, ROSC, or another generator) and
its `DIV` register applies an int + 1/256 fractional divide. Until firmware
writes a generator's CTRL it returns the pico-sdk steady-state default
(clk_sys 125 MHz, clk_adc 48 MHz, clk_rtc 46875 Hz), so bare-metal images
that never call `clocks_init` keep the fixed-125 MHz behaviour.

`Simulator::step()` re-pushes the derived `clk_sys` / `clk_adc` / `clk_rtc`
and the microsecond-tick length (`WATCHDOG_TICK.CYCLES * clk_sys / clk_ref`)
into TIMER, WATCHDOG, ADC and RTC whenever the clock configuration changes
(guarded by a cheap `signature()`). The CPU and PIO already advance at
`clk_sys` by construction (one `step()` == one `clk_sys` cycle for cycle
accounting).

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

### 7.1 GDB Stub  (`src/debuggers/gdb_stub.{h,cpp}`)

Implements the GDB Remote Serial Protocol (RSP). `handle_packet()` is a pure
`string -> string` function (transport-free, fully unit-tested); `serve(port)`
adds a TCP loop (winsock / BSD sockets). Verified end-to-end against a real
`arm-none-eabi-gdb`.

```
Target: arm-none-eabi (r0-r12, sp, lr, pc, xpsr; 17 regs x 8 hex, LE)

Supported packets:
  $g / $G                 - read / write all registers
  $p<n> / $P<n>=<val>     - read / write one register
  $m<addr>,<len>          - read memory      (E01 on a bus fault)
  $M<addr>,<len>:<data>   - write memory
  $c / $s , vCont;c / ;s  - continue / step  (S05 / S0B stop replies)
  $Z0 / $z0               - set / clear a software breakpoint
  $?                      - halt reason
  qSupported, qAttached, qC, QStartNoAckMode, H, D, k
```

CLI: `rp2040-sim --gdb <port> [--entry] <firmware.elf>`.

### 7.2 Breakpoint Types

- **Software breakpoints**: address set, checked before each executed
  instruction on continue/step (the instruction stream is not patched).
- **Watchpoints ($Z2-4)**: not yet implemented.

### 7.3 PIO Debugger  (`src/debuggers/pio_debugger.{h,cpp}`)

Direct inspection of the two PIO blocks (no RSP transport):

- **Per-SM breakpoints** keyed by `(block, sm, program address)`; `run()`
  advances both blocks one system clock at a time and stops when an enabled
  SM is about to execute a breakpoint address.
- **`step()`** - one system clock over both blocks.
- **`inspect(block, sm)`** - PC, X, Y, OSR, ISR + shift counts, TX/RX FIFO
  levels, enabled/stall flags, retired-instruction count, and the
  disassembly of the next instruction.
- **Instruction trace** - `(cycle, block, sm, pc, encoded word)` per retired
  SM instruction.

The disassembler (`src/pio/pio_disasm.{h,cpp}`) renders any 16-bit PIO word
as pioasm text and is verified as the exact inverse of the assembler.

### 7.4 Profiler  (`src/debuggers/profiler.{h,cpp}`)

Drives the machine like `Simulator::run()` while recording a per-PC hot-spot
histogram (exec count + cycles), overall CPI, and per-vector exception stats
(entry count, total/max handler cycles - handler frames are tracked on a
stack so nested/preempted handlers are attributed correctly). CLI: `--profile`.

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

### 8.2 UF2 Format  (`src/loaders/uf2_loader.{h,cpp}`)

Raspberry Pi's drag-and-drop bootloader format: a flat stream of 512-byte
blocks, each with up to 476 bytes of payload and an absolute target address.

```
UF2 block (512 bytes, little-endian):
  +0x000  magicStart0 = 0x0A324655   ("UF2\n")
  +0x004  magicStart1 = 0x9E5D5157
  +0x008  flags        (bit 0 = not-main-flash, bit 13 = familyID present)
  +0x00C  targetAddr
  +0x010  payloadSize  (<= 476)
  +0x014  blockNo
  +0x018  numBlocks
  +0x01C  familyID     (RP2040 = 0xE48BFF56)
  +0x020  data[476]
  +0x1FC  magicEnd    = 0x0AB16F30
```

The loader validates both start magics + the end magic, `numBlocks` vs the
file length, the payload cap, and the family ID (when present); skips
not-main-flash blocks; and copies each payload through the Memory backdoor.
`Simulator::load()` dispatches on the `.uf2` extension and resets through the
image's vector table - offset by 256 bytes when the image lands in flash
(`Memory::kFlash`), to skip the mandatory stage-2 bootloader real RP2040
silicon always runs first (every pico-sdk `boot2_*.S` - and any equivalent
hand-written flash image - is exactly 256 bytes, CRC32 included). SRAM-target
images have no boot2 (the boot ROM never validates them), so VTOR stays at
the image's lowest loaded address unchanged.

### 8.3 PIO Assembler  (`src/pio/pio_assembler.{h,cpp}`)

A two-pass assembler for the SDK's `pioasm` language (minus the code-gen
back-ends, which the simulator does not need):

```
.program blink
.side_set 1
.wrap_target
  set pins, 1  [3]  side 1
  set pins, 0  [3]  side 0
.wrap
```

- Directives: `.program`, `.define [PUBLIC]`, `.origin`,
  `.side_set N [opt] [pindirs]`, `.wrap_target`, `.wrap`.
- Labels (`name:` / `PUBLIC name:`), forward and backward.
- Expression evaluator: `+ - *`, unary `- ~ ::`, parens, hex/bin/dec,
  symbols (defines and labels share one table).
- Output: the encoded 16-bit words plus origin / wrap window / side-set
  configuration / public symbol table.
- `line N:` diagnostics on error.

Encodings are verified by round-tripping through `pio_decode` and the
disassembler.

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

## 12. Local Web Lab

Added 2026-08-31 as a second, browser-based front-end alongside `rp2040-sim`
(see `CONTEXT.md` Decision 6 and `BACKLOG.md` P10.1-P10.3 for the scope
decision and status). It's a thin client/server pair wrapping `rp2040_core`
through its existing public API - no changes to the core, and none of its
dependency-free guarantee is affected, since neither half of this pair links
into `rp2040_core` itself.

```
Browser (web/, Vite + React + TS)           tools/lab_server (native exe)
┌───────────────────────────────┐   HTTP/   ┌──────────────────────────────┐
│ Editor.tsx  - Monaco, gutter   │   JSON    │ main.cpp    - httplib routes │
│   breakpoints                  │◄─────────►│ DebugSession - owns one      │
│ Console.tsx - UART in/out       │  polled   │   Simulator, background     │
│ PinPanel.tsx - GPIO state/      │  ~5 Hz    │   run-loop thread            │
│   external stimulus             │           │ Compiler    - spawns         │
│ DebugToolbar/RegisterView.tsx  │           │   arm-none-eabi-gcc/objdump  │
└───────────────────────────────┘           └──────────────────────────────┘
```

### 12.1 Backend (`tools/lab_server`)

- **`DebugSession`** (`debug_session.{h,cpp}`) is the only genuinely new
  logic, and it's a near-copy of `gdb_stub.cpp`'s `run()` pattern (§7.1):
  a `std::set<uint32_t>` of PC breakpoints, checked each step alongside
  `Memory::take_watchpoint_hit()` (§7.2's watchpoint mechanism, reused
  as-is). The difference from the GDB stub is concurrency: continuing runs
  on a background `std::thread` in batches of 2000 instructions per
  `std::mutex` lock (released between batches) so the HTTP server's
  `/state` polling isn't starved while firmware runs. `snapshot()` takes the
  same lock to read `Simulator::regs()/gpio()/uart()/cycle_count()`.
- **`Compiler`** (`compiler.{h,cpp}`) spawns `arm-none-eabi-gcc` with the
  same flags/linker script as `tests/fixtures/firmware.ld`'s own build step
  (§8's ELF path, unchanged), using a real argv vector
  (`_spawnv`/`posix_spawn`, no shell) - a shell-command-string version was
  tried first and failed silently at runtime on Windows due to `cmd.exe`
  quoting ambiguity around multiple quoted arguments. It additionally runs
  `arm-none-eabi-objdump -dl --no-show-raw-insn` on the resulting ELF and
  parses its interleaved `file:line` / `addr: insn` text output into a
  source-line -> PC-address map, so the frontend's Monaco gutter clicks can
  set real address breakpoints without a full DWARF `.debug_line` parser.
- **`main.cpp`** wires httplib routes (`/compile`, `/load`, `/run`,
  `/pause`, `/step`, `/state`, `/breakpoints`, `/gpio/:pin/external`,
  `/uart/:n/feed`) to one `DebugSession`, base64-encoding ELF/UF2 bytes for
  JSON transport.
- `httplib.h` (v0.54.1) and `json.hpp` (v3.12.0) are vendored single headers
  under `tools/lab_server/vendor/` (MIT, same pattern as
  `tests/vendor/doctest.h`) - used **only** by this optional tool, matching
  `rp2040-sim`'s own "optional tool, not core" tier in the CMake tree.

### 12.2 Frontend (`web/`)

Vite + React + TypeScript, `@monaco-editor/react` for the editor. Talks to
the backend purely over `fetch()` (`web/src/api.ts`), polling `/state` at a
fixed interval while mounted. Two implementation details worth recording
because they cost real debugging time and would recur in any similar
React+Monaco embedding:

- **CORS preflight double-header bug**: the backend originally called
  `set_default_headers({{"Access-Control-Allow-Origin","*"}})` *and* set the
  same header again inside its `Options` handler. The resulting duplicated
  header is spec-invalid and Chrome rejects it for every preflighted
  (POST) request while leaving simple GET requests (like `/state` polling)
  unaffected - so the UI looked like it was working (register/pin panels
  kept updating) while every button (`Compile`, `Run`, `Step`, breakpoints)
  silently failed with a bare `fetch()` "Failed to fetch", no server-side
  error to point at. Fixed by setting the header exactly once.
- **Monaco initial-layout race**: Monaco measures its container
  synchronously on mount; inside this app's CSS grid, that measurement can
  race ahead of the browser's own layout pass and lock in a near-zero size.
  Because nothing about the *container* resizes afterward, `automaticLayout`
  option's `ResizeObserver` never has a reason to fire again, so the editor
  stays stuck tiny. Fixed with a deferred `editor.layout()` call
  (`requestAnimationFrame`) once, right after mount.
- **`onMount` stale-closure trap**: `@monaco-editor/react`'s `onMount`
  fires exactly once per editor instance, so a callback it registers (here,
  the gutter-click `onMouseDown` handler) permanently closes over whatever
  props existed at that first render - in this case, the empty `lineMap`
  from before any compile had happened. Fixed by reading the current
  callback through a `ref` kept up to date every render, rather than
  closing over the prop directly.

### 12.3 pico-sdk compile mode

`/compile`'s `mode: "pico_sdk"` builds the submitted `main.c` as a real
pico-sdk project (fixed `CMakeLists.txt` linking `pico_stdlib` + the
`hardware_*` libraries this simulator implements) instead of §12.1's bare
`arm-none-eabi-gcc` invocation, using a **persistent** project/build
directory pair so Ninja's incremental build stays fast after the first
compile (pico-sdk's own core libraries build from scratch the first time -
seconds, not the freestanding path's near-instant single-file compile).

Getting a real pico-sdk binary to actually *boot* in this simulator - not
just compile - surfaced three real gaps, found by attaching
`arm-none-eabi-gdb` to the existing GDB stub (§7.1) and reading the actual
backtrace at each fault rather than guessing:

- **Boot ROM dependency**: pico-sdk's ELF entry point (`_entry_point`,
  `pico_crt0/crt0.S`) deliberately jumps back into the RP2040's boot ROM on
  real hardware before ever reaching `main()`. This simulator's ROM is an
  empty 16 KiB block (§2 - no proprietary, unredistributable bootrom
  image), so that jump lands on garbage. The fix doesn't fake the ROM:
  `Simulator::load(path, from_entry=false)` resets through the image's
  *real* vector table instead (`_reset_handler`, not `_entry_point` - the
  same code the ROM itself would have jumped to), which was already a
  supported load mode, just never exercised by anything with a boot2-style
  stub segment before its vector table until now. Two of pico-sdk's
  `runtime_init()` steps still call `rom_func_lookup()` directly regardless
  of entry point; those are disabled via the SDK's own documented
  `PICO_RUNTIME_SKIP_INIT_BOOTROM_RESET` flags, and its bit-ops/divider/
  double/float helper libraries (which also default to ROM-lookup
  implementations) are steered to their `compiler` (libgcc) variants
  instead. **Approximation, stated plainly**: firmware built this way is
  not using the same optimized bootrom routines real hardware would; it's
  correct, not identically fast.
- **Vector table addressing** (general core fix, not lab-server-specific):
  `Simulator::load()`'s `from_entry=false` path used to take the *lowest*
  loaded PT_LOAD segment's address as VTOR. That's wrong for any flash
  image with a boot-stage stub before its real vector table - it now
  prefers the ELF's `__vectors`/`__VECTOR_TABLE` symbol
  (`ElfImage::symbol_named()`, `elf_loader.{h,cpp}`) when present, falling
  back to the old behavior otherwise. This also fixes `rp2040-sim`'s own
  CLI boot path, not just the lab server.
- **Atomic register aliasing** (general core fix): the RP2040 datasheet
  (2.1.3) defines peripheral-base + 0x1000/0x2000/0x3000 as XOR/SET/CLEAR
  aliases of the register at the base address - `hw_set_bits()` /
  `hw_clear_bits()` / `hw_xor_bits()`, used throughout every pico-sdk
  peripheral driver, compile straight to a write at one of these aliased
  addresses. `Memory::write_scalar()` didn't implement this at all
  (§2.4/2.5's peripheral dispatch only matched a register's own, unaliased
  address), so any pico-sdk driver call faulted immediately. Now: a write
  whose direct address doesn't match any attached peripheral is retried
  with bits [13:12] masked off; if *that* address matches, the alias type
  selects a read-modify-write (XOR/OR/AND-NOT) against the peripheral's
  real register instead of a plain store.

### 12.4 Multi-file projects

`/compile`'s `source: string` became `files: [{name, content}]` (flat, no
subdirectories, `.c`/`.h` only - `BACKLOG.md` P10.4's own framing, not a
general multi-file CMake project). Both compile paths (§12.1, §12.3) accept
the same file set; `LineAddr` gained a `file` field so breakpoints resolve
per-(file, line).

- **Backend**: freestanding mode writes the whole set into a fresh
  per-request subdirectory with real names (headers resolve via normal
  same-directory `#include "..."`, no special handling needed); pico-sdk
  mode's `CMakeLists.txt` glob (`file(GLOB SOURCES CONFIGURE_DEPENDS *.c)`)
  picks up the current file set automatically on the next `cmake --build`,
  and stale files from a previous compile that are no longer in the set are
  deleted before writing - otherwise a file removed client-side would
  linger on disk and stay linked into the build.
- **Frontend**: `Editor.tsx` moved from a single controlled Monaco model
  (`value`/`onChange`) to one `monaco.editor.ITextModel` per file, since
  undo history and cursor position live on the model, not on whatever
  string a render happens to pass as `value`. A model's content is only
  overwritten when it actually differs from the incoming prop, so this
  editor's own edits (which round-trip back through React state unchanged)
  never reset the cursor mid-keystroke - only genuinely external changes
  (switching mode, restoring from `localStorage`) do.
  - **`onMount`-ordering race, found live**: `@monaco-editor/react` loads
    the Monaco library itself asynchronously, so its `onMount` callback can
    fire *after* a `files`-keyed sync effect has already run once (with the
    editor ref still null, a no-op) - and since refs don't cause
    re-renders, nothing would ever re-trigger that effect again. Fixed by
    calling the same sync logic once directly inside `onMount`, reading
    `files`/`activeFile` through refs kept current every render (the same
    pattern §12.2's stale-closure fix already established) rather than
    relying on the effect firing again.
- The whole project (mode, files, active tab) auto-persists to
  `localStorage` (~500ms debounced) - one working set, not a
  multi-project manager; see `BACKLOG.md` P10.5 for that and the rest of
  what's still deferred.

### 12.5 Explicitly out of scope

A real DWARF `.debug_line` reader (§12.1's breakpoint mapping is
objdump-output parsing) and named/multiple saved projects are deferred.
See `BACKLOG.md` P10.5. (A drag-and-drop circuit editor was also listed
here at the original scope-decision point - see §12.6, it's built.)

### 12.6 Drag-and-drop circuit editor

Originally deferred at scope-decision time (2026-08-31, `CONTEXT.md`
Decision 6) as a multi-year feature; the author asked for it anyway and it
was built incrementally, component by component, each verified end-to-end
against real firmware before the next was added. `web/src/components/
circuit/*`, on `@xyflow/react` (React Flow v12) rather than a from-scratch
canvas.

- **The Pico board node** (`PicoNode.tsx`) is fixed (not draggable/
  deletable, synthesized fresh every render - see `CircuitCanvas.tsx`'s
  `PICO_NODE` constant) with a connection handle per GPIO plus the power/
  control pins (3V3, 3V3_EN, VBUS, VSYS, GND - structural only, not wired
  to anything simulated). Pin data (which GPIOs map to which peripheral,
  ADC channel, SPI/I2C instance) lives centrally in `picoPinout.ts`.
- **Component nodes** are one of two shapes:
  - *Single-GPIO* (LED, Button, Potentiometer, Buzzer): resolved by
    `useCircuitWiring()` to a `Map<nodeId, {gpio, state}>` - one wire in,
    one `PinState` out. Button/Pot also carry a callback (`onGpioPress`/
    `onAdcChange`, wired through `App.tsx`) back into the simulator
    (`DebugSession::set_gpio_external`/`set_adc_external`).
  - *Multi-pin* (the ST7789 TFT, the SSD1306 OLED): `useMultiPinWiring()`
    instead resolves a `Map<nodeId, Map<handleId, gpio>>`, since these
    need several named pins (SCK/MOSI/CS/DC for the TFT; SDA/SCL for the
    OLED) rather than one. Both are *virtual devices*, not RP2040
    peripherals - `src/peripherals/st7789.{h,cpp}` and `ssd1306.{h,cpp}` -
    dynamically attached/detached against whichever SPI/I2C instance the
    wiring implies (`spiInstanceForPins`/`i2cInstanceForPins` in
    `picoPinout.ts`, inferred from the wired SCK+MOSI or SDA+SCL pair
    rather than asking the user to pick a bus number). Each node manages
    its own attach lifecycle directly via `api.ts` calls in a `useEffect`
    (not routed through `App.tsx` like the single-GPIO nodes' callbacks -
    there's no shared App state to update afterward) and polls its
    framebuffer on its own slower interval, rendering into a `<canvas>`.
    `DebugSession` holds one attach slot per device type
    (`reattach_st7789_locked()`/`reattach_ssd1306_locked()`, re-run at the
    end of every `load()` since that replaces the whole `Simulator`) - a
    second TFT or OLED node would silently steal the slot from the first;
    not guarded against, documented in the node source instead.
  - ILI9341 (`src/peripherals/ili9341.h`) needed no new emulator at all: it's
    a MIPI DBI Type C controller like ST7789, and every command this
    project implements (SWRESET/CASET/RASET/RAMWR/MADCTL/...) uses the same
    opcodes on both - only panel resolution differs (240x320 vs ST7789's
    240x240). `St7789`'s width/height became constructor parameters
    (previously `kWidth`/`kHeight` static constants) so `Ili9341` is a
    ~10-line subclass that just passes 240x320 through; `DebugSession` and
    the HTTP surface still treat it as its own device (own attach slot,
    own `/ili9341/*` routes, own `Ili9341Node.tsx`) so wiring both an
    ST7789 and an ILI9341 at once (on different SPI instances) works.
  - The SSD1306 additionally needed `I2c::on_stop()` (`i2c.h`/`i2c.cpp`),
    a hook the ST7789/SPI path never needed: I2C's per-transaction control
    byte (Co/D-C bits, datasheet-external - SSD1306's own protocol) means
    the device needs to know when a STOP resets that framing, which
    `Spi::SlaveFn`'s simpler per-byte shape has no equivalent of. Additive
    - the 8 pre-existing `I2c::SlaveFn` call sites are unaffected.
- **Persistence**: `Project.circuit` (nodes + edges) rides along in the
  same `localStorage` blob as the code editor state (§12.4), preserved
  across Freestanding/pico-sdk mode switches.
- **Verification**: every component was checked against real firmware
  (freestanding, direct register pokes - not pico-sdk's `hardware_*`
  wrappers, to exercise the same register-level path real code would take)
  driven through the actual browser UI, not just `ctest`: an LED/button
  loop, a potentiometer read into ADC, a buzzer PWM tone, an ST7789
  `RAMWR` fill matched pixel-for-pixel against the polled framebuffer, and
  an SSD1306 init+GDDRAM-write sequence (mirroring MicroPython's
  `ssd1306.py` transaction framing exactly) matched bit-for-bit against
  `gddram()`, and an ILI9341 init+`RAMWR` fill (SPI1, direct register
  pokes, mirroring pico-sdk's own register-level `hardware_spi` path) came
  back as a uniform 153600-byte 0x07E0 framebuffer, both in the canvas and
  via `/ili9341/framebuffer`.

**Polish pass** (frontend-only, no `src/`/`tools/` changes), driven by
friction hit firsthand while wiring the last three components above:
- **Placement**: `CircuitCanvas.tsx`'s `handleAdd` used to drop every new
  node into a fixed random band regardless of what was already there -
  every multi-pin device needed a manual drag to un-overlap it. Replaced
  with `findFreeSpot()`: a small per-kind approximate footprint table plus
  a column-major grid scan that places a new node at the first slot that
  doesn't overlap any existing node's footprint. Existing nodes never move.
- **Invalid-wiring feedback**: `Tft7789Node`/`Ili9341Node`/`OledNode` used
  to show the same generic "wire ..." placeholder whether nothing was wired
  yet or the wired pins just couldn't work together (e.g. SCK/MOSI on
  different SPI instances) - the latter now gets its own error-styled
  message, mirroring the `(not ADC)` precedent `PotNode.tsx` already set
  for a wired-but-invalid pin. A `hasSibling` flag (computed once per
  render in `CircuitCanvas.tsx` by counting nodes per type) also surfaces a
  warning badge when two nodes of the same device type compete for the
  backend's one attach slot per type - a real limitation from day one,
  just not previously visible in the UI. `hasSibling` is also in each
  node's attach-effect dependency array, not just its render output: a
  same-type sibling's unmount cleanup unconditionally detaches the shared
  backend slot (it has no way to know whether *it* is the one currently
  holding it), which would otherwise silently orphan a surviving,
  correctly-wired node - found by hand (deleting a duplicate ST7789 node
  left the real one dark) while investigating a "the display shows
  nothing" report. Re-running the effect on every sibling-count change
  re-attaches whichever instance's effect runs last.
- **Notes**: a new `"note"` node kind (`NoteNode.tsx`) - free-text canvas
  annotations, no `Handle`s, added/removed exactly like any other node
  (`onNodesChange`/`applyNodeChanges` already handled Delete/Backspace for
  free). A small label strip above the textarea acts as the drag handle,
  since the textarea itself needs `nodrag` and would otherwise leave
  nothing left to grab.
- **Potentiometer**: `PotNode.tsx` swapped its `<input type=range>` for an
  SVG rotary-knob visual (pointer-capture drag, angle mapped across a 270°
  sweep like a real pot's mechanical travel) - same controlled `raw` state
  and `onChange` wiring as before, so `api.setAdcExternal` is unaffected.
- Handle size/hover affordance, and themed selected-node/edge styling, in
  `index.css`.

---

## References

- **RP2040 Datasheet**: https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf
- **ARM Cortex-M0+ Reference**: https://developer.arm.com/documentation/100165/0201/
- **Thumb ISA**: https://developer.arm.com/documentation/ddi0487/
- **GDB Remote Protocol**: https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html

---

**Last Updated**: 2024-08-28
**Status**: Complete (Phase 1 specifications)
