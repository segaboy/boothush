#include "x64_pe_loader.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace {
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
uint64_t u64(const uint8_t *p) { return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32; }
void put64(uint8_t *p, uint64_t value) { for (unsigned i = 0; i < 8; ++i) p[i] = uint8_t(value >> (i * 8)); }
bool inside(uint64_t offset, uint64_t size, uint64_t limit) { return offset <= limit && size <= limit - offset; }
}

const char *X64PeImage::load(std::span<const uint8_t> file, uint64_t base)
{
    if (file.size() < 64 || file[0] != 'M' || file[1] != 'Z') return "invalid PE DOS header";
    const uint32_t pe = u32(file.data() + 0x3c);
    if (!inside(pe, 24, file.size()) || std::memcmp(file.data() + pe, "PE\0\0", 4)) return "invalid PE signature";
    const auto *coff = file.data() + pe + 4;
    const uint16_t section_count = u16(coff + 2), optional_size = u16(coff + 16);
    const size_t optional = size_t(pe) + 24;
    if (u16(coff) != 0x8664 || !section_count || section_count > 96 || optional_size < 112 ||
        !inside(optional, optional_size, file.size()) ||
        !inside(optional + optional_size, size_t(section_count) * 40, file.size()))
        return "invalid PE section table";
    const auto *header = file.data() + optional;
    if (u16(header) != 0x20b || u16(header + 68) != 10) return "not a PE32+ x86-64 EFI application";
    const uint64_t preferred = u64(header + 24);
    const uint32_t entry = u32(header + 16), image_size = u32(header + 56), headers_size = u32(header + 60);
    if (!image_size || image_size > 16 * 1024 * 1024 || !entry || entry >= image_size ||
        !headers_size || headers_size > file.size() || headers_size > image_size ||
        optional + optional_size + size_t(section_count) * 40 > headers_size ||
        (base & 0xfff) || base > UINT64_MAX - image_size)
        return "invalid PE image dimensions or load base";

    try {
        std::vector<uint8_t> mapped(image_size, 0);
        std::copy_n(file.data(), headers_size, mapped.data());
        struct Range { uint32_t begin, end; };
        std::vector<Range> ranges;
        ranges.push_back({0, headers_size});
        bool executable_entry = false;
        const auto *sections = header + optional_size;
        for (unsigned i = 0; i < section_count; ++i) {
            const auto *section = sections + i * 40;
            const uint32_t virtual_size = u32(section + 8), va = u32(section + 12);
            const uint32_t raw_size = u32(section + 16), raw = u32(section + 20);
            const uint32_t span = std::max(virtual_size, raw_size);
            if (!inside(va, span, image_size) || !inside(raw, raw_size, file.size()) ||
                (raw_size && raw < headers_size)) return "PE section exceeds file or image";
            for (const auto &range : ranges) {
                if (span && va < range.end && range.begin < uint64_t(va) + span)
                    return "overlapping PE sections";
            }
            if (span) ranges.push_back({va, va + span});
            if (raw_size) std::copy_n(file.data() + raw, raw_size, mapped.data() + va);
            if ((u32(section + 36) & 0x20000000) && entry >= va && entry - va < span)
                executable_entry = true;
        }
        if (!executable_entry) return "PE entry point is not executable";
        const uint32_t directories = u32(header + 108);
        if (directories > (optional_size - 112) / 8) return "invalid PE data directories";
        uint32_t reloc_rva = 0, reloc_size = 0, relocations = 0;
        if (directories > 5) {
            reloc_rva = u32(header + 112 + 5 * 8);
            reloc_size = u32(header + 112 + 5 * 8 + 4);
        }
        if ((reloc_rva == 0) != (reloc_size == 0) || !inside(reloc_rva, reloc_size, image_size))
            return "invalid PE relocation directory";
        if (base != preferred && (!reloc_size || (u16(coff + 18) & 1)))
            return "PE image cannot be relocated";
        const uint64_t delta = base - preferred;
        size_t position = reloc_rva;
        const size_t end = size_t(reloc_rva) + reloc_size;
        while (position < end) {
            if (!inside(position, 8, end)) return "truncated PE relocation block";
            const uint32_t page = u32(mapped.data() + position), size = u32(mapped.data() + position + 4);
            if ((page & 0xfff) || size < 8 || (size & 3) || !inside(position, size, end))
                return "invalid PE relocation block";
            for (size_t offset = position + 8; offset < position + size; offset += 2) {
                const uint16_t item = u16(mapped.data() + offset), type = item >> 12;
                if (type == 0) continue;
                if (type != 10) return "unsupported PE relocation type";
                const uint64_t target = uint64_t(page) + (item & 0xfff);
                if (!inside(target, 8, image_size) || (target < end && uint64_t(reloc_rva) < target + 8))
                    return "invalid PE relocation target";
                bool in_mapped_range = false;
                for (const auto &range : ranges)
                    if (target >= range.begin && target + 8 <= range.end) { in_mapped_range = true; break; }
                if (!in_mapped_range) return "PE relocation target is outside mapped sections";
                put64(mapped.data() + target, u64(mapped.data() + target) + delta);
                ++relocations;
            }
            position += size;
        }
        bytes_ = std::move(mapped);
        preferred_base_ = preferred; loaded_base_ = base; entry_rva_ = entry; relocations_ = relocations;
        return nullptr;
    } catch (const std::bad_alloc &) { return "cannot allocate PE image"; }
}
