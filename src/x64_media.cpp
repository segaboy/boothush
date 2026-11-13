#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#include <windows.h>
#include <bcrypt.h>

#include "x64_media.h"
#include "x64_media_internal.h"
#include "x64_pe_loader.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace {
constexpr size_t sector = 512;
constexpr size_t image_size = 64 * 1024 * 1024;
constexpr std::array<uint8_t, 16> esp_guid{0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                                            0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};

uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
uint64_t u64(const uint8_t *p) { return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32; }

uint32_t crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

bool hash_bytes(const uint8_t *bytes, size_t size, uint8_t result[32])
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    const auto status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes),
                                   static_cast<ULONG>(size), result, 32);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0;
}

bool valid_gpt_header(const uint8_t *image, uint64_t lba, uint64_t other)
{
    const uint8_t *h = image + lba * sector;
    const uint32_t length = u32(h + 12);
    if (std::memcmp(h, "EFI PART", 8) || u32(h + 8) != 0x10000 || length < 92 || length > sector ||
        u32(h + 20) != 0 || u64(h + 24) != lba || u64(h + 32) != other) return false;
    std::array<uint8_t, sector> copy{};
    std::copy_n(h, length, copy.begin());
    const uint32_t expected = u32(copy.data() + 16);
    std::fill_n(copy.data() + 16, 4, 0);
    return crc32(copy.data(), length) == expected;
}

struct Fat {
    const uint8_t *image;
    size_t base;
    uint32_t total, fat_start, fat_sectors, data_start, cluster_count;
    uint8_t sectors_per_cluster;

    size_t cluster_size() const { return size_t(sectors_per_cluster) * sector; }
    bool valid_cluster(uint32_t c) const { return c >= 2 && c - 2 < cluster_count; }
    const uint8_t *cluster_data(uint32_t c) const {
        return image + base + size_t(data_start + (c - 2) * sectors_per_cluster) * sector;
    }
    uint32_t next(uint32_t c) const { return u32(image + base + size_t(fat_start) * sector + size_t(c) * 4) & 0x0fffffff; }

    bool find(uint32_t directory, const char name[11], bool want_directory,
              uint32_t &first, uint32_t &length) const
    {
        std::vector<uint8_t> seen(cluster_count, 0);
        bool found = false;
        for (uint32_t c = directory; valid_cluster(c); c = next(c)) {
            if (seen[c - 2]) return false;
            seen[c - 2] = 1;
            const uint8_t *data = cluster_data(c);
            for (size_t offset = 0; offset < cluster_size(); offset += 32) {
                const uint8_t *entry = data + offset;
                if (entry[0] == 0) return found;
                if (entry[0] == 0xe5 || (entry[11] & 0x0f) == 0x0f || (entry[11] & 8)) continue;
                if (std::memcmp(entry, name, 11) != 0) continue;
                if (found || ((entry[11] & 0x10) != 0) != want_directory) return false;
                first = (uint32_t(u16(entry + 20)) << 16) | u16(entry + 26);
                length = u32(entry + 28);
                if (!valid_cluster(first)) return false;
                found = true;
            }
            const auto following = next(c);
            if (following >= 0x0ffffff8) return found;
            if (!valid_cluster(following)) return false;
        }
        return false;
    }

    bool read_file(uint32_t first, uint32_t size, std::vector<uint8_t> &file) const
    {
        if (!size || size > 16 * 1024 * 1024) return false;
        file.reserve(size);
        std::vector<uint8_t> seen(cluster_count, 0);
        uint32_t c = first;
        while (file.size() < size) {
            if (!valid_cluster(c) || seen[c - 2]) return false;
            seen[c - 2] = 1;
            const auto bytes = std::min(cluster_size(), size_t(size) - file.size());
            const auto *source = cluster_data(c);
            file.insert(file.end(), source, source + bytes);
            c = next(c);
        }
        return c >= 0x0ffffff8;
    }
};

