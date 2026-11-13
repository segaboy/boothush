#include "x64_boot_services.h"

#include <algorithm>
#include <limits>
#include <new>

X64BootServices::X64BootServices(std::uint64_t address, std::uint64_t pages)
{
    if ((address % page_size) || !pages || pages > (UINT64_MAX - address) / page_size) return;
    try {
        regions_.push_back({address, pages, Type::conventional});
        valid_ = true;
    } catch (const std::bad_alloc &) { }
}

X64BootServices::Status X64BootServices::memory_map(std::span<Region> output, std::size_t &required,
                                                    std::uint64_t &key) const
{
    if (!valid_) return Status::invalid;
    if (exited_) return Status::exited;
    required = regions_.size();
    if (output.size() < required) return Status::buffer_too_small;
    std::copy(regions_.begin(), regions_.end(), output.begin());
    key = key_;
    return Status::ok;
}

X64BootServices::Status X64BootServices::allocate_max(std::uint64_t max_address, std::uint64_t pages,
                                                      std::uint64_t &address)
{
    if (!valid_ || !pages) return Status::invalid;
    if (exited_) return Status::exited;
    if (key_ == UINT64_MAX) return Status::out_of_resources;
    // The maximum is an inclusive byte address; a partial page cannot fit.
    const std::uint64_t last_end_page = max_address / page_size +
                                        (max_address % page_size == page_size - 1 ? 1 : 0);
    for (std::size_t i = regions_.size(); i-- > 0;) {
        const Region old = regions_[i];
        if (old.type != Type::conventional) continue;
        const std::uint64_t first = old.address / page_size;
        const std::uint64_t end = std::min(first + old.pages, last_end_page);
        if (end < first || end - first < pages) continue;
        const std::uint64_t chosen = end - pages;
        try {
            auto replacement = regions_;
            auto it = replacement.begin() + i;
            it = replacement.erase(it);
            if (chosen != first) it = replacement.insert(it, {old.address, chosen - first, Type::conventional}) + 1;
            it = replacement.insert(it, {chosen * page_size, pages, Type::loader_data}) + 1;
            if (end != first + old.pages)
                replacement.insert(it, {end * page_size, first + old.pages - end, Type::conventional});
            regions_.swap(replacement);
        } catch (const std::bad_alloc &) { return Status::out_of_resources; }
        address = chosen * page_size;
        ++key_;
        return Status::ok;
    }
    return Status::out_of_resources;
}

X64BootServices::Status X64BootServices::free_pages(std::uint64_t address, std::uint64_t pages)
{
    if (!valid_ || !pages || address % page_size) return Status::invalid;
    if (exited_) return Status::exited;
    if (key_ == UINT64_MAX) return Status::out_of_resources;
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        auto &region = regions_[i];
        if (region.type != Type::loader_data || region.address != address || region.pages != pages) continue;
        region.type = Type::conventional;
        if (i > 0 && regions_[i - 1].type == Type::conventional) {
            regions_[i - 1].pages += regions_[i].pages;
            regions_.erase(regions_.begin() + i);
            --i;
        }
        if (i + 1 < regions_.size() && regions_[i + 1].type == Type::conventional) {
            regions_[i].pages += regions_[i + 1].pages;
            regions_.erase(regions_.begin() + i + 1);
        }
        ++key_;
        return Status::ok;
    }
    return Status::invalid;
}

X64BootServices::Status X64BootServices::exit_boot_services(std::uint64_t key)
{
    if (!valid_) return Status::invalid;
    if (exited_) return Status::exited;
    if (key != key_) return Status::stale_key;
    exited_ = true;
    return Status::ok;
}
