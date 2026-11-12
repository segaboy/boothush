#include "mac_rtc.h"

namespace {

constexpr uint8_t serial_data = 0x01;
constexpr uint8_t serial_clock = 0x02;
constexpr uint8_t serial_disable = 0x04;
constexpr uint8_t write_protect_register = 13;

int pram_address(uint8_t reg)
{
    if (reg >= 16) {
        return reg - 16;
    }
    if (reg >= 8 && reg <= 11) {
        return reg + 8;
    }
    return -1;
}

} // namespace

void MacRtc::set_port_b(uint8_t latch, uint8_t direction)
{
    const uint8_t pins = static_cast<uint8_t>(latch | ~direction);
    const bool selected = (pins & serial_disable) == 0;
    const bool clock_high = (pins & serial_clock) != 0;

    if (!selected) {
        phase_ = Phase::command;
        shift_ = 0;
        bits_ = 0;
        data_high_ = true;
    } else if (selected_) {
        if (phase_ == Phase::read && clock_high_ && !clock_high) {
            data_high_ = (shift_ & 0x80) != 0;
            shift_ = static_cast<uint8_t>(shift_ << 1);
            if (++bits_ == 8) {
                phase_ = Phase::finished;
            }
        } else if ((phase_ == Phase::command || phase_ == Phase::write) &&
                   !clock_high_ && clock_high && (direction & serial_data) != 0) {
            receive_bit((pins & serial_data) != 0);
        } else if (phase_ == Phase::finished && !clock_high_ && clock_high) {
            data_high_ = true;
        }
    }
    selected_ = selected;
    clock_high_ = clock_high;
}

bool MacRtc::advance(uint64_t cycle)
{
    if (cycle < last_second_cycle_ || cycle - last_second_cycle_ < cycles_per_second) {
        return false;
    }
    const uint64_t elapsed = (cycle - last_second_cycle_) / cycles_per_second;
    last_second_cycle_ += elapsed * cycles_per_second;
    seconds_ += static_cast<uint32_t>(elapsed);
    return true;
}

void MacRtc::receive_bit(bool high)
{
    shift_ = static_cast<uint8_t>((shift_ << 1) | high);
    if (++bits_ != 8) {
        return;
    }

    const uint8_t byte = shift_;
    shift_ = 0;
    bits_ = 0;
    if (phase_ == Phase::write) {
        write_register(selected_register_, byte);
        phase_ = Phase::finished;
    } else if ((byte & 0x03) != 0x01) {
        phase_ = Phase::finished;
    } else {
        selected_register_ = (byte >> 2) & 0x1f;
        if ((byte & 0x80) != 0) {
            shift_ = read_register(selected_register_);
            phase_ = Phase::read;
        } else {
            phase_ = Phase::write;
        }
    }
}

uint8_t MacRtc::read_register(uint8_t reg) const
{
    // The four time bytes are selected by bits 3–2. Bit 4 is ignored.
    if (reg < 8) {
        return static_cast<uint8_t>(seconds_ >> ((reg & 3) * 8));
    }
    const int address = pram_address(reg);
    return address >= 0 ? pram_[address] : 0xff;
}

void MacRtc::write_register(uint8_t reg, uint8_t value)
{
    if (reg == write_protect_register) {
        write_protected_ = (value & 0x80) != 0;
        return;
    }
    if (write_protected_) {
        return;
    }
    if (reg < 8) {
        const unsigned shift = (reg & 3) * 8;
        seconds_ = (seconds_ & ~(UINT32_C(0xff) << shift)) | (uint32_t{value} << shift);
        return;
    }
    const int address = pram_address(reg);
    if (address >= 0) {
        pram_[address] = value;
    }
}