bool valid_efi(const std::vector<uint8_t> &file, uint32_t &entry)
{
    if (file.size() < 0x40 || file[0] != 'M' || file[1] != 'Z') return false;
    const uint32_t pe = u32(file.data() + 0x3c);
    if (pe > file.size() - 24 || std::memcmp(file.data() + pe, "PE\0\0", 4)) return false;
    const auto *coff = file.data() + pe + 4;
    const uint16_t sections = u16(coff + 2), optional_size = u16(coff + 16);
    const size_t optional = size_t(pe) + 24;
    if (u16(coff) != 0x8664 || sections == 0 || sections > 96 || optional_size < 112 ||
        optional + optional_size > file.size() || size_t(sections) * 40 > file.size() - optional - optional_size) return false;
    const uint8_t *header = file.data() + optional;
    if (u16(header) != 0x20b || u16(header + 68) != 10) return false;
    entry = u32(header + 16);
    const uint32_t image_bytes = u32(header + 56);
    if (!entry || entry >= image_bytes || !image_bytes || image_bytes > 16 * 1024 * 1024) return false;
    bool executable_entry = false;
    const uint8_t *table = header + optional_size;
    for (unsigned i = 0; i < sections; ++i) {
        const uint8_t *s = table + i * 40;
        const uint32_t va = u32(s + 12), virtual_size = u32(s + 8), raw_size = u32(s + 16), raw_offset = u32(s + 20);
        if (raw_offset > file.size() || raw_size > file.size() - raw_offset ||
            va > image_bytes || virtual_size > image_bytes - va) return false;
        if ((u32(s + 36) & 0x20000000) && entry >= va && entry - va < std::max(virtual_size, raw_size)) executable_entry = true;
    }
    return executable_entry;
}
} // namespace

