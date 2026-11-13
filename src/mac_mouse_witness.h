#ifndef BOOTHUSH_MAC_MOUSE_WITNESS_H
#define BOOTHUSH_MAC_MOUSE_WITNESS_H

#include <stdint.h>

enum mac128k_mouse_witness_kind {
    MAC128K_WITNESS_PHASE,
    MAC128K_WITNESS_BUTTON,
    MAC128K_WITNESS_SCC_READ,
    MAC128K_WITNESS_SCC_WRITE,
    MAC128K_WITNESS_VIA_READ,
    MAC128K_WITNESS_VIA_WRITE,
    MAC128K_WITNESS_IRQ,
    MAC128K_WITNESS_SNAPSHOT,
    MAC128K_WITNESS_IRQ_ACCEPT,
    MAC128K_WITNESS_INPUT_DUE,
    MAC128K_WITNESS_SAMPLE_ACCEPTED,
    MAC128K_WITNESS_SAMPLE_REJECTED,
    MAC128K_WITNESS_GUARD,
};

enum { MAC128K_MOUSE_WITNESS_CAPACITY = 65536 };

typedef struct {
    uint64_t cycle;
    uint32_t address;
    uint8_t kind;
    uint8_t reg;
    uint8_t value;
    uint8_t x_phase;
    uint8_t y_phase;
    uint8_t pressed;
    uint8_t scc_pending;
    uint8_t scc_latched;
    uint8_t scc_dcd;
    uint8_t via_ifr;
    uint8_t via_ier;
    uint8_t irq_level;
    uint32_t pc;
    int32_t dx, dy;
    uint32_t pending;
    uint64_t due, deadline;
    uint8_t source;
} mac128k_mouse_witness_event;

#endif
