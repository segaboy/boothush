#include "x64_efi_memory.h"
#include <algorithm>
#include <array>
#include <new>
#include <vector>

using Status = X64BootServices::Status;
using u64 = X64EfiMemory::u64;

bool X64EfiMemory::range(u64 p, u64 n) const {
    return p != 0 && p <= ram_.size() && n <= ram_.size() - p;
}
u64 X64EfiMemory::read(u64 p, unsigned n) const {
    u64 v = 0; for (unsigned i = 0; i < n; ++i) v |= u64(ram_[size_t(p+i)]) << (8*i); return v;
}
void X64EfiMemory::write(u64 p, u64 v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) ram_[size_t(p+i)] = std::uint8_t(v >> (8*i));
}
u64 X64EfiMemory::allocate_pages(u64 type, u64 memory_type, u64 pages, u64 ptr) {
    if (!range(ptr, 8) || !pages) return invalid_parameter;
    if (type != 1 || memory_type != 2) return unsupported; // AllocateMaxAddress, LoaderData only.
    u64 address = 0;
    const auto status = model_.allocate_max(read(ptr, 8), pages, address);
    if (status == Status::ok) { write(ptr, address, 8); return success; }
    if (status == Status::out_of_resources) return out_of_resources;
    return invalid_parameter;
}
u64 X64EfiMemory::free_pages(u64 address, u64 pages) {
    const auto status = model_.free_pages(address, pages);
    return status == Status::ok ? success : invalid_parameter;
}
u64 X64EfiMemory::get_memory_map(u64 size_ptr, u64 map_ptr, u64 key_ptr, u64 stride_ptr, u64 version_ptr) {
    if (!range(size_ptr,8)) return invalid_parameter;
    size_t count = 0; u64 key = 0;
    auto status = model_.memory_map({}, count, key);
    if (status != Status::buffer_too_small || count > 4096) return invalid_parameter;
    const u64 bytes = u64(count) * descriptor_size;
    const u64 capacity = read(size_ptr,8);
    if (capacity < bytes) { write(size_ptr,bytes,8); return buffer_too_small; }
    if (!range(map_ptr,bytes) || !range(key_ptr,8) || !range(stride_ptr,8) || !range(version_ptr,4))
        return invalid_parameter;
    // Reject aliasing to make publication atomic and unambiguous.
    const std::array<std::pair<u64,u64>,5> ranges{{{size_ptr,8},{map_ptr,bytes},{key_ptr,8},{stride_ptr,8},{version_ptr,4}}};
    for (size_t i=0;i<ranges.size();++i) for (size_t j=i+1;j<ranges.size();++j)
        if (ranges[i].first < ranges[j].first+ranges[j].second && ranges[j].first < ranges[i].first+ranges[i].second)
            return invalid_parameter;
    std::vector<X64BootServices::Region> regions;
    try { regions.resize(count); } catch (const std::bad_alloc &) { return out_of_resources; }
    status = model_.memory_map(regions,count,key);
    if (status != Status::ok) return invalid_parameter;
    for (size_t i=0;i<count;++i) {
        const auto &r=regions[i]; const u64 p=map_ptr+i*descriptor_size;
        write(p,static_cast<u64>(r.type),4); write(p+4,0,4); write(p+8,r.address,8);
        write(p+16,0,8); write(p+24,r.pages,8); write(p+32,0,8);
    }
    write(size_ptr,bytes,8); write(key_ptr,key,8); write(stride_ptr,descriptor_size,8);
    write(version_ptr,descriptor_version,4); return success;
}
u64 X64EfiMemory::exit_boot_services(u64 image, u64 key) {
    if (!image_handle_ || image != image_handle_) return invalid_parameter;
    return model_.exit_boot_services(key) == Status::ok ? success : invalid_parameter;
}
