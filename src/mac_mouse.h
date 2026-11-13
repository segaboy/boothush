#ifndef BOOTHUSH_MAC_MOUSE_H
#define BOOTHUSH_MAC_MOUSE_H

#include "mac_mouse_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Converts relative input to the Mac's SCC DCD and VIA quadrature pins.
// The caller owns cycle time, serializes all calls and applies returned states
// at CPU boundaries. Phase bit 0 is DCD and bit 1 is VIA PB4 (X) or PB5 (Y).
class MacMouse {
public:
    static constexpr uint32_t default_edge_cycles = MAC_MOUSE_DEFAULT_EDGE_CYCLES;
    static constexpr uint32_t vbl_cycles = 130240;
    static constexpr uint32_t default_vbl_guard_cycles = MAC_MOUSE_DEFAULT_VBL_GUARD_CYCLES;
    static constexpr uint32_t button_hold_cycles = 2 * vbl_cycles;
    static constexpr std::size_t queue_capacity = 1024;
    static constexpr uint32_t transition_capacity = 16384;

    // Invert bits 0/1 match the guest's expected raw DCD polarity for X/Y.
    explicit MacMouse(uint32_t edge_cycles = default_edge_cycles, uint8_t dcd_invert = 0,
                      uint32_t vbl_guard_cycles = default_vbl_guard_cycles);

    // Timestamps must be nondecreasing. A false return leaves all queued input
    // intact. A zero delta or an unchanged queued button state adds no work.
    bool queue_motion(uint64_t cycle, int32_t dx, int32_t dy);
    bool queue_button(uint64_t cycle, bool pressed);
    bool queue_sample(uint64_t cycle, int32_t dx, int32_t dy, bool button_changed, bool pressed);

    // A queued vector uses two Gray edges per unit of its longer axis. Button
    // changes each take one transition and stay separate from motion vectors.
    uint32_t pending_transitions() const { return pending_transitions_; }
    std::optional<uint64_t> next_cycle() const;
    std::optional<uint64_t> unguarded_next_cycle() const;
    std::optional<mac_mouse_state> advance(uint64_t cycle);

private:
    struct Event {
        uint64_t cycle = 0;
        uint32_t x_distance = 0;
        uint32_t y_distance = 0;
        uint32_t distance = 0;
        uint32_t transitions = 0;
        uint8_t x_turn = 0;
        uint8_t y_turn = 0;
        bool pressed = false;
    };

    void append(Event event);
    std::optional<uint64_t> after_vbl_guard(uint64_t cycle) const;

    std::array<Event, queue_capacity> queue_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    uint32_t pending_transitions_ = 0;
    uint64_t last_input_cycle_ = 0;
    std::optional<uint64_t> ready_cycle_ = 0;
    uint32_t edge_cycles_;
    uint32_t vbl_guard_cycles_;
    uint8_t dcd_invert_;
    uint8_t x_phase_index_ = 3;
    uint8_t y_phase_index_ = 3;
    bool pressed_ = false;
    bool queued_pressed_ = false;
};

#endif
