#include "ega.h"

#include <algorithm>
#include <bit>

namespace {

constexpr std::uint16_t attribute_port = 0x3c0;
constexpr std::uint16_t misc_port = 0x3c2;
constexpr std::uint16_t sequencer_index_port = 0x3c4;
constexpr std::uint16_t sequencer_data_port = 0x3c5;
constexpr std::uint16_t graphics_index_port = 0x3ce;
constexpr std::uint16_t graphics_data_port = 0x3cf;
constexpr std::uint16_t mono_crtc_base = 0x3b4;
constexpr std::uint16_t color_crtc_base = 0x3d4;

constexpr std::size_t seq_reset = 0;
constexpr std::size_t seq_map_mask = 2;
constexpr std::size_t seq_memory_mode = 4;
constexpr std::size_t gc_set_reset = 0;
constexpr std::size_t gc_enable_set_reset = 1;
constexpr std::size_t gc_color_compare = 2;
constexpr std::size_t gc_rotate = 3;
constexpr std::size_t gc_read_map = 4;
constexpr std::size_t gc_mode = 5;
constexpr std::size_t gc_misc = 6;
constexpr std::size_t gc_color_care = 7;
constexpr std::size_t gc_bit_mask = 8;
constexpr std::size_t attr_mode = 0x10;
constexpr std::size_t attr_planes = 0x12;
constexpr std::size_t crtc_start_high = 0x0c;
constexpr std::size_t crtc_start_low = 0x0d;
constexpr std::size_t crtc_offset = 0x13;
constexpr std::size_t crtc_mode = 0x17;

constexpr std::uint32_t aperture_bases[] = {0xa0000, 0xa0000, 0xb0000, 0xb8000};
constexpr std::uint32_t aperture_sizes[] = {0x20000, 0x10000, 0x8000, 0x8000};
constexpr std::uint32_t cycles_per_frame = 100000; // 6 MHz CPU, nominal 60 Hz.
constexpr std::uint32_t scanlines_per_frame = 366;
constexpr std::uint8_t enhanced_color_switches = 0x09;

constexpr std::array<std::uint8_t, 16> default_palette = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};

std::uint8_t expanded_bit(std::uint8_t value, unsigned bit)
{
    return (value & (1u << bit)) != 0 ? 0xff : 0;
}

std::uint8_t raster_op(std::uint8_t value, std::uint8_t latch, unsigned operation)
{
    switch (operation) {
    case 1: return value & latch;
    case 2: return value | latch;
    case 3: return value ^ latch;
    default: return value;
    }
}

std::uint8_t channel(std::uint8_t color, unsigned shift)
{
    const unsigned primary = (color >> shift) & 1u;
    const unsigned secondary = (color >> (shift + 3)) & 1u;
    return static_cast<std::uint8_t>(170 * primary + 85 * secondary);
}

} // namespace

Ega::Ega()
{
    set_bios_mode(0x90);
}

bool Ega::set_bios_mode(std::uint8_t mode)
{
    const auto selected = mode & 0x7f;
    if (selected != 3 && selected != 0x10) {
        return false;
    }

    const bool graphics_mode = selected == 0x10;
    misc_output_ = graphics_mode ? 0xa7 : 0x07;
    sequencer_ = {0x03, 0x01, graphics_mode ? std::uint8_t{0x0f} : std::uint8_t{0x03}, 0x00,
                  graphics_mode ? std::uint8_t{0x06} : std::uint8_t{0x03}};
    graphics_.fill(0);
    graphics_[gc_mode] = graphics_mode ? 0 : 0x10;
    graphics_[gc_misc] = graphics_mode ? 0x05 : 0x0e;
    graphics_[gc_color_care] = 0x0f;
    graphics_[gc_bit_mask] = 0xff;
    crtc_.fill(0);
    crtc_[crtc_offset] = 40;
    crtc_[crtc_mode] = graphics_mode ? 0xe3 : 0xa3;
    attribute_.fill(0);
    std::copy(default_palette.begin(), default_palette.end(), attribute_.begin());
    attribute_[attr_mode] = graphics_mode ? 1 : 0;
    attribute_[attr_planes] = 0x0f;
    sequencer_index_ = graphics_index_ = crtc_index_ = attribute_index_ = 0;
    attribute_data_ = false;
    palette_enabled_ = true;
    latch_.fill(0);

    if ((mode & 0x80) == 0) {
        for (auto &plane : planes_) {
            plane.fill(0);
        }
        if (!graphics_mode) {
            std::fill_n(planes_[0].begin(), 80 * 25, 0x20);
            std::fill_n(planes_[1].begin(), 80 * 25, 0x07);
        }
    }
    return true;
}

void Ega::set_text_font(const std::uint8_t font[256 * 14])
{
    std::copy_n(font, text_font_.size(), text_font_.begin());
    has_text_font_ = true;
}

