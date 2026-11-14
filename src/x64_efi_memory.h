#ifndef BOOTHUSH_X64_EFI_MEMORY_H
#define BOOTHUSH_X64_EFI_MEMORY_H

#include "x64_boot_services.h"
#include <span>

// Host-callable serialization experiment. Addresses index this RAM span; zero
// is a null pointer. This is not a CPU, firmware table, or guest call gate.
class X64EfiMemory {
public:
    using u64 = std::uint64_t;
    static constexpr u64 success = 0, invalid_parameter = 0x8000000000000002ULL,
        unsupported = 0x8000000000000003ULL, buffer_too_small = 0x8000000000000005ULL,
        out_of_resources = 0x8000000000000009ULL;
    static constexpr u64 descriptor_size = 40, descriptor_version = 1;
    X64EfiMemory(X64BootServices &model, std::span<std::uint8_t> ram, u64 image_handle)
        : model_(model), ram_(ram), image_handle_(image_handle) {}
    u64 allocate_pages(u64 allocation_type, u64 memory_type, u64 pages, u64 memory_ptr);
    u64 free_pages(u64 address, u64 pages);
    u64 get_memory_map(u64 size_ptr, u64 map_ptr, u64 key_ptr, u64 stride_ptr, u64 version_ptr);
    u64 exit_boot_services(u64 image_handle, u64 key);
private:
    X64BootServices &model_;
    std::span<std::uint8_t> ram_;
    u64 image_handle_;
    bool range(u64 address, u64 size) const;
    u64 read(u64 address, unsigned width) const;
    void write(u64 address, u64 value, unsigned width);
};
#endif
