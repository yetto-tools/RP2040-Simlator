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

#include "core/memory.h"

namespace rp2040 {

struct ElfImage {
    bool ok = false;
    std::string error;               // populated when ok == false
    std::uint32_t entry = 0;         // e_entry (Thumb bit not stripped)
    unsigned segments_loaded = 0;
    std::uint32_t lowest_addr = 0;
    std::uint32_t highest_addr = 0;  // one past the last byte written
};

// Parse and load an ELF image held entirely in `data`.
ElfImage load_elf(Memory& mem, const std::uint8_t* data, std::size_t size);

// Read `path` into memory, then load_elf(). On a file error, `ok` is false
// and `error` explains.
ElfImage load_elf_file(Memory& mem, const std::string& path);

}  // namespace rp2040

#endif  // RP2040_LOADERS_ELF_LOADER_H
