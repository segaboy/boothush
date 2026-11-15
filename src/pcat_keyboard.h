#ifndef BOOTHUSH_PCAT_KEYBOARD_H
#define BOOTHUSH_PCAT_KEYBOARD_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

// AT controller. The optional auxiliary model is opt-in; the 286 keeps its
// existing keyboard-only behavior. All input is explicitly injected.
class PcatKeyboard {
public:
    explicit PcatKeyboard(bool auxiliary_present = false) : auxiliary_present_(auxiliary_present) {}
    std::uint8_t read_port(std::uint16_t port);
    void write_port(std::uint16_t port, std::uint8_t value);
    bool peek_output(std::uint8_t &value) const;

    bool a20_enabled() const;
    bool take_reset_request();
    bool take_irq1();
    bool take_irq12();

    // Supplies one raw set-2 byte. False means no byte was accepted.
    bool inject_scancode(std::uint8_t scan_code);
    // Diagnostic snapshot of the shared controller output queue; no reads.
    std::size_t output_queued() const { return output_size_; }
    std::size_t output_capacity() const { return output_.size(); }
    // Relative screen coordinates: +X right, +Y down. One atomic PS/2 packet.
    bool inject_mouse(std::int32_t dx, std::int32_t dy, bool primary);
    std::uint64_t dropped_aux_replies() const { return dropped_aux_replies_; }

private:
    enum class ControllerWrite { none, configuration, output_port, output_buffer, auxiliary };
    enum class KeyboardWrite { none, leds, scan_set, typematic };

    std::array<std::uint8_t, 32> output_{};
    std::array<bool, 32> output_aux_{};
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
    bool irq12_pending_ = false;
    bool reset_requested_ = false;
    bool auxiliary_present_ = false;
    bool auxiliary_streaming_ = false;
    std::uint64_t dropped_aux_replies_ = 0;

    bool queue(std::initializer_list<std::uint8_t> bytes);
    bool queue_aux(std::initializer_list<std::uint8_t> bytes);
    bool queue_bytes(std::initializer_list<std::uint8_t> bytes, bool auxiliary);
    void auxiliary_command(std::uint8_t value);
    void signal_output();
    void controller_command(std::uint8_t value);
    void write_data(std::uint8_t value);
    void keyboard_command(std::uint8_t value);
    bool keyboard_reply(std::initializer_list<std::uint8_t> bytes);
};
#endif
