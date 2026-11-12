#ifndef BOOTHUSH_MAC_MOUSE_API_H
#define BOOTHUSH_MAC_MOUSE_API_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MAC_MOUSE_DEFAULT_EDGE_CYCLES = 8192,
    MAC_MOUSE_DEFAULT_VBL_GUARD_CYCLES = 4096,
    MAC_MOUSE_MAX_VBL_GUARD_CYCLES = 8192,
};

typedef struct mac_mouse mac_mouse;

typedef struct {
    uint8_t x_phase;
    uint8_t y_phase;
    bool pressed;
} mac_mouse_state;

/* edge_cycles must be positive. dcd_invert is a two-bit X/Y polarity mask. */
mac_mouse *mac_mouse_create(uint32_t edge_cycles, uint8_t dcd_invert);
/* The guard applies on both sides of VBL. Zero permits input at any phase. */
mac_mouse *mac_mouse_create_timed(uint32_t edge_cycles, uint8_t dcd_invert, uint32_t vbl_guard_cycles);
void mac_mouse_destroy(mac_mouse *mouse);
bool mac_mouse_queue_motion(mac_mouse *mouse, uint64_t cycle, int32_t dx, int32_t dy);
bool mac_mouse_queue_button(mac_mouse *mouse, uint64_t cycle, bool pressed);
bool mac_mouse_next_cycle(const mac_mouse *mouse, uint64_t *cycle);
bool mac_mouse_advance(mac_mouse *mouse, uint64_t cycle, mac_mouse_state *state);
uint32_t mac_mouse_pending(const mac_mouse *mouse);

#ifdef __cplusplus
}
#endif

#endif
