// elf_loader.h - Load a little-endian ARM ELF32 executable into Memory.
//
// Handles the PT_LOAD segments of a statically-linked bare-metal image
// (`arm-none-eabi-gcc -mcpu=cortex-m0plus`). Segments are placed at their
// physical address (p_paddr / LMA) via the Memory backdoor path; the BSS
// tail (p_memsz > p_filesz) is zero-filled.
//
// Reference: ELF-32 spec + "ELF for the ARM Architecture" (ARM IHI 0044).
#ifndef RP2040_LOADERS_ELF_LOADER_H
#define RP2040_LOADERS_ELF_LOADER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/memory.h"

namespace rp2040 {

// One entry of SHT_SYMTAB (ELF-32 spec ch. 1-3, "Symbol Table").
struct ElfSymbol {
    std::string name;
    std::uint32_t value = 0;  // st_value (Thumb bit not stripped, as in the ELF)
    std::uint32_t size = 0;
    std::uint8_t type = 0;    // STT_*: 1 = OBJECT, 2 = FUNC, ...
    std::uint8_t bind = 0;    // STB_*: 0 = LOCAL, 1 = GLOBAL, 2 = WEAK
};

// One section header (name + load address/size only - not the full Shdr).
struct ElfSection {
    std::string name;
    std::uint32_t addr = 0;   // sh_addr (0 for non-loadable sections, e.g. .symtab)
    std::uint32_t size = 0;
};

struct ElfImage {
    bool ok = false;
    std::string error;               // populated when ok == false
    std::uint32_t entry = 0;         // e_entry (Thumb bit not stripped)
    unsigned segments_loaded = 0;
    std::uint32_t lowest_addr = 0;
    std::uint32_t highest_addr = 0;  // one past the last byte written

    // Best-effort: absent (empty) rather than a load failure if the ELF was
    // stripped, or its section headers are malformed - only the PT_LOAD
    // segments above are required for correct execution.
    std::vector<ElfSymbol> symbols;
    std::vector<ElfSection> sections;

    // The symbol whose [value, value + size) contains `addr` (a FUNC/OBJECT
    // symbol is preferred over a zero-size one at the same address), or
    // nullptr if none - for resolving PC to a function name in traces/the
    // debugger. `addr` is matched with the Thumb bit masked off.
    const ElfSymbol* symbol_at(std::uint32_t addr) const;

    // Exact-name lookup (first match), or nullptr. Used to find the real
    // vector-table address by its conventional linker symbol name
    // (`__vectors` et al. - see Simulator::load()'s from_entry=false path),
    // since the lowest PT_LOAD segment isn't always where the vector table
    // starts (e.g. a flash image with a boot2 stub segment before it).
    const ElfSymbol* symbol_named(const std::string& name) const;
};

// Parse and load an ELF image held entirely in `data`.
ElfImage load_elf(Memory& mem, const std::uint8_t* data, std::size_t size);

// Read `path` into memory, then load_elf(). On a file error, `ok` is false
// and `error` explains.
ElfImage load_elf_file(Memory& mem, const std::string& path);

}  // namespace rp2040

#endif  // RP2040_LOADERS_ELF_LOADER_H
