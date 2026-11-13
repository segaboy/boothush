#include "x64_pe_loader.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
void put(std::vector<std::uint8_t> &v, std::size_t offset, std::uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i) v[offset + i] = std::uint8_t(value >> (i * 8));
}
std::uint64_t get64(const std::vector<std::uint8_t> &v, std::size_t off)
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(v[off + i]) << (8 * i);
    return value;
}
std::vector<std::uint8_t> image()
{
    std::vector<std::uint8_t> f(0x600);
    f[0] = 'M'; f[1] = 'Z'; put(f, 0x3c, 0x80, 4);
    f[0x80] = 'P'; f[0x81] = 'E'; put(f, 0x84, 0x8664, 2); put(f, 0x86, 2, 2);
    put(f, 0x94, 240, 2);
    constexpr unsigned h = 0x98, table = h + 240;
    put(f, h, 0x20b, 2); put(f, h + 16, 0x1000, 4); put(f, h + 24, 0x10000000, 8);
    put(f, h + 56, 0x3000, 4); put(f, h + 60, 0x200, 4); put(f, h + 68, 10, 2);
    put(f, h + 108, 16, 4); put(f, h + 112 + 5 * 8, 0x2000, 4);
    put(f, h + 112 + 5 * 8 + 4, 12, 4);
    put(f, table + 8, 0x200, 4); put(f, table + 12, 0x1000, 4);
    put(f, table + 16, 0x200, 4); put(f, table + 20, 0x200, 4); put(f, table + 36, 0x60000020, 4);
    put(f, table + 40 + 8, 0x200, 4); put(f, table + 40 + 12, 0x2000, 4);
    put(f, table + 40 + 16, 0x200, 4); put(f, table + 40 + 20, 0x400, 4);
    f[0x200] = 0xc3; // Original synthetic return instruction; never executed.
    put(f, 0x220, 0x10000088, 8);
    put(f, 0x400, 0x1000, 4); put(f, 0x404, 12, 4); put(f, 0x408, 0xa020, 2);
    return f;
}
int main()
{
    const auto original = image();
    X64PeImage map;
    require(!map.load(original, 0x10000000), "preferred mapping failed");
    require(map.bytes().size() == 0x3000 && map.bytes()[0x1000] == 0xc3, "section data incorrect");
    require(map.bytes()[0x1800] == 0 && get64(map.bytes(), 0x1020) == 0x10000088, "zero fill or preferred pointer incorrect");
    require(!map.load(original, 0x20000000), "relocated mapping failed");
    require(map.entry_address() == 0x20001000 && map.relocations() == 1, "entry or relocation count incorrect");
    require(get64(map.bytes(), 0x1020) == 0x20000088, "DIR64 relocation incorrect");
    const auto previous = map.bytes();
    unsigned negative_case = 0;
    auto expect_bad = [&](const std::vector<std::uint8_t> &bad, std::uint64_t base) {
        ++negative_case;
        if (map.load(bad, base) == nullptr) std::cerr << "accepted negative case " << negative_case << '\n';
        else { require(map.bytes() == previous && map.loaded_base() == 0x20000000, "failed map mutated prior output"); return; }
        require(false, "malformed image accepted");
    };
    auto bad = original; bad[0] = 0; expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x84, 0x014c, 2); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x98 + 240 + 40 + 12, 0x1000, 4); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x98 + 240 + 20, 0x5f0, 4); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x98 + 16, 0x2000, 4); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x408, 0x3020, 2); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x404, 11, 4); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x400, 0x3000, 4); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x400, 0x2000, 4); put(bad, 0x408, 0xa000, 2); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x98 + 108, 17, 4); expect_bad(bad, 0x20000000);
    bad = original; put(bad, 0x98 + 112 + 5 * 8, 0, 4); put(bad, 0x98 + 112 + 5 * 8 + 4, 0, 4);
    expect_bad(bad, 0x20000000);
    require(!map.load(bad, 0x10000000), "fixed preferred image rejected");
    require(map.relocations() == 0, "fixed image reported relocations");
    X64PeImage other;
    require(other.load(original, 0x20000001) != nullptr, "unaligned base accepted");
    require(other.load(original, UINT64_C(0xfffffffffffff000)) != nullptr, "overflowing base accepted");
    return 0;
}
