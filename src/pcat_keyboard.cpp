#include "pcat_keyboard.h"

#include <utility>

namespace {

constexpr std::uint8_t keyboard_irq = 0x01;
constexpr std::uint8_t auxiliary_irq = 0x02;
constexpr std::uint8_t system_flag = 0x04;
constexpr std::uint8_t keyboard_disabled = 0x10;
constexpr std::uint8_t auxiliary_disabled = 0x20;
constexpr std::uint8_t translate_set2 = 0x40;
constexpr std::uint8_t output_available = 0x01;
constexpr std::uint8_t command_write = 0x08;
constexpr std::uint8_t keyboard_unlocked = 0x10;
constexpr std::uint8_t ack = 0xfa;
constexpr std::uint8_t resend = 0xfe;

// Scan-set mapping for the AT and enhanced keyboards. Zero entries are
// unused positions. E0 and E1 remain in the byte stream.
constexpr std::array<std::uint8_t, 128> set1_by_set2 = {
    0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58, 0x00, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x00,
    0x65, 0x38, 0x2a, 0x00, 0x1d, 0x10, 0x02, 0x00, 0x66, 0x00, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
    0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c, 0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e, 0x6a, 0x00, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
    0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x00, 0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x00,
    0x6d, 0x00, 0x28, 0x00, 0x1a, 0x0d, 0x00, 0x00, 0x3a, 0x36, 0x1c, 0x1b, 0x00, 0x2b, 0x63, 0x00,
    0x00, 0x56, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x4f, 0x00, 0x4b, 0x47, 0x00, 0x00, 0x00,
    0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45, 0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
};

std::uint8_t translate_byte(std::uint8_t scan_code)
{
    if (scan_code < set1_by_set2.size()) {
        return set1_by_set2[scan_code];
    }
    switch (scan_code) {
    case 0x83:
        return 0x41; // F7.
    case 0x84:
        return 0x54; // SysRq.
    case 0xff:
        return 0xff; // Keyboard overrun.
    default:
        return 0;
    }
}

} // namespace

bool PcatKeyboard::peek_output(std::uint8_t &value) const
{
    if (output_size_ == 0) {
        return false;
    }
    value = output_[output_head_];
    return true;
}

std::uint8_t PcatKeyboard::read_port(std::uint16_t port)
{
    if (port == 0x64) {
        // Input operations complete synchronously. The input-buffer-full bit
        // is clear and there is no auxiliary device or transport error.
        std::uint8_t status = keyboard_unlocked | (configuration_ & system_flag);
        if (last_write_command_) {
            status |= command_write;
        }
        if (output_size_ != 0) {
            status |= output_available;
            if (output_aux_[output_head_]) status |= 0x20;
        }
        return status;
    }
    if (port != 0x60 || output_size_ == 0) {
        return 0xff;
    }

    const std::uint8_t value = output_[output_head_];
    output_head_ = (output_head_ + 1) % output_.size();
    --output_size_;
    // Presenting the next byte refills the output latch and raises a new edge.
    signal_output();
    return value;
}

void PcatKeyboard::write_port(std::uint16_t port, std::uint8_t value)
{
    if (port == 0x64) {
        last_write_command_ = true;
        controller_command(value);
    } else if (port == 0x60) {
        last_write_command_ = false;
        write_data(value);
    }
}

bool PcatKeyboard::a20_enabled() const
{
    return (output_port_ & 2) != 0;
}

bool PcatKeyboard::take_reset_request()
{
    return std::exchange(reset_requested_, false);
}

bool PcatKeyboard::take_irq1()
{
    return std::exchange(irq1_pending_, false);
}

bool PcatKeyboard::take_irq12()
{
    return std::exchange(irq12_pending_, false);
}

bool PcatKeyboard::queue(std::initializer_list<std::uint8_t> bytes)
{
    return queue_bytes(bytes, false);
}

bool PcatKeyboard::queue_aux(std::initializer_list<std::uint8_t> bytes)
{
    return queue_bytes(bytes, true);
}

