#ifndef BOOTHUSH_MAC_LIVE_WINDOW_H
#define BOOTHUSH_MAC_LIVE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "mac128k.h"
#include "live_window.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mac_live_window mac_live_window;

typedef struct {
    int32_t dx, dy;
    bool button_changed;
    bool pressed;
} mac_live_event;

typedef enum {
    MAC_LIVE_RUNNING,
    MAC_LIVE_CLOSED,
    MAC_LIVE_INPUT_OVERFLOW,
    MAC_LIVE_INPUT_ERROR,
} mac_live_status;

typedef struct {
    bool enabled, captured, unconfined;
    uint64_t packets;
    unsigned captures, releases;
    unsigned initial_gain_percent, gain_percent, gain_changes;
    const char *failure;
    uint32_t win32_error;
} mac_live_pointer_state;

typedef struct {
    uint64_t elapsed_ms;
    int32_t raw_dx, raw_dy, guest_dx, guest_dy, host_dx, host_dy;
    uint32_t capture, scale, gain_percent;
    bool host_valid;
} mac_live_pointer_sample;

/* Optional bounded diagnostic. Host deltas are sampled desktop-pointer
 * displacement, not per-packet accelerated counts or absolute positions. */
bool mac_live_window_enable_pointer_log(mac_live_window *window);
const mac_live_pointer_sample *mac_live_window_pointer_log(const mac_live_window *window,
    uint32_t *count, uint64_t *dropped);

/* Optional foreground relative mouse capture. F12 releases; click recaptures. */
bool mac_live_window_relative_mode(mac_live_window *window,bool enabled);
/* Explicit unconfined mode keeps the host pointer visible and never clips it. */
bool mac_live_window_relative_options(mac_live_window *window,bool enabled,bool confine);
/* Configure before enabling relative input. F9/F10 halve/double it at runtime. */
bool mac_live_window_relative_gain(mac_live_window *window,unsigned percent);
mac_live_pointer_state mac_live_window_pointer_state(const mac_live_window *window);
/* Hidden-test counterpart to a relative RAWMOUSE packet; never global input. */
bool mac_live_window_inject_relative(mac_live_window *window,int32_t dx,int32_t dy);

/* Call all functions on the thread that creates the window. Scale is 1 through 4. */
mac_live_window *mac_live_window_create(unsigned scale, bool visible);
void mac_live_window_destroy(mac_live_window *window);

/* Pump a bounded set of messages. Closing or overflowing stops the live run. */
mac_live_status mac_live_window_pump(mac_live_window *window);

/* Motion is relative to the display viewport, in guest pixels, with positive Y down. */
bool mac_live_window_next_event(mac_live_window *window, mac_live_event *event);

/* Inject a logical primary-button event into an invisible test window. */
bool mac_live_window_inject(mac_live_window *window, unsigned x, unsigned y, unsigned action);

/* Copy an MSB-first packed monochrome frame, where 1 is black. */
void mac_live_window_present(mac_live_window *window, const uint8_t frame[MAC128K_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
