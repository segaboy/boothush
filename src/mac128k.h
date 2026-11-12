#ifndef BOOTHUSH_MAC128K_H
#define BOOTHUSH_MAC128K_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* Queues a guest M0110 key transition without touching host input. */
bool mac128k_inject_key(mac128k *machine, uint8_t code, bool pressed);

/* Reads physical RAM without triggering guest bus side effects. */
bool mac128k_read_ram(const mac128k *machine, uint32_t address, uint8_t *data, size_t size);
const uint8_t *mac128k_frame(const mac128k *machine);

#ifdef __cplusplus
}
#endif

#endif
