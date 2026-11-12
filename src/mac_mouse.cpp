#include "mac_mouse.h"

#include <algorithm>
#include <new>

namespace {

constexpr std::array<uint8_t, 4> gray_phases{0, 1, 3, 2};

uint32_t magnitude(int32_t value)
{
    return value < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(value)) : value;
}

bool takes_axis_step(uint32_t step, uint32_t distance, uint32_t span)
{
    // Both phases of a movement unit must step the same axes. The queue's
    // transition bound keeps step * distance within uint32_t.
    return (step * distance + span / 2) % span + distance >= span;
}

} // namespace

std::optional<uint64_t> MacMouse::after_vbl_guard(uint64_t cycle) const
{
    // Mac guest VBL handlers inspect SCC and VIA in a sequence of reads.
    // Leave those reads undisturbed around the start of each emulated VBL.
    const uint64_t phase = cycle % MacMouse::vbl_cycles;
    if (phase >= vbl_guard_cycles_ && phase < vbl_cycles - vbl_guard_cycles_) {
        return cycle;
    }
    const uint64_t delay = (vbl_guard_cycles_ + vbl_cycles - phase) % vbl_cycles;
    if (cycle > UINT64_MAX - delay) {
        return std::nullopt;
    }
    return cycle + delay;
}

MacMouse::MacMouse(uint32_t edge_cycles, uint8_t dcd_invert, uint32_t vbl_guard_cycles)
    : edge_cycles_(std::max(1u, edge_cycles)),
      vbl_guard_cycles_(std::min<uint32_t>(vbl_guard_cycles, MAC_MOUSE_MAX_VBL_GUARD_CYCLES)),
      dcd_invert_(dcd_invert & 3)
{
}

bool MacMouse::queue_motion(uint64_t cycle, int32_t dx, int32_t dy)
{
    return queue_sample(cycle, dx, dy, false, false);
}

bool MacMouse::queue_button(uint64_t cycle, bool pressed)
{
    return queue_sample(cycle, 0, 0, true, pressed);
}

bool MacMouse::queue_sample(uint64_t cycle, int32_t dx, int32_t dy, bool button_changed, bool pressed)
{
    const uint32_t x = magnitude(dx);
    const uint32_t y = magnitude(dy);
    const uint32_t span = std::max(x, y);
    if (span > transition_capacity / 2 || cycle < last_input_cycle_) {
        return false;
    }
    const bool button_edge = button_changed && pressed != queued_pressed_;
    const uint32_t transitions = span * 2 + button_edge;
    const size_t commands = static_cast<size_t>(span != 0) + button_edge;
    if (commands != 0 && (!ready_cycle_ || !after_vbl_guard(std::max(cycle, *ready_cycle_)) ||
        commands > queue_capacity - size_ || transitions > transition_capacity - pending_transitions_)) {
        return false;
    }
    if (span != 0) {
        // Forward Gray order moves left and down without guest DCD inversion.
        const uint8_t x_turn = (dx < 0) != ((dcd_invert_ & 1) != 0) ? 1 : 3;
        const uint8_t y_turn = (dy > 0) != ((dcd_invert_ & 2) != 0) ? 1 : 3;
        append({cycle, x, y, span, span * 2, x_turn, y_turn, false});
    }
    if (button_edge) {
        append({cycle, 0, 0, 0, 1, 0, 0, pressed});
        queued_pressed_ = pressed;
    }
    last_input_cycle_ = cycle;
    return true;
}

void MacMouse::append(Event event)
{
    queue_[(head_ + size_) % queue_capacity] = event;
    ++size_;
    pending_transitions_ += event.transitions;
}

std::optional<uint64_t> MacMouse::next_cycle() const
{
    if (size_ == 0 || !ready_cycle_) {
        return std::nullopt;
    }
    return after_vbl_guard(std::max(queue_[head_].cycle, *ready_cycle_));
}

