#include "loaders/elf_loader.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace rp2040 {

namespace {

std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// ELF-32 header field offsets.
enum : std::size_t {
    kEIClass = 4, kEIData = 5,
    kEType = 16, kEMachine = 18, kEEntry = 24, kEPhoff = 28,
    kEPhentsize = 42, kEPhnum = 44, kElfHeaderSize = 52,
};
// Program-header field offsets.
enum : std::size_t {
    kPType = 0, kPOffset = 4, kPPaddr = 12, kPFilesz = 16, kPMemsz = 20,
    kProgHeaderSize = 32,
};
inline constexpr std::uint16_t kEMArm = 40;
inline constexpr std::uint32_t kPtLoad = 1;

ElfImage fail(const char* msg) {
    ElfImage img;
    img.ok = false;
    img.error = msg;
    return img;
}

}  // namespace

ElfImage load_elf(Memory& mem, const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < kElfHeaderSize) return fail("file too small for an ELF header");
    if (std::memcmp(data, "\x7F""ELF", 4) != 0) return fail("bad ELF magic");
    if (data[kEIClass] != 1) return fail("not ELF32");
    if (data[kEIData] != 1) return fail("not little-endian");

    const std::uint16_t e_type = rd16(data + kEType);
    if (e_type != 2 /*ET_EXEC*/ && e_type != 3 /*ET_DYN*/) return fail("not an executable ELF");
    if (rd16(data + kEMachine) != kEMArm) return fail("e_machine is not EM_ARM");

    const std::uint32_t phoff = rd32(data + kEPhoff);
    const std::uint16_t phentsize = rd16(data + kEPhentsize);
    const std::uint16_t phnum = rd16(data + kEPhnum);
    if (phnum == 0) return fail("no program headers");
    if (phentsize < kProgHeaderSize) return fail("program header entry too small");
    if (phoff > size || static_cast<std::uint64_t>(phoff) + static_cast<std::uint64_t>(phnum) * phentsize > size) {
        return fail("program header table out of bounds");
    }

    ElfImage img;
    img.entry = rd32(data + kEEntry);
    img.lowest_addr = 0xFFFFFFFFu;

    for (std::uint16_t i = 0; i < phnum; ++i) {
        const std::uint8_t* ph = data + phoff + static_cast<std::size_t>(i) * phentsize;
        if (rd32(ph + kPType) != kPtLoad) continue;

        const std::uint32_t off = rd32(ph + kPOffset);
        const std::uint32_t paddr = rd32(ph + kPPaddr);
        const std::uint32_t filesz = rd32(ph + kPFilesz);
        const std::uint32_t memsz = rd32(ph + kPMemsz);
        if (memsz == 0) continue;
        if (memsz < filesz) return fail("segment p_memsz < p_filesz");
        if (memsz > kFlashSize) return fail("segment larger than any RP2040 region");
        if (static_cast<std::uint64_t>(off) + filesz > size) return fail("segment file range out of bounds");
        if (static_cast<std::uint64_t>(paddr) + memsz > 0x100000000ull) return fail("segment wraps the address space");

        if (filesz != 0 && !mem.load(paddr, data + off, filesz)) {
            return fail("segment load address is not backed by ROM/Flash/SRAM");
        }
        if (memsz > filesz) {
            const std::vector<std::uint8_t> zeros(memsz - filesz, 0u);
            if (!mem.load(paddr + filesz, zeros.data(), zeros.size())) {
                return fail("segment BSS address is not backed by RAM");
            }
        }

        ++img.segments_loaded;
        if (paddr < img.lowest_addr) img.lowest_addr = paddr;
        if (paddr + memsz > img.highest_addr) img.highest_addr = paddr + memsz;
    }

    if (img.segments_loaded == 0) return fail("no PT_LOAD segments");
    img.ok = true;
    return img;
}

ElfImage load_elf_file(Memory& mem, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("cannot open file");
    std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
    if (buf.empty()) return fail("file is empty");
    return load_elf(mem, buf.data(), buf.size());
}

}  // namespace rp2040