bool PcatKeyboard::queue_bytes(std::initializer_list<std::uint8_t> bytes, bool auxiliary)
{
    if (bytes.size() > output_.size() - output_size_) {
        return false;
    }
    const bool previously_empty = output_size_ == 0;
    for (const auto byte : bytes) {
        const auto index = (output_head_ + output_size_) % output_.size();
        output_[index] = byte;
        output_aux_[index] = auxiliary;
        ++output_size_;
    }
    if (previously_empty) {
        signal_output();
    }
    return true;
}

void PcatKeyboard::signal_output()
{
    if (!output_size_) return;
    if (output_aux_[output_head_]) {
        if (configuration_ & auxiliary_irq) irq12_pending_ = true;
    } else if (configuration_ & keyboard_irq) irq1_pending_ = true;
}

void PcatKeyboard::controller_command(std::uint8_t value)
{
    controller_write_ = ControllerWrite::none;
    switch (value) {
    case 0x20:
        queue({configuration_});
        break;
    case 0x60:
        controller_write_ = ControllerWrite::configuration;
        break;
    case 0xa7:
        configuration_ |= auxiliary_disabled;
        break;
    case 0xa8:
        configuration_ &= ~auxiliary_disabled;
        break;
    case 0xa9:
        queue({std::uint8_t(auxiliary_present_ ? 0 : 1)});
        break;
    case 0xaa:
        configuration_ |= system_flag;
        queue({0x55});
        break;
    case 0xab:
        queue({0});
        break;
    case 0xad:
        configuration_ |= keyboard_disabled;
        break;
    case 0xae:
        configuration_ &= ~keyboard_disabled;
        break;
    case 0xc0:
        queue({0xbf}); // Color-display strap and unlocked keyboard.
        break;
    case 0xd0:
        queue({std::uint8_t((output_port_ & ~0x10) | (output_size_ != 0 ? 0x10 : 0))});
        break;
    case 0xd1:
        controller_write_ = ControllerWrite::output_port;
        break;
    case 0xd2:
        controller_write_ = ControllerWrite::output_buffer;
        break;
    case 0xd4:
        if (auxiliary_present_) controller_write_ = ControllerWrite::auxiliary;
        break;
    default:
        // F0..FF briefly pulse the selected output lines low. Line zero
        // is CPU RESET. The latched A20 level stays unchanged after a pulse.
        if (value >= 0xf0 && (value & 1) == 0) {
            reset_requested_ = true;
        }
        break;
    }
}

void PcatKeyboard::write_data(std::uint8_t value)
{
    switch (std::exchange(controller_write_, ControllerWrite::none)) {
    case ControllerWrite::configuration: {
        const std::uint8_t previous = configuration_;
        configuration_ = value & 0x7f;
        if ((previous ^ configuration_) & translate_set2) {
            translated_break_ = false;
        }
        if ((configuration_ & (keyboard_irq | auxiliary_irq)) & ~previous) {
            signal_output();
        }
        break;
    }
    case ControllerWrite::output_port:
        reset_requested_ = reset_requested_ || (value & 1) == 0;
        // The reset request is delivered as a pulse at an instruction boundary.
        output_port_ = value | 1;
        break;
    case ControllerWrite::output_buffer:
        queue({value});
        break;
    case ControllerWrite::auxiliary:
        auxiliary_command(value);
        break;
    case ControllerWrite::none:
        keyboard_command(value);
        break;
    }
}

void PcatKeyboard::auxiliary_command(std::uint8_t value)
{
    if (!auxiliary_present_ || (configuration_ & auxiliary_disabled)) return;
    bool queued = false;
    switch (value) {
    case 0xff:
        queued = queue_aux({ack, 0xaa, 0x00});
        if (queued) auxiliary_streaming_ = false;
        break;
    case 0xf5:
    case 0xf6:
        queued = queue_aux({ack});
        if (queued) auxiliary_streaming_ = false;
        break;
    case 0xf4:
        queued = queue_aux({ack});
        if (queued) auxiliary_streaming_ = true;
        break;
    default:
        queued = queue_aux({resend});
        break;
    }
    if (!queued) ++dropped_aux_replies_;
}

