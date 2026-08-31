# CONTEXT.md - RP2040 Cycle-Accurate Simulator for Thesis

> **Relationship to other docs**: this file is the stable mission/design-decision
> reference. It intentionally does **not** duplicate the live per-feature
> checklist — that's `BACKLOG.md` (authoritative status, updated every commit)
> and `CLAUDE.md` (working agreements + current-phase summary). `ARCHITECTURE.md`
> and `DESIGN.md` hold the technical/design write-ups. If this file and
> `BACKLOG.md`/`CLAUDE.md` ever disagree on status, the other two win.

## 🎯 PROJECT OVERVIEW

### Mission
A **cycle-accurate, publish-ready simulator** of the Raspberry Pi Pico (RP2040) microcontroller for an academic thesis project.

### Key Principles
- ✅ Cycle-Accurate: every operation tracks cycle cost
- ✅ Deterministic: no randomness, no threading, fully reproducible
- ✅ Testable: large automated test suite validating every component
- ✅ Publishable: academic-grade code suitable for peer review
- ✅ Independent: simulator works WITHOUT a GUI
- ✅ Single target: RP2040 only (not a multi-chip/multi-family framework — that's a post-tesis concern)

Out of scope for the thesis phase:
- ❌ GUI (Qt, VSCode, Web) — post-tesis optional
- ❌ Other microcontroller families — RP2040 only
- ❌ Production/commercial features — academic focus

---

## 📅 STATUS (as of 2026-08-31)

**Phases 1-7 are substantially complete.** Core CPU (dual-core Cortex-M0+,
per-core NVIC), the full PIO co-processor, all non-USB peripherals, ELF/UF2
loaders, and the debug tooling (GDB stub, PIO debugger + disassembler,
profiler) are implemented and unit-tested — 36 test suites, green under
`-Werror`.

For the authoritative, item-by-item status (`[DONE]` / `[IN PROGRESS]` / not
started) of every feature, read **`BACKLOG.md`**. Do not infer status from
this file or assume a fresh/empty project — this simulator already has
CPU + memory + PIO + GPIO/UART/SPI/I2C/Timer/PWM/ADC/DMA/USB/RTC/watchdog/clock-tree
implemented. The remaining work is finishing touches on in-progress items
(e.g. PIO clock-divider fractional timing, ADC edge cases, GDB stub extras)
and Phase 8 (testing/validation) + Phase 9 (documentation/thesis writing).

**Next** (per `CLAUDE.md`): hardware-trace validation against physical Pico
boards (needs hardware access), and whatever `BACKLOG.md` currently marks
`[IN PROGRESS]`.

---

## 🏗️ ARCHITECTURE

### Overall Design

```
RP2040 Simulator (monolithic for the thesis)
│
├─ Core library: rp2040_core (static lib, src/, no external deps)
│  ├─ CPU: 2x ARM Cortex-M0+ (cycle-accurate, ARMv6-M Thumb)
│  ├─ PIO: 8 state machines (2 blocks × 4 SM), own co-processor loop
│  ├─ Memory/bus: ROM, Flash, SRAM, memory-mapped peripherals
│  ├─ Peripherals: GPIO/SIO/IO_BANK0/PADS, Timer, UART, SPI, I2C, PWM,
│  │  ADC, DMA, USB, RTC, Watchdog, RESETS, SYSINFO, clock tree
│  ├─ Loaders: ELF, UF2
│  ├─ Debuggers: GDB stub (RSP), PIO debugger + disassembler, profiler
│  └─ Simulator: top-level orchestrator (src/simulator.{h,cpp})
│
└─ CLI front-end: rp2040-sim (src/main.cpp)
```

Post-tesis: a GUI or multi-core-family plugin layer could wrap this library
without changing the core (see `ARCHITECTURE.md` for how the boundaries are
drawn today).

### Execution model

Every clock cycle (see `CLAUDE.md` "How to Think About This Project" for the
full explanation):
1. Each CPU core executes one Thumb instruction.
2. Each PIO block's 4 state machines each execute one PIO instruction
   (respecting their own clock divider).
3. Peripherals update (GPIO, UART, Timer, ADC, ...).
4. Interrupts are checked and dispatched.
5. Clock advances by 1 cycle.

CPU and PIO run in the **same deterministic loop**, not separate threads —
see Design Decision 2 below for why.

---

## 🎯 DESIGN DECISIONS (CRITICAL, still binding)

### Decision 1: Cycle-Accurate, Not Just Functional
Every instruction/operation tracks its cycle cost (`Cpu::cycle_count()`,
the Cortex-M0+ timing table in `src/core/timing.{h,cpp}`, PIO's per-SM
`tick()`). Real firmware depends on timing; PIO must be cycle-perfect;
this is what makes the results publishable.

### Decision 2: PIO + CPU in the Same Loop
No separate threads for CPU vs. PIO. Why not threads?
- ❌ Race conditions, non-deterministic timing, impossible to debug/reproduce
- ✅ Same loop = perfect synchronization, 100% reproducible

### Decision 3: No External Dependencies for the Core
`rp2040_core` is self-contained C++17 (see `CMakeLists.txt`) plus the
vendored single-header `doctest` for tests (`tests/vendor/doctest.h`,
already in the repo — nothing to download). Must build with MSVC or
GCC/Clang; must ultimately interoperate with `arm-none-eabi-gcc`/`gdb`
firmware artifacts.

### Decision 4: Behavioral Circuit/Peripheral Simulation, Not SPICE
Peripherals are modeled at the register/behavioral level against the
datasheet, not at the analog/electrical level. Fast, deterministic,
sufficient for firmware validation; not a replacement for SPICE.

### Decision 5: doctest Framework
Single-header, no external dependency, already vendored at
`tests/vendor/doctest.h`. Tests live under `tests/unit/`,
`tests/integration/`, `tests/hardware_cmp/`, `tests/regression/`.

### Decision 6: CLI Interface Only (No GUI in the Thesis)
`rp2040-sim` (from `src/main.cpp`) is the only front-end for the thesis
phase. A GDB stub (RSP protocol) provides interactive debugging instead of
a graphical debugger. GUI work is explicitly post-tesis.

### Hardware fidelity is non-negotiable
See `CLAUDE.md` "Critical Constraints" — no simplified/mock PIO, no skipped
timing, no untested edge cases. Every component should match the RP2040
datasheet exactly, with any approximation documented in `BACKLOG.md`/
`ARCHITECTURE.md`.

---

## 🛠️ BUILD SYSTEM

Real `CMakeLists.txt` (C++17, CMake ≥ 3.16):

- `rp2040_core` — static library, all of `src/` except `src/main.cpp`
  (bus, memory, registers, thumb decode, ALU, CPU, timing, SCS/NVIC,
  ELF/UF2 loaders, PIO decode/assembler/disasm/block/state-machine/registers,
  GDB stub, PIO debugger, profiler, and every peripheral). No external deps
  (links `ws2_32` on Windows only, for the GDB stub's TCP socket).
- `rp2040-sim` — CLI executable, links `rp2040_core`.
- `RP2040_BUILD_TESTS` (default ON) — builds `tests/` via
  `add_subdirectory(tests)`.
- `RP2040_WARNINGS_AS_ERRORS` (default OFF) — CI/working-agreement builds
  should turn this ON; `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
  -Wsign-conversion` (or `/W4 /permissive-` on MSVC) apply either way.

```bash
mkdir build && cd build
cmake .. -DRP2040_WARNINGS_AS_ERRORS=ON
cmake --build .
ctest            # run the test suite
```

(`CMakePresets.json` in the repo root defines convenience presets — check it
before hand-rolling a configure command.)

---

## 🧪 TESTING FRAMEWORK

`doctest` (single header, vendored at `tests/vendor/doctest.h` — do not
re-download it). Current scale: 275 `TEST_CASE` + 81 `SUBCASE` across 36
suites (~1950 assertions), green under `-Werror`. See `tests/CMakeLists.txt`
for how suites are wired up, and `BACKLOG.md`/`CLAUDE.md` for the coverage
target (>90%, not yet measured — no gcov wiring yet) and the hardware
validation plan (Phase 8, needs physical Pico boards).

```
tests/
├─ doctest_main.cpp
├─ vendor/doctest.h          (vendored, don't touch unless upgrading)
├─ fixtures/                 (test firmware: sum.c, firmware.ld)
├─ unit/                     (one file per component, ~30 files)
├─ integration/              (test_firmware.cpp, test_pio_program.cpp)
├─ hardware_cmp/              (empty so far — Phase 8, needs real hardware)
└─ regression/                (empty so far)
```

---

## 📝 IMPORTANT NOTES (still binding)

### No GUI in the thesis
CLI + GDB stub only. GUI (Qt/VSCode/Web) is explicitly post-tesis — see
`CLAUDE.md` for the reasoning (12-week budget, GUI is 4-6 weeks of
non-essential work).

### Cycle accuracy and determinism
Both are hard requirements, not aspirations — see Design Decisions 1-2
above and `CLAUDE.md`'s "Critical Constraints" section. No `std::random`,
no wall-clock time, no threading in the simulation core.

---

## 📂 PROJECT STRUCTURE (top level; see the repo tree for current detail — this list drifts, `BACKLOG.md` per-feature entries name the actual files)

```
RP2040-Simlator/
├─ src/
│  ├─ core/          CPU, bus, memory, registers, thumb decode, ALU, timing, SCS/NVIC
│  ├─ pio/            PIO block, state machine, decode, assembler, disassembler, registers
│  ├─ peripherals/    gpio, sio, iobank0, padsbank0, timer, uart, spi, i2c, pwm, adc,
│  │                  dma, usb, rtc, watchdog, resets, sysinfo, clocks, clock_tree
│  ├─ loaders/        elf_loader, uf2_loader
│  ├─ debuggers/      gdb_stub, pio_debugger, profiler
│  ├─ simulator.{h,cpp}   top-level orchestrator
│  └─ main.cpp        CLI entry point
├─ include/           public headers (rp2040.h, thumb_isa.h, pio_isa.h, exceptions.h)
├─ tests/             see TESTING FRAMEWORK above
├─ CMakeLists.txt, CMakePresets.json
├─ CLAUDE.md          working agreements + current-phase status (authoritative)
├─ BACKLOG.md         per-feature roadmap + status (authoritative)
├─ ARCHITECTURE.md    technical design write-up
├─ DESIGN.md          design decisions in depth
├─ README.md          project overview
└─ CONTEXT.md         this file
```

---

## 🎓 EXPECTED THESIS CONTRIBUTION

"A Cycle-Accurate Simulator for the ARM Cortex-M0+ Microcontroller with
Embedded Programmable I/O Co-Simulation"

Key contributions:
- Cycle-accurate dual-core CPU simulation
- Full PIO co-simulation (8 state machines, all 9 instruction types)
- Deterministic execution (fully reproducible)
- Large automated test suite (36 suites / ~2000 assertions and growing)
- Open-source implementation

Deliverables: thesis manuscript, source code, documentation
(README/ARCHITECTURE/DESIGN/BACKLOG/CLAUDE), test suite, hardware
validation data (pending physical Pico access — see `BACKLOG.md` Phase 8).

Not included (post-tesis): GUI, other MCU families, production deployment,
cloud integration, community features.

---

## 📞 WHERE TO LOOK FOR WHAT

- Current per-feature status → `BACKLOG.md`
- Working agreements, execution-model explanation, common-mistakes list,
  "how to communicate about this project" → `CLAUDE.md`
- Technical architecture / component design → `ARCHITECTURE.md`
- Design rationale in depth → `DESIGN.md`
- This file → stable mission + design decisions that don't change commit to
  commit
