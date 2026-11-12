#ifndef BOOTHUSH_MAC_MOUSE_TRACE_H
#define BOOTHUSH_MAC_MOUSE_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MAC_MOUSE_TRACE_MAX_BYTES = 4 * 1024 * 1024,
    MAC_MOUSE_TRACE_MAX_SAMPLES = 65536,
};

typedef struct mac_mouse_trace mac_mouse_trace;

typedef struct {
    uint64_t cycle;
    int32_t dx, dy;
    bool button_changed;
    bool pressed;
} mac_mouse_sample;

mac_mouse_trace *mac_mouse_trace_create(uint32_t edge_cycles, uint8_t dcd_invert,
                                       uint32_t vbl_guard_cycles);
mac_mouse_trace *mac_mouse_trace_parse(const uint8_t *bytes, size_t size);
void mac_mouse_trace_destroy(mac_mouse_trace *trace);

uint32_t mac_mouse_trace_edge_cycles(const mac_mouse_trace *trace);
uint8_t mac_mouse_trace_dcd_invert(const mac_mouse_trace *trace);
uint32_t mac_mouse_trace_vbl_guard_cycles(const mac_mouse_trace *trace);
uint64_t mac_mouse_trace_stop_cycle(const mac_mouse_trace *trace);
size_t mac_mouse_trace_count(const mac_mouse_trace *trace);
const mac_mouse_sample *mac_mouse_trace_samples(const mac_mouse_trace *trace);
bool mac_mouse_trace_append(mac_mouse_trace *trace, mac_mouse_sample sample);
/* Undo a successful append if its input sample could not be queued. */
void mac_mouse_trace_pop(mac_mouse_trace *trace);

/* Buffer remains valid until this trace is serialized again or destroyed. */
const uint8_t *mac_mouse_trace_serialize(mac_mouse_trace *trace, uint64_t stop_cycle, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