bool PcatKeyboard::inject_mouse(std::int32_t dx, std::int32_t dy, bool primary)
{
    if (!auxiliary_present_ || !auxiliary_streaming_ || (configuration_ & auxiliary_disabled) ||
        dx < -255 || dx > 255 || dy < -255 || dy > 255) return false;
    const std::int32_t device_y = -dy;
    const auto flags = std::uint8_t(0x08 | (primary ? 1 : 0) |
                                    (dx < 0 ? 0x10 : 0) | (device_y < 0 ? 0x20 : 0));
    return queue_aux({flags, std::uint8_t(dx & 0xff), std::uint8_t(device_y & 0xff)});
}

bool PcatKeyboard::keyboard_reply(std::initializer_list<std::uint8_t> bytes)
{
    if (!queue(bytes)) {
        return false;
    }
    last_keyboard_byte_ = *(bytes.end() - 1);
    return true;
}

void PcatKeyboard::keyboard_command(std::uint8_t value)
{
    // Reset can recover a device left waiting for a command argument.
    if (value == 0xff) {
        scanning_enabled_ = true;
        translated_break_ = false;
        keyboard_write_ = KeyboardWrite::none;
        keyboard_reply({ack, 0xaa});
        return;
    }

    switch (keyboard_write_) {
    case KeyboardWrite::leds:
    case KeyboardWrite::typematic:
        keyboard_write_ = KeyboardWrite::none;
        keyboard_reply({ack});
        return;
    case KeyboardWrite::scan_set:
        if (value == 0) {
            keyboard_write_ = KeyboardWrite::none;
            const std::uint8_t active_set = (configuration_ & translate_set2) ? 0x41 : 2;
            keyboard_reply({ack, active_set});
        } else if (value == 2) {
            keyboard_write_ = KeyboardWrite::none;
            keyboard_reply({ack});
        } else {
            keyboard_reply({resend});
        }
        return;
    case KeyboardWrite::none:
        break;
    }

    switch (value) {
    case 0xed:
        keyboard_write_ = KeyboardWrite::leds;
        keyboard_reply({ack});
        break;
    case 0xee:
        keyboard_reply({0xee});
        break;
    case 0xf0:
        keyboard_write_ = KeyboardWrite::scan_set;
        keyboard_reply({ack});
        break;
    case 0xf2: {
        const std::uint8_t model = (configuration_ & translate_set2) ? 0x41 : 0x83;
        keyboard_reply({ack, 0xab, model});
        break;
    }
    case 0xf3:
        keyboard_write_ = KeyboardWrite::typematic;
        keyboard_reply({ack});
        break;
    case 0xf4:
    case 0xf5:
    case 0xf6:
        scanning_enabled_ = value != 0xf5;
        translated_break_ = false;
        keyboard_reply({ack});
        break;
    case 0xfe:
        keyboard_reply({last_keyboard_byte_});
        break;
    default:
        keyboard_reply({resend});
        break;
    }
}

bool PcatKeyboard::inject_scancode(std::uint8_t scan_code)
{
    if (!scanning_enabled_ || (configuration_ & keyboard_disabled) ||
        output_size_ == output_.size()) {
        return false;
    }

    if ((configuration_ & translate_set2) && scan_code != 0xe0 && scan_code != 0xe1) {
        if (scan_code == 0xf0) {
            translated_break_ = true;
            return true;
        }

        const std::uint8_t translated = translate_byte(scan_code);
        if (translated == 0) {
            return false;
        }
        scan_code = std::uint8_t(translated | (translated_break_ ? 0x80 : 0));
        translated_break_ = false;
    }
    return keyboard_reply({scan_code});
}