void Ega::set_cycles(std::uint64_t cycles)
{
    frame_phase_ = static_cast<std::uint32_t>(cycles % cycles_per_frame);
}

std::uint16_t Ega::crtc_base() const
{
    return (misc_output_ & 1) != 0 ? color_crtc_base : mono_crtc_base;
}

bool Ega::handles_port(std::uint16_t port) const
{
    return (port >= 0x3c0 && port <= 0x3cf) ||
           (port >= mono_crtc_base && port <= mono_crtc_base + 1) || port == 0x3ba ||
           (port >= color_crtc_base && port <= color_crtc_base + 1) || port == 0x3da;
}

std::uint8_t Ega::read_port(std::uint16_t port)
{
    if (port == misc_port) {
        const unsigned switch_index = (misc_output_ >> 2) & 3u;
        return (enhanced_color_switches & (1u << switch_index)) != 0 ? 0x10 : 0;
    }
    if (port == crtc_base() + 6) {
        attribute_data_ = false;
        const std::uint32_t scan = frame_phase_ * scanlines_per_frame;
        const std::uint32_t line = scan / cycles_per_frame;
        const bool visible = line < height && scan % cycles_per_frame < 85000;
        const bool vertical_retrace = line >= 350 && line < 354;
        return static_cast<std::uint8_t>(0x04 | (visible ? 0x01 : 0) |
                                         (vertical_retrace ? 0x08 : 0));
    }
    // EGA indexed registers are write-only.
    return 0xff;
}

void Ega::write_port(std::uint16_t port, std::uint8_t value)
{
    switch (port) {
    case attribute_port:
        if (!attribute_data_) {
            attribute_index_ = value & 0x1f;
            palette_enabled_ = (value & 0x20) != 0;
        } else if (attribute_index_ < attribute_.size() &&
                   (attribute_index_ >= default_palette.size() || !palette_enabled_)) {
            attribute_[attribute_index_] = value & 0x3f;
        }
        attribute_data_ = !attribute_data_;
        break;
    case misc_port:
        misc_output_ = value;
        break;
    case sequencer_index_port:
        sequencer_index_ = value & 0x0f;
        break;
    case sequencer_data_port:
        if (sequencer_index_ < sequencer_.size()) {
            sequencer_[sequencer_index_] = value;
        }
        break;
    case graphics_index_port:
        graphics_index_ = value & 0x0f;
        break;
    case graphics_data_port:
        if (graphics_index_ < graphics_.size()) {
            graphics_[graphics_index_] = value;
        }
        break;
    default:
        if (port == crtc_base()) {
            crtc_index_ = value & 0x1f;
        } else if (port == crtc_base() + 1 && crtc_index_ < crtc_.size()) {
            crtc_[crtc_index_] = value;
        }
        break;
    }
}

std::uint32_t Ega::aperture_base() const
{
    return aperture_bases[(graphics_[gc_misc] >> 2) & 3];
}

bool Ega::handles_memory(std::uint32_t address) const
{
    const auto map = (graphics_[gc_misc] >> 2) & 3;
    return (misc_output_ & 2) != 0 && address >= aperture_bases[map] &&
           address - aperture_bases[map] < aperture_sizes[map];
}

std::uint16_t Ega::memory_offset(std::uint32_t address, bool odd_even) const
{
    std::uint32_t offset = address - aperture_base();
    if (odd_even && (graphics_[gc_misc] & 2) != 0) {
        offset = (offset >> 1) | ((misc_output_ & 0x20) != 0 ? 0x8000 : 0);
    }
    const std::uint32_t mask = (sequencer_[seq_memory_mode] & 2) != 0 ? 0xffff : 0x3fff;
    return static_cast<std::uint16_t>(offset & mask);
}

Ega::PlaneBytes Ega::load_planes(std::uint16_t offset) const
{
    return {planes_[0][offset], planes_[1][offset], planes_[2][offset], planes_[3][offset]};
}

std::uint8_t Ega::selected_read(std::uint32_t address, const PlaneBytes &bytes) const
{
    if ((graphics_[gc_mode] & 0x08) == 0) {
        unsigned plane = graphics_[gc_read_map] & 3;
        if ((graphics_[gc_mode] & 0x10) != 0) {
            plane = (plane & 2) | (address & 1);
        }
        return bytes[plane];
    }

    std::uint8_t matches = 0xff;
    for (unsigned plane = 0; plane < bytes.size(); ++plane) {
        if ((graphics_[gc_color_care] & (1u << plane)) != 0) {
            matches &= static_cast<std::uint8_t>(~(bytes[plane] ^ expanded_bit(graphics_[gc_color_compare], plane)));
        }
    }
    return matches;
}

std::uint8_t Ega::read_memory(std::uint32_t address)
{
    if (!handles_memory(address)) {
        return 0xff;
    }
    const bool odd_even = (graphics_[gc_mode] & 0x10) != 0;
    latch_ = load_planes(memory_offset(address, odd_even));
    return selected_read(address, latch_);
}

