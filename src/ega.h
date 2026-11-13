#ifndef BOOTHUSH_EGA_H
#define BOOTHUSH_EGA_H

#include <array>
#include <cstddef>
#include <cstdint>

class Ega {
public:
    static constexpr std::size_t width = 640;
    static constexpr std::size_t height = 350;
    static constexpr std::size_t frame_size = width * height * 3;

    Ega();

    bool set_bios_mode(std::uint8_t mode);
    void set_text_font(const std::uint8_t font[256 * 14]);
    void set_cycles(std::uint64_t cycles);

    bool handles_port(std::uint16_t port) const;
    std::uint8_t read_port(std::uint16_t port);
    void write_port(std::uint16_t port, std::uint8_t value);

    bool handles_memory(std::uint32_t address) const;
    std::uint8_t read_memory(std::uint32_t address);
    std::uint8_t peek_memory(std::uint32_t address) const;
    void write_memory(std::uint32_t address, std::uint8_t value);

    // Writes frame_size bytes in top-to-bottom RGB order without updating the adapter.
    void frame_rgb(std::uint8_t *destination) const;

private:
    static constexpr std::size_t plane_size = 65536;
    using PlaneBytes = std::array<std::uint8_t, 4>;

    std::array<std::array<std::uint8_t, plane_size>, 4> planes_{};
    PlaneBytes latch_{};
    std::array<std::uint8_t, 5> sequencer_{};
    std::array<std::uint8_t, 9> graphics_{};
    std::array<std::uint8_t, 0x19> crtc_{};
    std::array<std::uint8_t, 0x14> attribute_{};
    std::array<std::uint8_t, 256 * 14> text_font_{};
    bool has_text_font_ = false;
    std::uint8_t misc_output_ = 0;
    std::uint8_t sequencer_index_ = 0;
    std::uint8_t graphics_index_ = 0;
    std::uint8_t crtc_index_ = 0;
    std::uint8_t attribute_index_ = 0;
    bool attribute_data_ = false;
    bool palette_enabled_ = false;
    std::uint32_t frame_phase_ = 0;

    std::uint32_t aperture_base() const;
    std::uint16_t memory_offset(std::uint32_t address, bool odd_even) const;
    std::uint16_t crtc_base() const;
    PlaneBytes load_planes(std::uint16_t offset) const;
    std::uint8_t selected_read(std::uint32_t address, const PlaneBytes &bytes) const;
};

#endif
