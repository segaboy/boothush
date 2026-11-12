#ifndef BOOTHUSH_MAC_RTC_H
#define BOOTHUSH_MAC_RTC_H

#include <array>
#include <cstdint>

// The Macintosh 128K's 343-0040 real-time clock and parameter RAM.
class MacRtc {
public:
    static constexpr uint64_t cycles_per_second = 7'833'600;

    void set_port_b(uint8_t latch, uint8_t direction);
    bool data_high() const { return data_high_; }

    // Returns true if one or more emulated seconds elapsed since the last call.
    // The VIA latches this as a single pending CA2 interrupt.
    bool advance(uint64_t cycle);

private:
    enum class Phase { command, write, read, finished };

    void receive_bit(bool high);
    uint8_t read_register(uint8_t reg) const;
    void write_register(uint8_t reg, uint8_t value);

    std::array<uint8_t, 20> pram_{};
    uint32_t seconds_ = 0;
    uint64_t last_second_cycle_ = 0;
    Phase phase_ = Phase::command;
    uint8_t selected_register_ = 0;
    uint8_t shift_ = 0;
    unsigned bits_ = 0;
    bool selected_ = false;
    bool clock_high_ = true;
    bool data_high_ = true;
    bool write_protected_ = false;
};

#endif
