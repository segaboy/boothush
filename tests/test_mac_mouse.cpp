#include "mac_mouse.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool result, const char *message)
{
    if (!result) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct GuestPointer {
    mac_mouse_state pins{2, 2, false};
    int x = 0;
    int y = 0;
    int presses = 0;
    int releases = 0;

    void observe(mac_mouse_state next, uint8_t invert)
    {
        const unsigned x_edge = pins.x_phase ^ next.x_phase;
        const unsigned y_edge = pins.y_phase ^ next.y_phase;
        require(x_edge < 3 && y_edge < 3, "two-bit jump on a quadrature axis");
        if (x_edge == 1) {
            const bool dcd = ((next.x_phase ^ invert) & 1) != 0;
            x += dcd == ((next.x_phase & 2) != 0) ? 1 : -1;
        }
        if (y_edge == 1) {
            const bool dcd = ((next.y_phase ^ (invert >> 1)) & 1) != 0;
            y += dcd != ((next.y_phase & 2) != 0) ? 1 : -1;
        }
        if (next.pressed != pins.pressed) {
            require(x_edge == 0 && y_edge == 0, "button and motion were delivered together");
            if (next.pressed) {
                ++presses;
            } else {
                ++releases;
            }
        }
        pins = next;
    }
};

mac_mouse_state run_edge(MacMouse &mouse)
{
    const auto cycle = mouse.next_cycle();
    require(cycle.has_value(), "queued edge has no deadline");
    const auto state = mouse.advance(*cycle);
    require(state.has_value(), "queued edge missed its deadline");
    require(!mouse.advance(*cycle), "a second edge was delivered at the same cycle");
    return *state;
}

void test_axes_and_reversals()
{
    for (uint8_t invert = 0; invert <= 3; ++invert) {
        for (int dx = -7; dx <= 7; ++dx) {
            for (int dy = -7; dy <= 7; ++dy) {
                MacMouse mouse(4, invert);
                GuestPointer pointer;
                require(mouse.queue_motion(0, dx, dy), "initial movement rejected");
                while (mouse.pending_transitions() != 0) {
                    pointer.observe(run_edge(mouse), invert);
                }
                require(pointer.x == dx && pointer.y == dy, "wrong movement or polarity");

                require(mouse.queue_motion(MacMouse::default_vbl_guard_cycles + 1000, -dx, -dy),
                        "movement reversal rejected");
                while (mouse.pending_transitions() != 0) {
                    pointer.observe(run_edge(mouse), invert);
                }
                require(pointer.x == 0 && pointer.y == 0, "reversal lost movement");
                require(pointer.pins.x_phase == 2 && pointer.pins.y_phase == 2,
                        "reversal did not restore the original phases");
            }
        }
    }
}

void test_diagonal_pacing_and_button_order()
{
    constexpr uint64_t button_gap = MacMouse::button_hold_cycles;
    MacMouse mouse(64, 3);
    GuestPointer pointer;
    const uint64_t first = MacMouse::default_vbl_guard_cycles + 10;
    require(!mouse.next_cycle(), "idle mouse has a deadline");
    require(mouse.queue_motion(first, 6, 2), "initial diagonal motion rejected");
    require(mouse.queue_button(first, true), "press rejected");
    require(mouse.queue_button(first, true), "duplicate press rejected");
    require(mouse.queue_motion(first, -2, 1), "drag motion rejected");
    require(mouse.queue_button(first, false), "release rejected");
    require(mouse.pending_transitions() == 18, "incorrect transition count for diagonal drag");
    require(!mouse.advance(first - 1), "event fired before its timestamp");

    for (int edge = 0; edge < 12; ++edge) {
        pointer.observe(run_edge(mouse), 3);
        require(!pointer.pins.pressed, "press arrived ahead of preceding movement");
        if (edge == 5) {
            require(pointer.x == 3 && pointer.y == 1, "diagonal movement was not distributed");
        }
    }
    require(pointer.x == 6 && pointer.y == 2, "initial movement ended at the wrong point");

    const auto press_cycle = *mouse.next_cycle();
    pointer.observe(run_edge(mouse), 3);
    require(pointer.presses == 1, "queued press was not observed");
    require(mouse.next_cycle() == press_cycle + button_gap, "press has no VBL observation window");
    require(!mouse.advance(press_cycle + button_gap - 1), "motion started before the press could be sampled");
    for (int edge = 0; edge < 4; ++edge) {
        pointer.observe(run_edge(mouse), 3);
        require(pointer.pins.pressed, "release arrived ahead of preceding drag motion");
    }
    pointer.observe(run_edge(mouse), 3);
    require(pointer.x == 4 && pointer.y == 3 && pointer.releases == 1, "drag or release was lost");
    require(mouse.pending_transitions() == 0 && !mouse.next_cycle(), "drained mouse still has work");
}

void test_late_delivery_and_bounds()
{
    const uint64_t guard = MacMouse::default_vbl_guard_cycles;
    MacMouse mouse(64);
    require(mouse.queue_motion(10, 2, 0), "motion rejected");
    const auto late = guard + 1000;
    require(mouse.advance(late).has_value(), "late call did not deliver an edge");
    require(!mouse.advance(late), "late call burst multiple edges");
    require(mouse.next_cycle() == late + 64, "next edge was paced from nominal time");
    require(!mouse.queue_button(late - 1, true), "past event accepted after delivering a newer edge");
    require(mouse.queue_button(late, true), "current-boundary event rejected");
    require(!mouse.queue_motion(late, std::numeric_limits<int32_t>::min(), 0), "INT32_MIN exceeded work cap");
    require(mouse.pending_transitions() == 4, "rejected work changed pending count");

    MacMouse large(1);
    require(large.queue_motion(0, MacMouse::transition_capacity / 2, 0), "maximum-sized motion rejected");
    require(!large.queue_button(1, true), "pending transition capacity exceeded");
    run_edge(large);
    require(large.queue_button(guard, true), "a refused press changed the queued button state");
    require(large.pending_transitions() == MacMouse::transition_capacity, "work capacity was not reused");

    MacMouse full(1);
    for (size_t i = 0; i < MacMouse::queue_capacity; ++i) {
        require(full.queue_motion(0, 1, 0), "command queue filled early");
    }
    require(!full.queue_motion(0, -1, 0), "command queue overflow accepted");
    run_edge(full);
    run_edge(full);
    require(full.queue_motion(guard + 1, -1, 0), "command queue did not reuse its drained slot");

    MacMouse terminal(64);
    const uint64_t final_cycle = std::numeric_limits<uint64_t>::max();
    require(terminal.queue_button(final_cycle, true), "terminal press rejected");
    require(run_edge(terminal).pressed, "terminal press was lost");
    require(!terminal.queue_button(final_cycle, false), "unrepresentable next deadline accepted");
}

void test_vbl_guard()
{
    const auto vbl = MacMouse::vbl_cycles;
    const auto guard = MacMouse::default_vbl_guard_cycles;
    MacMouse mouse(64);
    const auto pre_vbl = vbl - guard - 1;
    require(mouse.queue_motion(pre_vbl, 1, 0), "guard test motion rejected");
    require(mouse.next_cycle() == pre_vbl, "pre-VBL deadline was unexpectedly moved");
    require(!mouse.advance(vbl + 2), "instruction overrun emitted an edge inside the guard");
    require(mouse.next_cycle() == vbl + guard, "instruction overrun did not reschedule the edge");
    require(!mouse.advance(vbl + guard - 1), "edge was delivered before guard ended");
    run_edge(mouse);
    require(mouse.next_cycle() == vbl + guard + 64, "post-guard edge was not paced");
    run_edge(mouse);
    require(mouse.queue_button(vbl * 2 + 10, true), "in-guard press rejected");
    require(mouse.next_cycle() == vbl * 2 + guard, "scheduled in-guard press was not deferred");
    require(run_edge(mouse).pressed, "press was lost inside the guard");

    mac_mouse *unguarded = mac_mouse_create_timed(64, 0, 0);
    require(unguarded != nullptr, "unrestricted pacing creation failed");
    require(mac_mouse_queue_motion(unguarded, vbl, 1, 0), "unguarded event refused");
    uint64_t due = 0;
    mac_mouse_state state{};
    require(mac_mouse_next_cycle(unguarded, &due) && due == vbl, "zero guard moved a VBL event");
    require(mac_mouse_advance(unguarded, vbl, &state), "zero guard suppressed a VBL event");
    mac_mouse_destroy(unguarded);
    require(mac_mouse_create_timed(64, 0, MAC_MOUSE_MAX_VBL_GUARD_CYCLES + 1) == nullptr,
            "out-of-range VBL guard was accepted");
}

void test_c_api()
{
    require(mac_mouse_create(0, 0) == nullptr && mac_mouse_create(1, 4) == nullptr,
            "C API accepted an invalid pacing or polarity setting");
    mac_mouse *mouse = mac_mouse_create(MAC_MOUSE_DEFAULT_EDGE_CYCLES, 0);
    require(mouse != nullptr, "C API creation failed");
    const uint64_t first = MAC_MOUSE_DEFAULT_VBL_GUARD_CYCLES + 2000;
    require(mac_mouse_queue_button(mouse, first, true) && mac_mouse_queue_button(mouse, first, false),
            "C API did not queue same-cycle click");
    require(mac_mouse_pending(mouse) == 2, "C API pending count is wrong");
    uint64_t cycle = 0;
    mac_mouse_state state{};
    require(mac_mouse_next_cycle(mouse, &cycle) && cycle == first, "C API deadline is wrong");
    require(mac_mouse_advance(mouse, cycle, &state) && state.pressed, "C API press was not delivered");
    require(!mac_mouse_advance(mouse, cycle, &state) && state.pressed, "C API early release or clobbered output");
    require(mac_mouse_next_cycle(mouse, &cycle) && cycle == first + MacMouse::button_hold_cycles,
            "C API did not hold a click for the guest to sample");
    require(mac_mouse_advance(mouse, cycle, &state) && !state.pressed, "C API release was not delivered");
    mac_mouse_destroy(mouse);
}

} // namespace

int main()
{
    test_axes_and_reversals();
    test_diagonal_pacing_and_button_order();
    test_late_delivery_and_bounds();
    test_vbl_guard();
    test_c_api();
}
