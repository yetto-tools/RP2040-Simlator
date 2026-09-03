#include "loaders/elf_loader.h"

#include <algorithm>
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
    kEType = 16, kEMachine = 18, kEEntry = 24, kEPhoff = 28, kEShoff = 32,
    kEPhentsize = 42, kEPhnum = 44, kEShentsize = 46, kEShnum = 48, kEShstrndx = 50,
    kElfHeaderSize = 52,
};
// Program-header field offsets.
enum : std::size_t {
    kPType = 0, kPOffset = 4, kPPaddr = 12, kPFilesz = 16, kPMemsz = 20,
    kProgHeaderSize = 32,
};
// Section-header field offsets.
enum : std::size_t {
    kShName = 0, kShType = 4, kShAddr = 12, kShOffset = 16, kShSize = 20,
    kShLink = 24, kSectionHeaderSize = 40,
};
inline constexpr std::uint32_t kShtSymtab = 2;

// Symbol-table entry field offsets (Elf32_Sym).
enum : std::size_t {
    kStName = 0, kStValue = 4, kStSize = 8, kStInfo = 12, kSymEntrySize = 16,
};

inline constexpr std::uint16_t kEMArm = 40;
inline constexpr std::uint32_t kPtLoad = 1;

ElfImage fail(const char* msg) {
    ElfImage img;
    img.ok = false;
    img.error = msg;
    return img;
}

// A NUL-terminated string at `off` in a string table spanning
// [tab_off, tab_off + tab_size) of `data` (size `size`). "" if out of bounds.
std::string read_str(const std::uint8_t* data, std::size_t size,
                      std::uint32_t tab_off, std::uint32_t tab_size, std::uint32_t off) {
    if (off >= tab_size) return "";
    const std::uint64_t start = static_cast<std::uint64_t>(tab_off) + off;
    if (start >= size) return "";
    const std::uint8_t* p = data + start;
    const std::uint8_t* end = data + std::min<std::uint64_t>(size, static_cast<std::uint64_t>(tab_off) + tab_size);
    const std::uint8_t* nul = static_cast<const std::uint8_t*>(std::memchr(p, 0, static_cast<std::size_t>(end - p)));
    return nul != nullptr ? std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(nul - p)) : "";
}

// Best-effort: section headers + symbol table are not required for
// execution, so any inconsistency here just leaves img.sections/symbols
// empty rather than failing the load.
void load_sections_and_symbols(ElfImage& img, const std::uint8_t* data, std::size_t size) {
    const std::uint32_t shoff = rd32(data + kEShoff);
    const std::uint16_t shentsize = rd16(data + kEShentsize);
    const std::uint16_t shnum = rd16(data + kEShnum);
    const std::uint16_t shstrndx = rd16(data + kEShstrndx);
    if (shoff == 0 || shnum == 0 || shentsize < kSectionHeaderSize) return;
    if (static_cast<std::uint64_t>(shoff) + static_cast<std::uint64_t>(shnum) * shentsize > size) return;

    auto sh = [&](std::uint16_t i) { return data + shoff + static_cast<std::size_t>(i) * shentsize; };

    std::uint32_t shstr_off = 0, shstr_size = 0;
    if (shstrndx < shnum) {
        shstr_off = rd32(sh(shstrndx) + kShOffset);
        shstr_size = rd32(sh(shstrndx) + kShSize);
    }

    std::uint16_t symtab_idx = shnum;  // shnum == "none found"
    for (std::uint16_t i = 0; i < shnum; ++i) {
        const std::uint8_t* s = sh(i);
        ElfSection sec;
        sec.name = read_str(data, size, shstr_off, shstr_size, rd32(s + kShName));
        sec.addr = rd32(s + kShAddr);
        sec.size = rd32(s + kShSize);
        img.sections.push_back(std::move(sec));
        if (rd32(s + kShType) == kShtSymtab && symtab_idx == shnum) symtab_idx = i;
    }
    if (symtab_idx == shnum) return;  // no SHT_SYMTAB

    const std::uint8_t* symtab = sh(symtab_idx);
    const std::uint32_t sym_off = rd32(symtab + kShOffset);
    const std::uint32_t sym_size = rd32(symtab + kShSize);
    const std::uint32_t strtab_idx = rd32(symtab + kShLink);
    if (strtab_idx >= shnum) return;
    const std::uint32_t str_off = rd32(sh(static_cast<std::uint16_t>(strtab_idx)) + kShOffset);
    const std::uint32_t str_size = rd32(sh(static_cast<std::uint16_t>(strtab_idx)) + kShSize);
    if (static_cast<std::uint64_t>(sym_off) + sym_size > size) return;

    const std::uint32_t count = sym_size / kSymEntrySize;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = data + sym_off + static_cast<std::size_t>(i) * kSymEntrySize;
        ElfSymbol sym;
        sym.name = read_str(data, size, str_off, str_size, rd32(e + kStName));
        if (sym.name.empty()) continue;  // e.g. the mandatory null entry 0
        sym.value = rd32(e + kStValue);
        sym.size = rd32(e + kStSize);
        const std::uint8_t info = e[kStInfo];
        sym.bind = static_cast<std::uint8_t>(info >> 4);
        sym.type = static_cast<std::uint8_t>(info & 0xFu);
        img.symbols.push_back(std::move(sym));
    }
}

}  // namespace

const ElfSymbol* ElfImage::symbol_at(std::uint32_t addr) const {
    addr &= ~std::uint32_t{1};  // Thumb bit
    const ElfSymbol* best = nullptr;
    std::uint32_t best_span = 0;
    for (const ElfSymbol& s : symbols) {
        const std::uint32_t value = s.value & ~std::uint32_t{1};
        const std::uint32_t span = s.size == 0 ? 1u : s.size;
        if (addr < value || addr - value >= span) continue;
        // Prefer a sized symbol over a size-0 marker, then the tightest
        // enclosing span (e.g. a FUNC over a whole section's worth of range).
        const bool better = best == nullptr ||
                             (s.size > 0 && best->size == 0) ||
                             (s.size > 0 && best->size > 0 && span < best_span);
        if (better) { best = &s; best_span = span; }
    }
    return best;
}

const ElfSymbol* ElfImage::symbol_named(const std::string& name) const {
    for (const ElfSymbol& s : symbols) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

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
    load_sections_and_symbols(img, data, size);
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