std::optional<mac_mouse_state> MacMouse::advance(uint64_t cycle)
{
    const auto next = next_cycle();
    if (!next || cycle < *next) {
        return std::nullopt;
    }
    const auto guarded = after_vbl_guard(cycle);
    if (!guarded || *guarded != cycle) {
        // The CPU can complete an instruction just after the next VBL starts.
        // Publish a future deadline so the caller keeps advancing the guest.
        ready_cycle_ = guarded;
        last_input_cycle_ = std::max(last_input_cycle_, cycle);
        return std::nullopt;
    }

    Event &event = queue_[head_];
    uint32_t delay = edge_cycles_;
    if (event.distance == 0) {
        pressed_ = event.pressed;
        delay = std::max(edge_cycles_, button_hold_cycles);
    } else {
        const uint32_t step = event.distance - (event.transitions + 1) / 2;
        if (takes_axis_step(step, event.x_distance, event.distance)) {
            x_phase_index_ = (x_phase_index_ + event.x_turn) & 3;
        }
        if (takes_axis_step(step, event.y_distance, event.distance)) {
            y_phase_index_ = (y_phase_index_ + event.y_turn) & 3;
        }
    }

    // Space the next edge from its actual delivery boundary. A late caller
    // must leave time for the guest to observe this edge before applying more.
    if (cycle > UINT64_MAX - delay) {
        ready_cycle_.reset();
    } else {
        ready_cycle_ = cycle + delay;
    }
    last_input_cycle_ = std::max(last_input_cycle_, cycle);
    --pending_transitions_;
    if (--event.transitions == 0) {
        head_ = (head_ + 1) % queue_capacity;
        --size_;
    }
    return mac_mouse_state{gray_phases[x_phase_index_], gray_phases[y_phase_index_], pressed_};
}

struct mac_mouse {
    MacMouse mouse;
};

mac_mouse *mac_mouse_create(uint32_t edge_cycles, uint8_t dcd_invert)
{
    return mac_mouse_create_timed(edge_cycles, dcd_invert, MAC_MOUSE_DEFAULT_VBL_GUARD_CYCLES);
}

mac_mouse *mac_mouse_create_timed(uint32_t edge_cycles, uint8_t dcd_invert, uint32_t vbl_guard_cycles)
{
    if (edge_cycles == 0 || dcd_invert > 3 || vbl_guard_cycles > MAC_MOUSE_MAX_VBL_GUARD_CYCLES) {
        return nullptr;
    }
    return new (std::nothrow) mac_mouse{MacMouse(edge_cycles, dcd_invert, vbl_guard_cycles)};
}

void mac_mouse_destroy(mac_mouse *mouse)
{
    delete mouse;
}

bool mac_mouse_queue_motion(mac_mouse *mouse, uint64_t cycle, int32_t dx, int32_t dy)
{
    return mouse->mouse.queue_motion(cycle, dx, dy);
}

bool mac_mouse_queue_button(mac_mouse *mouse, uint64_t cycle, bool pressed)
{
    return mouse->mouse.queue_button(cycle, pressed);
}

bool mac_mouse_queue_sample(mac_mouse *mouse, uint64_t cycle, int32_t dx, int32_t dy,
                            bool button_changed, bool pressed)
{
    return mouse->mouse.queue_sample(cycle, dx, dy, button_changed, pressed);
}

bool mac_mouse_next_cycle(const mac_mouse *mouse, uint64_t *cycle)
{
    if (const auto next = mouse->mouse.next_cycle()) {
        *cycle = *next;
        return true;
    }
    return false;
}

bool mac_mouse_advance(mac_mouse *mouse, uint64_t cycle, mac_mouse_state *state)
{
    if (const auto next = mouse->mouse.advance(cycle)) {
        *state = *next;
        return true;
    }
    return false;
}

uint32_t mac_mouse_pending(const mac_mouse *mouse)
{
    return mouse->mouse.pending_transitions();
}
