#include "loaders/uf2_loader.h"

#include <fstream>
#include <iterator>
#include <vector>

namespace rp2040 {

namespace {

std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// UF2 block layout (all little-endian).
enum : std::size_t {
    kMagicStart0 = 0, kMagicStart1 = 4, kFlags = 8, kTargetAddr = 12,
    kPayloadSize = 16, kBlockNo = 20, kNumBlocks = 24, kFileSizeOrFamily = 28,
    kDataStart = 32, kMagicEnd = 508,
};
inline constexpr std::size_t kBlockSize = 512;
inline constexpr std::size_t kMaxPayload = 476;

inline constexpr std::uint32_t kMagic0 = 0x0A324655u;   // "UF2\n"
inline constexpr std::uint32_t kMagic1 = 0x9E5D5157u;
inline constexpr std::uint32_t kMagicEndVal = 0x0AB16F30u;

inline constexpr std::uint32_t kFlagNotMainFlash = 0x00000001u;
inline constexpr std::uint32_t kFlagFileContainer = 0x00001000u;
inline constexpr std::uint32_t kFlagFamilyIdPresent = 0x00002000u;

Uf2Image fail(const char* msg) {
    Uf2Image img;
    img.ok = false;
    img.error = msg;
    return img;
}

}  // namespace

Uf2Image load_uf2(Memory& mem, const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) return fail("empty UF2 image");
    if (size % kBlockSize != 0) return fail("UF2 size is not a multiple of 512 bytes");

    const std::size_t block_count = size / kBlockSize;

    Uf2Image img;
    img.lowest_addr = 0xFFFFFFFFu;

    for (std::size_t i = 0; i < block_count; ++i) {
        const std::uint8_t* b = data + i * kBlockSize;

        if (rd32(b + kMagicStart0) != kMagic0 || rd32(b + kMagicStart1) != kMagic1) {
            return fail("bad UF2 start magic");
        }
        if (rd32(b + kMagicEnd) != kMagicEndVal) return fail("bad UF2 end magic");

        const std::uint32_t flags = rd32(b + kFlags);
        const std::uint32_t addr = rd32(b + kTargetAddr);
        const std::uint32_t payload = rd32(b + kPayloadSize);
        const std::uint32_t num_blocks = rd32(b + kNumBlocks);

        if (num_blocks != block_count) return fail("UF2 numBlocks disagrees with the file length");

        if (flags & kFlagFileContainer) return fail("UF2 file-container images are not supported");

        if (flags & kFlagFamilyIdPresent) {
            img.family_id = rd32(b + kFileSizeOrFamily);
            if (img.family_id != kUf2FamilyRp2040) {
                return fail("UF2 family ID is not RP2040 (0xE48BFF56)");
            }
        }

        if (flags & kFlagNotMainFlash) {
            ++img.blocks_skipped;
            continue;
        }

        if (payload == 0) continue;
        if (payload > kMaxPayload) return fail("UF2 block payload exceeds 476 bytes");
        if (static_cast<std::uint64_t>(addr) + payload > 0x100000000ull) {
            return fail("UF2 block wraps the address space");
        }

        if (!mem.load(addr, b + kDataStart, payload)) {
            return fail("UF2 target address is not backed by ROM/Flash/SRAM");
        }

        ++img.blocks_loaded;
        if (addr < img.lowest_addr) img.lowest_addr = addr;
        if (addr + payload > img.highest_addr) img.highest_addr = addr + payload;
    }

    if (img.blocks_loaded == 0) return fail("UF2 image has no payload for main flash");
    img.ok = true;
    return img;
}

Uf2Image load_uf2_file(Memory& mem, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("cannot open file");
    std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
    if (buf.empty()) return fail("file is empty");
    return load_uf2(mem, buf.data(), buf.size());
}

}  // namespace rp2040
