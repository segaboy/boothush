#include "x64_boot_services.h"

#include <array>
#include <cstdlib>
#include <iostream>

using Model = X64BootServices;
using Status = Model::Status;

void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
    Model invalid(1, 4);
    require(!invalid.valid(), "unaligned initial range accepted");
    Model model(0x100000, 256);
    require(model.valid(), "valid range rejected");
    std::size_t required = 0;
    std::uint64_t key = 0;
    require(model.memory_map({}, required, key) == Status::buffer_too_small && required == 1 && key == 0,
            "short memory-map query changed key or did not report size");
    std::array<Model::Region, 8> map{};
    require(model.memory_map(map, required, key) == Status::ok && required == 1, "initial map failed");
    require(map[0].address == 0x100000 && map[0].pages == 256 && map[0].type == Model::Type::conventional,
            "initial conventional range incorrect");
    const auto initial_key = key;
    std::uint64_t address = 0xdeadbeef;
    require(model.allocate_max(0x17f000, 2, address) == Status::ok && address == 0x17d000,
            "allocation exceeded inclusive unaligned maximum");
    require(model.exit_boot_services(initial_key) == Status::stale_key && !model.exited(),
            "stale key was accepted or exited firmware");
    require(model.memory_map(map, required, key) == Status::ok && key != initial_key && required == 3,
            "allocation did not update map and key");
    require(map[0].address == 0x100000 && map[0].pages == 125 && map[0].type == Model::Type::conventional,
            "lower free range incorrect");
    require(map[1].address == 0x17d000 && map[1].pages == 2 && map[1].type == Model::Type::loader_data,
            "allocated range incorrect");
    require(map[2].address == 0x17f000 && map[2].pages == 129 && map[2].type == Model::Type::conventional,
            "upper free range incorrect");
    const auto allocated_key = key;
    std::uint64_t unchanged = 77;
    require(model.allocate_max(0x100fff, 2, unchanged) == Status::out_of_resources && unchanged == 77,
            "failed allocation mutated output");
    require(model.free_pages(address, 1) == Status::invalid, "partial free unexpectedly accepted");
    require(model.memory_map(map, required, key) == Status::ok && key == allocated_key,
            "failed mutation changed map key");
    require(model.free_pages(address, 2) == Status::ok, "exact free failed");
    require(model.memory_map(map, required, key) == Status::ok && required == 1 && map[0].pages == 256,
            "free did not merge adjacent ranges");
    require(model.exit_boot_services(allocated_key) == Status::stale_key, "free did not invalidate key");
    const auto free_key = key;
    require(model.allocate_max(UINT64_C(0x7fffffffffff), 128, address) == Status::ok && address == 0x180000,
            "128-page candidate bundle allocation failed");
    require(model.exit_boot_services(free_key) == Status::stale_key, "bundle allocation did not invalidate key");
    require(model.memory_map(map, required, key) == Status::ok && required == 2, "bundle map failed");
    require(model.exit_boot_services(key) == Status::ok && model.exited(), "current key did not exit");
    require(model.memory_map(map, required, key) == Status::exited, "map available after exit");
    require(model.allocate_max(UINT64_MAX, 1, address) == Status::exited, "allocation allowed after exit");
    require(model.free_pages(0x100000, 1) == Status::exited, "free allowed after exit");
    require(model.exit_boot_services(key) == Status::exited, "second exit allowed");
    return 0;
}
