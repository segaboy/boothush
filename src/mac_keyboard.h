#ifndef BOOTHUSH_MAC_KEYBOARD_H
#define BOOTHUSH_MAC_KEYBOARD_H

#include <array>
#include <cstddef>
#include <cstdint>

// A byte-level M0110 and its connection to the VIA shift register.
class MacKeyboard {
public:
    static constexpr uint8_t max_scan_code = 0x39;
    static constexpr std::size_t queue_capacity = 64;

    void set_auxiliary_control(uint8_t value, uint64_t cycle);
    void write_shift_register(uint8_t value, uint64_t cycle);
    uint8_t shift_register() const { return shift_register_; }
    bool queue_key(uint8_t scan_code, bool pressed, uint64_t cycle);

    // A true result sets the VIA shift register's IFR bit. The caller clears
    // that bit on guest accesses to the shift register or writes to the IFR.
    bool advance(uint64_t cycle);

private:
    enum class Phase { idle, sending, waiting, receiving };

    void begin_send(uint64_t cycle);
    void begin_reply(uint64_t cycle, uint8_t value, bool consumes_key = false);
    void prepare_reply(uint64_t cycle);

    std::array<uint8_t, queue_capacity> keys_{};
    std::size_t head_ = 0;
    std::size_t key_count_ = 0;
    Phase phase_ = Phase::idle;
    uint64_t transfer_end_ = 0;
    uint64_t inquiry_expiry_ = 0;
    uint8_t shift_mode_ = 0;
    uint8_t shift_register_ = 0;
    uint8_t command_ = 0;
    uint8_t reply_ = 0;
    bool consumes_key_ = false;
    bool preloaded_ = false;
};

#endif