std::uint8_t Ega::peek_memory(std::uint32_t address) const
{
    if (!handles_memory(address)) {
        return 0xff;
    }
    const bool odd_even = (graphics_[gc_mode] & 0x10) != 0;
    return selected_read(address, load_planes(memory_offset(address, odd_even)));
}

void Ega::write_memory(std::uint32_t address, std::uint8_t value)
{
    const unsigned mode = graphics_[gc_mode] & 3;
    if (mode == 3 || !handles_memory(address)) {
        return;
    }
    const bool odd_even = (sequencer_[seq_memory_mode] & 4) == 0;
    const auto offset = memory_offset(address, odd_even);
    unsigned map_mask = sequencer_[seq_map_mask];
    if (odd_even) {
        map_mask &= 0x5u << (address & 1u);
    }
    const auto rotated = std::rotr(value, graphics_[gc_rotate] & 7);
    const unsigned operation = (graphics_[gc_rotate] >> 3) & 3;
    const auto bit_mask = graphics_[gc_bit_mask];

    for (unsigned plane = 0; plane < planes_.size(); ++plane) {
        if ((map_mask & (1u << plane)) == 0) {
            continue;
        }
        if (mode == 1) {
            planes_[plane][offset] = latch_[plane];
            continue;
        }

        std::uint8_t data = rotated;
        if (mode == 2) {
            data = expanded_bit(value, plane);
        } else if ((graphics_[gc_enable_set_reset] & (1u << plane)) != 0) {
            data = expanded_bit(graphics_[gc_set_reset], plane);
        }
        data = raster_op(data, latch_[plane], operation);
        planes_[plane][offset] = static_cast<std::uint8_t>((data & bit_mask) | (latch_[plane] & ~bit_mask));
    }
}

void Ega::frame_rgb(std::uint8_t *destination) const
{
    if (has_text_font_ && palette_enabled_ && (attribute_[attr_mode] & 1) == 0 &&
        (sequencer_[seq_reset] & 3) == 3) {
        const unsigned start = (unsigned(crtc_[0x0c]) << 8) | crtc_[0x0d];
        const unsigned cursor = (unsigned(crtc_[0x0e]) << 8) | crtc_[0x0f];
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned column = 0; column < 80; ++column) {
                const auto offset = static_cast<std::uint16_t>(start + (y / 14) * 80 + column);
                const std::uint8_t ch = planes_[0][offset];
                const std::uint8_t attr = planes_[1][offset];
                std::uint8_t bits = text_font_[ch * 14 + y % 14];
                if ((crtc_[0x0a] & 0x20) == 0 && offset == cursor &&
                    y % 14 >= (crtc_[0x0a] & 0x1f) && y % 14 <= (crtc_[0x0b] & 0x1f)) bits = 0xff;
                for (unsigned bit = 0x80; bit; bit >>= 1) {
                    const unsigned index = (bits & bit) ? (attr & 15) : ((attr >> 4) & 7);
                    const auto color = attribute_[index & attribute_[attr_planes]];
                    *destination++ = channel(color, 2); *destination++ = channel(color, 1); *destination++ = channel(color, 0);
                }
            }
        }
        return;
    }
    if (!palette_enabled_ || (attribute_[attr_mode] & graphics_[gc_misc] & 1) == 0 ||
        (misc_output_ & 0x10) != 0 || (sequencer_[seq_reset] & 3) != 3) {
        std::fill_n(destination, frame_size, 0);
        return;
    }

    std::array<std::array<std::uint8_t, 3>, 16> palette{};
    for (std::size_t index = 0; index < palette.size(); ++index) {
        const auto color = attribute_[index & attribute_[attr_planes]];
        palette[index] = {channel(color, 2), channel(color, 1), channel(color, 0)};
    }

    const unsigned address_scale = (crtc_[crtc_mode] & 0x40) != 0 ? 1 : 2;
    const unsigned start = (static_cast<unsigned>(crtc_[crtc_start_high]) << 8) | crtc_[crtc_start_low];
    const unsigned pitch = crtc_[crtc_offset] * 2u * address_scale;
    unsigned row = start * address_scale;
    for (std::size_t y = 0; y < height; ++y, row += pitch) {
        for (std::size_t column = 0; column < width / 8; ++column) {
            const auto bytes = load_planes(static_cast<std::uint16_t>(row + column));
            for (unsigned bit = 0x80; bit != 0; bit >>= 1) {
                unsigned color = 0;
                for (unsigned plane = 0; plane < bytes.size(); ++plane) {
                    if ((bytes[plane] & bit) != 0) {
                        color |= 1u << plane;
                    }
                }
                destination = std::copy(palette[color].begin(), palette[color].end(), destination);
            }
        }
    }
}
