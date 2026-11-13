#ifndef BOOTHUSH_X64_BOOT_SERVICES_H
#define BOOTHUSH_X64_BOOT_SERVICES_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Host-side model only: no EFI ABI, guest pointers, RAM mapping, or CPU.
class X64BootServices {
public:
    enum class Status { ok, invalid, buffer_too_small, out_of_resources, stale_key, exited };
    enum class Type : std::uint32_t { loader_data = 2, conventional = 7 };
    struct Region { std::uint64_t address, pages; Type type; };
    static constexpr std::uint64_t page_size = 4096;

    X64BootServices(std::uint64_t address, std::uint64_t pages);
    bool valid() const { return valid_; }
    bool exited() const { return exited_; }
    Status memory_map(std::span<Region> output, std::size_t &required, std::uint64_t &key) const;
    Status allocate_max(std::uint64_t max_address, std::uint64_t pages, std::uint64_t &address);
    // This narrow model accepts exact allocations, not partial frees.
    Status free_pages(std::uint64_t address, std::uint64_t pages);
    Status exit_boot_services(std::uint64_t key);

private:
    std::vector<Region> regions_;
    std::uint64_t key_ = 1;
    bool valid_ = false, exited_ = false;
};

#endif
