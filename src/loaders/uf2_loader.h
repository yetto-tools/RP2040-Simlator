// uf2_loader.h - Load a Microsoft UF2 image into Memory.
//
// UF2 ("USB Flashing Format") is the drag-and-drop firmware container the
// RP2040 bootrom accepts on its mass-storage device. The file is a flat
// sequence of 512-byte blocks; each block carries up to 476 bytes of payload
// and the absolute target address to place it at.
//
// This loader validates every block (both start magics, the end magic, the
// block/count fields and - when present - the family ID) and copies each
// payload to its target address through the Memory backdoor, exactly as the
// bootrom would flash it. Blocks flagged "not main flash" are skipped.
//
// Reference: https://github.com/microsoft/uf2 (SPEC), RP2040 datasheet 2.8.4.1.
#ifndef RP2040_LOADERS_UF2_LOADER_H
#define RP2040_LOADERS_UF2_LOADER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/memory.h"

namespace rp2040 {

// RP2040 family ID, as emitted by pico-sdk's elf2uf2 / picotool.
inline constexpr std::uint32_t kUf2FamilyRp2040 = 0xE48BFF56u;

struct Uf2Image {
    bool ok = false;
    std::string error;               // populated when ok == false
    unsigned blocks_loaded = 0;      // payload-bearing blocks copied to Memory
    unsigned blocks_skipped = 0;     // blocks flagged "not main flash"
    std::uint32_t family_id = 0;     // last family ID seen (0 if none present)
    std::uint32_t lowest_addr = 0;
    std::uint32_t highest_addr = 0;  // one past the last byte written
};

// Parse and load a UF2 image held entirely in `data`.
Uf2Image load_uf2(Memory& mem, const std::uint8_t* data, std::size_t size);

// Read `path` into memory, then load_uf2(). On a file error, `ok` is false
// and `error` explains.
Uf2Image load_uf2_file(Memory& mem, const std::string& path);

}  // namespace rp2040

#endif  // RP2040_LOADERS_UF2_LOADER_H
