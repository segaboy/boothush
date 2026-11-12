#ifndef BOOTHUSH_MAC_LIVE_WINDOW_H
#define BOOTHUSH_MAC_LIVE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "mac128k.h"

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
} mac_live_status;

/* Call all functions on the thread that creates the window. Scale is 1 through 4. */
mac_live_window *mac_live_window_create(unsigned scale, bool visible);
void mac_live_window_destroy(mac_live_window *window);

/* Pump a bounded set of messages. Closing or overflowing stops the live run. */
mac_live_status mac_live_window_pump(mac_live_window *window);

/* Motion is relative to the display viewport, in guest pixels, with positive Y down. */
bool mac_live_window_next_event(mac_live_window *window, mac_live_event *event);

/* Copy an MSB-first packed monochrome frame, where 1 is black. */
void mac_live_window_present(mac_live_window *window, const uint8_t frame[MAC128K_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
