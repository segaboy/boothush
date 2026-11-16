#ifndef BOOTHUSH_MAC128K_H
#define BOOTHUSH_MAC128K_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mac_mouse_witness.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MAC128K_ROM_SIZE = 65536,
    MAC128K_FLOPPY_SIZE = 409600,
    MAC128K_MAX_KEY_CODE = 0x39,
    MAC128K_RAM_SIZE = 131072,
    MAC128K_FRAME_WIDTH = 512,
    MAC128K_FRAME_HEIGHT = 342,
    MAC128K_FRAME_SIZE = MAC128K_FRAME_WIDTH / 8 * MAC128K_FRAME_HEIGHT
};

typedef struct mac128k mac128k;

typedef struct {
    uint64_t cycles;
    uint64_t vbl;
    uint32_t pc;
    uint32_t fault_address;
    const char *fault_operation;
} mac128k_state;

/* M0110 witness kinds 0-8 follow queue, reject, mode, SR write, command
   completion, reply start/completion, key delivery, SR read; 9 is input due. */
typedef struct { uint64_t cycle,due; uint32_t pc; uint8_t kind,value,queue; } mac128k_keyboard_event;
typedef struct { uint64_t cycle,vbl; uint32_t pc,address,value; uint8_t kind,width; } mac128k_display_event;

/* Copies a complete ROM and resets the machine. NULL indicates allocation failure. */
mac128k *mac128k_create(const uint8_t rom[MAC128K_ROM_SIZE]);
void mac128k_destroy(mac128k *machine);

/* Inserts a read-only raw or Disk Copy 4.2 Macintosh 400 KiB floppy. */
bool mac128k_insert_floppy(mac128k *machine, const uint8_t *image, size_t size);

/* Executes whole instructions up to the requested slice or the first machine fault. */
void mac128k_run(mac128k *machine, uint32_t cycles);
mac128k_state mac128k_get_state(const mac128k *machine);

/* Sets the guest VIA's active-low mouse button input. */
void mac128k_set_mouse_button(mac128k *machine, bool pressed);

/* Sets raw quadrature phases. Bit 0 is SCC DCD, bit 1 is VIA PB4 (X) or PB5 (Y). */
void mac128k_set_mouse_phase(mac128k *machine, uint8_t x_phase, uint8_t y_phase);

/* Enables device observations for the inclusive cycle range and clears old rows. */
bool mac128k_start_mouse_witness(mac128k *machine, uint64_t first_cycle, uint64_t last_cycle);

/* Detailed observations are opt-in; the legacy witness remains unchanged. */
void mac128k_mouse_witness_detailed(mac128k *machine, bool enabled);
/* Opt-in instruction boundaries in the same bounded detailed witness. */
bool mac128k_mouse_witness_cpu(mac128k *machine, bool enabled);
/* Physical RAM offsets, not bus aliases; valid only for an active detailed witness. */
bool mac128k_mouse_witness_memory(mac128k *machine, uint32_t first, uint32_t length);
void mac128k_mouse_witness_memory_stats(const mac128k *machine, uint64_t *matched, uint64_t *dropped);
void mac128k_mouse_witness_pending(mac128k *machine, uint32_t pending);
void mac128k_mouse_witness_input(mac128k *machine, enum mac128k_mouse_witness_kind kind,
                                 uint8_t source, int32_t dx, int32_t dy, uint8_t value,
                                 uint32_t pending, uint64_t due, uint64_t deadline);

/* Returns the retained prefix and counts discarded rows after the cap. */
const mac128k_mouse_witness_event *mac128k_mouse_witness(const mac128k *machine,
                                                        size_t *count, uint64_t *dropped);

/* Queues a guest M0110 key transition without touching host input. */
bool mac128k_inject_key(mac128k *machine, uint8_t code, bool pressed);
bool mac128k_keyboard_fault(mac128k *machine,uint8_t command,uint32_t occurrence,uint8_t kind,uint32_t argument);
bool mac128k_keyboard_fault_status(const mac128k *machine,uint64_t *cycle);
bool mac128k_start_keyboard_witness(mac128k *machine,uint64_t first,uint64_t last);
bool mac128k_keyboard_witness_limit(mac128k *machine,uint32_t limit);
void mac128k_keyboard_input_due(mac128k *machine,uint64_t due,uint8_t code,bool pressed);
const mac128k_keyboard_event *mac128k_keyboard_witness(const mac128k *machine,size_t *count,uint64_t *dropped,uint64_t *rejected);
/* Optional VBL/fault and selected physical RAM-write witness. */
bool mac128k_start_display_witness(mac128k *machine,uint64_t first,uint64_t last,uint32_t address,uint32_t length);
const mac128k_display_event *mac128k_display_witness(const mac128k *machine,size_t *count,uint64_t *dropped);

/* Reads physical RAM without triggering guest bus side effects. */
bool mac128k_read_ram(const mac128k *machine, uint32_t address, uint8_t *data, size_t size);
const uint8_t *mac128k_frame(const mac128k *machine);

#ifdef __cplusplus
}
#endif

#endif
