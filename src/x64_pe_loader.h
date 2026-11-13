#ifndef BOOTHUSH_X64_PE_LOADER_H
#define BOOTHUSH_X64_PE_LOADER_H

#include <cstdint>
#include <span>
#include <vector>

// Maps an original PE32+ EFI application into host memory; never executes it.
class X64PeImage {
public:
    const char *load(std::span<const std::uint8_t> file, std::uint64_t base);
    const std::vector<std::uint8_t> &bytes() const { return bytes_; }
    std::uint64_t preferred_base() const { return preferred_base_; }
    std::uint64_t loaded_base() const { return loaded_base_; }
    std::uint64_t entry_address() const { return loaded_base_ + entry_rva_; }
    std::uint32_t relocations() const { return relocations_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint64_t preferred_base_ = 0, loaded_base_ = 0;
    std::uint32_t entry_rva_ = 0, relocations_ = 0;
};
#endif
