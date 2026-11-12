#include "ega.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr std::uint32_t graphics_base = 0xa0000;
constexpr std::uint32_t text_base = 0xb8000;
using Rgb = std::array<std::uint8_t, 3>;

void require(bool result, const char *message)
{
    if (!result) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void indexed_write(Ega &ega, std::uint16_t port, std::uint8_t index, std::uint8_t data)
{
    ega.write_port(port, index);
    ega.write_port(static_cast<std::uint16_t>(port + 1), data);
}

void graphics(Ega &ega, std::uint8_t index, std::uint8_t value)
{
    indexed_write(ega, 0x3ce, index, value);
}

Rgb pixel(const std::vector<std::uint8_t> &rgb, std::size_t x, std::size_t y)
{
    const auto offset = (y * Ega::width + x) * 3;
    return {rgb[offset], rgb[offset + 1], rgb[offset + 2]};
}

void test_graphics_pipeline()
{
    auto ega = std::make_unique<Ega>();
    std::vector<std::uint8_t> rgb(Ega::frame_size);

    require(ega->handles_memory(graphics_base), "graphics aperture unavailable");
    require(!ega->handles_memory(text_base), "unexpected text aperture in graphics mode");

    indexed_write(*ega, 0x3c4, 2, 1); // Plane 0 only.
    graphics(*ega, 3, 1);             // Rotate right once.
    ega->write_memory(graphics_base, 0x01);
    require(ega->read_memory(graphics_base) == 0x80, "write mode 0 did not rotate CPU data");
    graphics(*ega, 4, 1);
    require(ega->peek_memory(graphics_base) == 0, "sequencer map mask did not protect plane 1");

    graphics(*ega, 0, 2);             // Set plane 1.
    graphics(*ega, 1, 2);             // Use set/reset on plane 1.
    indexed_write(*ega, 0x3c4, 2, 2);
    graphics(*ega, 8, 0x40);          // Merge only pixel 1 with the latched byte.
    ega->write_memory(graphics_base, 0x00);
    require(ega->peek_memory(graphics_base) == 0x40, "set/reset and bit mask did not combine");

    graphics(*ega, 5, 2);             // CPU nibble selects all four plane bytes.
    graphics(*ega, 3, 0);
    graphics(*ega, 8, 0xff);
    indexed_write(*ega, 0x3c4, 2, 0x0f);
    ega->write_memory(graphics_base + 80, 6);
    ega->write_memory(graphics_base + 80 * 350 - 1, 0x0f);

    graphics(*ega, 5, 1);             // Copy latched pixels into another location.
    require(ega->read_memory(graphics_base) == 0x40, "read mode 0 returned the wrong plane");
    require(ega->peek_memory(graphics_base + 1) == 0, "peek did not read the requested address");
    ega->frame_rgb(rgb.data());        // Capture must not replace the CPU latches.
    ega->write_memory(graphics_base + 1, 0);
    ega->frame_rgb(rgb.data());

    require(pixel(rgb, 0, 0) == Rgb{0, 0, 170}, "wrong plane 0 RGB value");
    require(pixel(rgb, 1, 0) == Rgb{0, 170, 0}, "wrong plane 1 RGB value");
    require(pixel(rgb, 8, 0) == pixel(rgb, 0, 0), "read/capture unexpectedly replaced the latch");
    require(pixel(rgb, 9, 0) == pixel(rgb, 1, 0), "write mode 1 did not copy all latched planes");
    require(pixel(rgb, 0, 1) == Rgb{170, 85, 0}, "EGA brown or scanline pitch is wrong");
    require(pixel(rgb, 639, 349) == Rgb{255, 255, 255}, "last pixel was not rendered");

    graphics(*ega, 5, 8);             // Color-compare read mode.
    graphics(*ega, 2, 1);
    require(ega->read_memory(graphics_base) == 0x80, "color compare did not match pixel 0");
    graphics(*ega, 7, 2);             // Ignore planes other than 1.
    require(ega->read_memory(graphics_base) == 0xbf, "color compare did not honor don't-care planes");

    graphics(*ega, 5, 2);
    graphics(*ega, 3, 0x18);          // XOR mode 2 data with the latches.
    graphics(*ega, 8, 0x80);
    ega->write_memory(graphics_base, 1);
    graphics(*ega, 5, 0);
    graphics(*ega, 4, 0);
    require(ega->read_memory(graphics_base) == 0, "raster XOR did not clear the plane 0 pixel");

    graphics(*ega, 5, 3);
    ega->write_memory(graphics_base, 0xff);
    graphics(*ega, 5, 0);
    require(ega->peek_memory(graphics_base) == 0, "undefined EGA write mode changed VRAM");
}

void test_display_registers_and_text_aperture()
{
    auto ega = std::make_unique<Ega>();
    std::vector<std::uint8_t> rgb(Ega::frame_size);

    graphics(*ega, 5, 2);
    ega->write_memory(graphics_base + 1, 1);
    indexed_write(*ega, 0x3d4, 0x0d, 1);

    ega->write_port(0x3c0, 0x05);     // Leave controller expecting data.
    require((ega->read_port(0x3da) & 0x09) == 1, "display phase should start active");
    ega->write_port(0x3c0, 1);        // Status read restored address selection.
    ega->write_port(0x3c0, 0x24);     // Assign palette entry 1 to bright red.
    ega->frame_rgb(rgb.data());
    require(pixel(rgb, 0, 0) == Rgb{0, 0, 0}, "palette access did not blank video");
    ega->write_port(0x3c0, 0x20);
    ega->frame_rgb(rgb.data());
    require(pixel(rgb, 0, 0) == Rgb{255, 0, 0}, "palette programming or CRTC start address is wrong");

    ega->set_cycles(96100);
    require((ega->read_port(0x3da) & 0x09) == 8, "vertical retrace was not observable");
    ega->set_cycles(100000);
    require((ega->read_port(0x3da) & 0x09) == 1, "display phase did not wrap");

    ega->write_port(0x3c2, 0x01);
    require(ega->peek_memory(graphics_base + 1) == 0xff, "video RAM enable was ignored");

    require(ega->set_bios_mode(3), "mode 3 setup failed");
    require(ega->handles_memory(text_base), "text aperture unavailable");
    require(ega->peek_memory(text_base) == 0x20 && ega->peek_memory(text_base + 1) == 0x07,
            "mode 3 did not initialize blank text cells");
    ega->write_memory(text_base + 2, 'A');
    ega->write_memory(text_base + 3, 0x1f);
    require(ega->peek_memory(text_base + 2) == 'A' && ega->peek_memory(text_base + 3) == 0x1f,
            "text byte addressing did not preserve character and attribute planes");
    require(ega->set_bios_mode(0x83), "mode 3 no-clear setup failed");
    require(ega->peek_memory(text_base + 2) == 'A', "no-clear mode change lost text");
    require(!ega->set_bios_mode(0x13), "accepted VGA mode 13h on EGA");
    ega->frame_rgb(rgb.data());
    require(std::all_of(rgb.begin(), rgb.end(), [](auto value) { return value == 0; }),
            "text planes were misinterpreted as graphics");
}

} // namespace

int main()
{
    test_graphics_pipeline();
    test_display_registers_and_text_aperture();
}
