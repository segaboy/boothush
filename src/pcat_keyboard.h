#ifndef BOOTHUSH_PCAT_KEYBOARD_H
#define BOOTHUSH_PCAT_KEYBOARD_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

// An AT keyboard controller with a single emulated keyboard. All input comes
// from guest I/O or explicit scan-code injection.
class PcatKeyboard {
public:
    std::uint8_t read_port(std::uint16_t port);
    void write_port(std::uint16_t port, std::uint8_t value);
    bool peek_output(std::uint8_t &value) const;

    bool a20_enabled() const;
    bool take_reset_request();
    bool take_irq1();

    // Supplies one raw set-2 byte. False means no byte was accepted.
    bool inject_scancode(std::uint8_t scan_code);

private:
    enum class ControllerWrite { none, configuration, output_port, output_buffer };
    enum class KeyboardWrite { none, leds, scan_set, typematic };

    std::array<std::uint8_t, 32> output_{};
    std::size_t output_head_ = 0;
    std::size_t output_size_ = 0;
    std::uint8_t configuration_ = 0;
    std::uint8_t output_port_ = 3;
    std::uint8_t last_keyboard_byte_ = 0xfe;
    ControllerWrite controller_write_ = ControllerWrite::none;
    KeyboardWrite keyboard_write_ = KeyboardWrite::none;
    bool last_write_command_ = false;
    bool scanning_enabled_ = true;
    bool translated_break_ = false;
    bool irq1_pending_ = false;
    bool reset_requested_ = false;

    bool queue(std::initializer_list<std::uint8_t> bytes);
    void signal_output();
    void controller_command(std::uint8_t value);
    void write_data(std::uint8_t value);
    void keyboard_command(std::uint8_t value);
    bool keyboard_reply(std::initializer_list<std::uint8_t> bytes);
};
#endif