static const char *verify_media(const uint8_t *image, size_t size, const uint8_t *expected_efi,
                                size_t expected_size, x64_media_info *info, std::vector<uint8_t> *extracted)
{
    if (!image || !info || size != image_size) return "x64 disk must be a 67108864-byte raw image";
    try {
        if (image[510] != 0x55 || image[511] != 0xaa || image[450] != 0xee || u32(image + 454) != 1)
            return "invalid protective MBR";
        const uint64_t last = size / sector - 1;
        if (!valid_gpt_header(image, 1, last) || !valid_gpt_header(image, last, 1)) return "invalid GPT header or CRC";
        const uint8_t *primary = image + sector, *backup = image + last * sector;
        const uint32_t count = u32(primary + 80), entry_size = u32(primary + 84);
        if (!count || count > 4096 || entry_size < 128 || entry_size > 4096 ||
            count != u32(backup + 80) || entry_size != u32(backup + 84) ||
            std::memcmp(primary + 56, backup + 56, 16) != 0) return "unsupported GPT entry table";
        const uint64_t bytes = uint64_t(count) * entry_size;
        const uint64_t a = u64(primary + 72), b = u64(backup + 72);
        if (a >= size / sector || b >= size / sector || bytes > size - a * sector || bytes > size - b * sector ||
            crc32(image + a * sector, size_t(bytes)) != u32(primary + 88) ||
            crc32(image + b * sector, size_t(bytes)) != u32(backup + 88) ||
            std::memcmp(image + a * sector, image + b * sector, size_t(bytes))) return "invalid GPT entry CRC or mirror";
        uint64_t first = 0, end = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *entry = image + a * sector + size_t(i) * entry_size;
            if (std::memcmp(entry, esp_guid.data(), esp_guid.size())) continue;
            if (first) return "multiple EFI system partitions are unsupported";
            first = u64(entry + 32); end = u64(entry + 40);
        }
        if (first < u64(primary + 40) || end > u64(primary + 48) || end < first || end >= last)
            return "invalid or missing EFI system partition";
        const uint8_t *boot = image + first * sector;
        const uint16_t reserved = u16(boot + 14);
        const uint8_t per_cluster = boot[13], copies = boot[16];
        const uint32_t total = u32(boot + 32), fat_size = u32(boot + 36), root = u32(boot + 44);
        if (boot[510] != 0x55 || boot[511] != 0xaa || u16(boot + 11) != sector || !per_cluster ||
            (per_cluster & (per_cluster - 1)) || per_cluster > 128 || !reserved || copies != 2 ||
            u16(boot + 17) || u16(boot + 19) || u16(boot + 22) || !fat_size ||
            total > end - first + 1 || total <= uint64_t(reserved) + uint64_t(copies) * fat_size)
            return "invalid or unsupported FAT32 boot sector";
        const uint32_t data_start = reserved + copies * fat_size;
        const uint32_t clusters = (total - data_start) / per_cluster;
        if (clusters < 65525 || uint64_t(clusters + 2) * 4 > uint64_t(fat_size) * sector)
            return "invalid FAT32 cluster geometry";
        const size_t base = size_t(first) * sector;
        const auto *fat1 = image + base + size_t(reserved) * sector;
        if (std::memcmp(fat1, fat1 + size_t(fat_size) * sector, size_t(fat_size) * sector))
            return "FAT32 copies differ";
        Fat fat{image, base, total, reserved, fat_size, data_start, clusters, per_cluster};
        uint32_t directory, ignored, boot_directory, file_cluster, file_size;
        if (!fat.find(root, "EFI        ", true, directory, ignored) ||
            !fat.find(directory, "BOOT       ", true, boot_directory, ignored) ||
            !fat.find(boot_directory, "BOOTX64 EFI", false, file_cluster, file_size))
            return "EFI/BOOT/BOOTX64.EFI missing or invalid";
        std::vector<uint8_t> efi;
        if (!fat.read_file(file_cluster, file_size, efi)) return "invalid EFI file cluster chain";
        uint32_t entry;
        if (!valid_efi(efi, entry)) return "EFI file is not a supported PE32+ x86-64 application";
        if (expected_efi && (expected_size != efi.size() || std::memcmp(expected_efi, efi.data(), efi.size())))
            return "disk EFI payload differs from expected file";
        if (!hash_bytes(image, size, info->image_sha256) || !hash_bytes(efi.data(), efi.size(), info->efi_sha256))
            return "cannot calculate SHA-256";
        info->partition_lba = first;
        info->partition_sectors = static_cast<uint32_t>(end - first + 1);
        info->efi_size = file_size;
        info->entry_rva = entry;
        if (extracted) *extracted = std::move(efi);
        return nullptr;
    } catch (const std::bad_alloc &) {
        return "cannot allocate x64 media inspection buffers";
    }
}

const char *x64_media_verify(const uint8_t *image, size_t size, const uint8_t *expected_efi,
                             size_t expected_size, x64_media_info *info)
{
    return verify_media(image, size, expected_efi, expected_size, info, nullptr);
}

const char *x64_media_extract_efi(const uint8_t *image, size_t size,
                                  x64_media_info &info, std::vector<uint8_t> &efi)
{
    return verify_media(image, size, nullptr, 0, &info, &efi);
}

const char *x64_media_map(const uint8_t *image, size_t size, uint64_t base, x64_mapped_media_info *info)
{
    if (!info) return "missing mapped image result";
    x64_mapped_media_info result{};
    std::vector<uint8_t> efi;
    if (const char *error = x64_media_extract_efi(image, size, result.media, efi)) return error;
    X64PeImage mapped;
    if (const char *error = mapped.load(efi, base)) return error;
    if (!hash_bytes(mapped.bytes().data(), mapped.bytes().size(), result.mapped_sha256)) return "cannot calculate SHA-256";
    result.preferred_base = mapped.preferred_base(); result.loaded_base = mapped.loaded_base();
    result.entry_address = mapped.entry_address(); result.image_size = static_cast<uint32_t>(mapped.bytes().size());
    result.relocations = mapped.relocations();
    *info = result;
    return nullptr;
}
