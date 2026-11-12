#include "pcat_floppy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool result, const char *message)
{
    if (!result) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void command(PcatFloppy &fdc, std::initializer_list<std::uint8_t> bytes)
{
    for (const auto byte : bytes) {
        require((fdc.read_port(0x3f4) & 0xc0) == 0x80, "FDC did not accept a command byte");
        fdc.write_port(0x3f5, byte);
    }
}

void expect_result(PcatFloppy &fdc, std::initializer_list<std::uint8_t> bytes)
{
    for (const auto byte : bytes) {
        require(fdc.read_port(0x3f4) == 0xd0, "FDC did not expose a result byte");
        require(fdc.read_port(0x3f5) == byte, "FDC returned the wrong result byte");
    }
    require(fdc.read_port(0x3f4) == 0x80, "FDC did not return to idle after results");
}

void start(PcatFloppy &fdc)
{
    require(fdc.read_port(0x3f4) == 0, "FDC did not start in reset");
    fdc.write_port(0x3f2, 0x1c);
    require(fdc.take_irq() && !fdc.take_irq(), "FDC reset did not raise one edge");
    for (std::uint8_t unit = 0; unit < 4; ++unit) {
        command(fdc, {0x08});
        expect_result(fdc, {std::uint8_t(0xc0 | unit), 0});
    }
    command(fdc, {0x08});
    expect_result(fdc, {0x80});
}

void program_dma(PcatFloppy &fdc, std::uint32_t address, std::uint16_t count)
{
    fdc.write_port(0x0a, 6); // Mask channel 2 while programming.
    fdc.write_port(0x0c, 0);
    fdc.write_port(4, std::uint8_t(address));
    fdc.write_port(4, std::uint8_t(address >> 8));
    fdc.write_port(5, std::uint8_t(count - 1));
    fdc.write_port(5, std::uint8_t((count - 1) >> 8));
    fdc.write_port(0x81, std::uint8_t(address >> 16));
    fdc.write_port(0x0b, 0x46); // Single-mode device-to-memory, channel 2.
    fdc.write_port(0x0a, 2);
}

void test_seek_and_read_id()
{
    std::vector<std::uint8_t> ram(640 * 1024);
    std::vector<std::uint8_t> media(PcatFloppy::image_size);
    PcatFloppy fdc(ram);
    fdc.set_media(media);
    require(!fdc.handles_port(0x80) && !fdc.handles_port(0x3f6), "FDC claims POST or hard-disk I/O");
    start(fdc);

    command(fdc, {0x0f, 0x00, 0x04});
    require(fdc.take_irq(), "FDC seek did not signal IRQ6");
    command(fdc, {0x08});
    expect_result(fdc, {0x20, 0x04});

    command(fdc, {0x4a, 0x04});
    require(fdc.take_irq(), "FDC read ID did not signal IRQ6");
    expect_result(fdc, {0x04, 0, 0, 4, 1, 1, 2});

    command(fdc, {0x07, 0x00});
    require(fdc.take_irq(), "FDC recalibrate did not signal IRQ6");
    command(fdc, {0x08});
    expect_result(fdc, {0x20, 0x00});
    command(fdc, {0x04, 0x00});
    expect_result(fdc, {0x78});
}

void test_dma()
{
    std::vector<std::uint8_t> ram(640 * 1024);
    std::vector<std::uint8_t> media(PcatFloppy::image_size);
    std::fill_n(media.begin(), 512, 0x2a);
    std::fill_n(media.begin() + 14 * 512, 512, 0x19);
    std::fill_n(media.begin() + 15 * 512, 512, 0x29);
    PcatFloppy fdc(ram);
    fdc.set_media(media);
    start(fdc);

    fdc.write_port(0xd8, 0); // Secondary controller byte flip-flop.
    fdc.write_port(0xc4, 0x55);
    fdc.write_port(0xc4, 0xaa);
    fdc.write_port(0xd8, 0);
    require(fdc.read_port(0xc4) == 0x55 && fdc.read_port(0xc4) == 0xaa,
            "AT secondary DMA register read-back was incorrect");

    program_dma(fdc, 0x7c00, 512);
    command(fdc, {0xe6, 0, 0, 0, 1, 2, 15, 0x1b, 0xff});
    require(fdc.take_irq(), "FDC rejected DMA without an IRQ6 edge");
    expect_result(fdc, {0x40, 0x10, 0, 0, 0, 1, 2});
    require(ram[0x7c00] == 0, "disabled AT DMA cascade still wrote memory");

    fdc.write_port(0xd6, 0xc0); // Cascade channel 4.
    fdc.write_port(0xd4, 0x00);
    command(fdc, {0xe6, 0, 0, 0, 1, 2, 15, 0x1b, 0xff});
    require(fdc.take_irq(), "FDC DMA completion did not signal IRQ6");
    expect_result(fdc, {0, 0, 0, 0, 0, 1, 2});
    require(std::equal(media.begin(), media.begin() + 512, ram.begin() + 0x7c00),
            "FDC did not DMA the first sector into boot memory");
    require(ram[0x7bff] == 0 && ram[0x7e00] == 0, "FDC DMA wrote beyond terminal count");
    require(fdc.read_port(8) == 4 && fdc.read_port(8) == 0, "DMA terminal-count status is wrong");
    require((fdc.read_port(0x0f) & 4) != 0, "DMA channel was left unmasked after terminal count");

    program_dma(fdc, 0x1ff00, 1024);
    command(fdc, {0xe6, 0, 0, 0, 15, 2, 15, 0x1b, 0xff});
    require(fdc.take_irq(), "multi-track read did not signal IRQ6");
    expect_result(fdc, {4, 0, 0, 0, 1, 1, 2});
    require(ram[0x1ff00] == 0x19 && ram[0x1ffff] == 0x19 &&
            ram[0x10000] == 0x19 && ram[0x100ff] == 0x19 &&
            ram[0x10100] == 0x29 && ram[0x102ff] == 0x29 && ram[0x20000] == 0,
            "DMA did not wrap the low 16 bits across its fixed page");
}

void test_non_dma_and_write_protection()
{
    std::vector<std::uint8_t> ram(640 * 1024);
    std::vector<std::uint8_t> media(PcatFloppy::image_size);
    std::fill_n(media.begin(), 512, 0x6d);
    PcatFloppy fdc(ram);
    fdc.set_media(media);
    start(fdc);
    command(fdc, {3, 0xdf, 0x03}); // Non-DMA execution.
    command(fdc, {0x46, 0, 0, 0, 1, 2, 1, 0x1b, 0xff});
    require(!fdc.take_irq(), "PIO FDC completed before transfer");
    for (unsigned index = 0; index < 512; ++index) {
        require(fdc.read_port(0x3f4) == 0xf0 && fdc.read_port(0x3f5) == 0x6d,
                "FDC PIO returned wrong execution data");
    }
    require(fdc.take_irq(), "FDC PIO completion did not signal IRQ6");
    expect_result(fdc, {0, 0, 0, 0, 0, 1, 2});

    command(fdc, {0x45, 0, 0, 0, 1, 2, 1, 0x1b, 0xff});
    require(fdc.take_irq(), "FDC write protection did not signal IRQ6");
    expect_result(fdc, {0x40, 2, 0, 0, 0, 1, 2});
    require(media[0] == 0x6d, "write-protected floppy changed");
}

} // namespace

int main()
{
    test_seek_and_read_id();
    test_dma();
    test_non_dma_and_write_protection();
}
