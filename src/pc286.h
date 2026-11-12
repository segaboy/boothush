#ifndef BOOTHUSH_PC286_H
#define BOOTHUSH_PC286_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC286_ROM_SIZE = 65536,
    PC286_RAM_SIZE = 640 * 1024,
    PC286_FLOPPY_SIZE = 80 * 2 * 15 * 512,
    PC286_VIDEO_END = 0xc0000,
    PC286_FRAME_WIDTH = 640,
    PC286_FRAME_HEIGHT = 350,
    PC286_FRAME_SIZE = PC286_FRAME_WIDTH * PC286_FRAME_HEIGHT * 3,
};

typedef struct pc286 pc286;

typedef struct {
    uint64_t cycles;
    uint32_t pc;
    uint32_t fault_address;
    const char *fault_operation;
    uint16_t cs, ip;
    int last_exception;
    uint8_t post;
    bool post_written;
    bool halted;
} pc286_state;

/* These constructors copy the supplied image. NULL indicates invalid size or allocation failure. */
pc286 *pc286_create(const uint8_t *rom, size_t size);
pc286 *pc286_create_floppy(const uint8_t *image, size_t size);
void pc286_destroy(pc286 *machine);

void pc286_run(pc286 *machine, uint32_t cycles);
pc286_state pc286_get_state(const pc286 *machine);

/* Side-effect-free inspection of conventional RAM and the EGA memory aperture. */
bool pc286_read_ram(const pc286 *machine, uint32_t address, uint8_t *data, size_t size);
void pc286_frame_rgb(const pc286 *machine, uint8_t data[PC286_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
