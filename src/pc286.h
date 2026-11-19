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
    PC286_HARD_DISK_SIZE = 615 * 4 * 17 * 512,
    PC286_EGA_ROM_MAX = 32768,
    PC286_VIDEO_END = 0xc0000,
    PC286_FRAME_WIDTH = 640,
    PC286_FRAME_HEIGHT = 350,
    PC286_FRAME_SIZE = PC286_FRAME_WIDTH * PC286_FRAME_HEIGHT * 3,
};

typedef struct pc286 pc286;

enum { PC286_WITNESS_CAPACITY = 65536 };
typedef struct {
    uint64_t cycle;
    uint32_t pc, address, value;
    uint16_t cs, ip;
    uint8_t kind, width;
} pc286_witness_event;
enum { PC286_WITNESS_STEP, PC286_WITNESS_READ, PC286_WITNESS_WRITE,
       PC286_WITNESS_IN, PC286_WITNESS_OUT, PC286_WITNESS_IRQ, PC286_WITNESS_EXCEPTION };

bool pc286_start_witness(pc286 *machine, uint32_t mem_start, uint32_t mem_end,
                         uint32_t io_start, uint32_t io_end, uint64_t first_cycle, uint64_t last_cycle);
const pc286_witness_event *pc286_witness(const pc286 *machine, size_t *count, uint64_t *dropped);

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
    uint32_t keyboard_output_queued, keyboard_output_capacity;
    bool read_failure_enabled;
    uint8_t read_failure_cylinder, read_failure_head, read_failure_sector;
    uint64_t read_failures;
    bool hard_read_failure_enabled;
    bool hard_read_failure_once;
    uint32_t hard_read_failure_at;
    uint64_t hard_read_matches;
    uint16_t hard_read_failure_cylinder;
    uint8_t hard_read_failure_head, hard_read_failure_sector;
    uint64_t hard_read_failures;
    bool hard_write_failure_enabled;
    uint32_t hard_write_failure_at;
    uint64_t hard_write_matches;
    uint16_t hard_write_failure_cylinder;
    uint8_t hard_write_failure_head, hard_write_failure_sector;
    uint64_t hard_write_failures;
    uint32_t hard_reset_failure_at;
    uint64_t hard_reset_matches, hard_reset_failures;
} pc286_state;

/* These constructors copy the supplied image. NULL indicates invalid size or allocation failure. */
pc286 *pc286_create(const uint8_t *rom, size_t size);
pc286 *pc286_create_floppy(const uint8_t *image, size_t size);
pc286 *pc286_create_system(const uint8_t *rom, size_t rom_size,
                           const uint8_t *floppy, size_t floppy_size,
                           const uint8_t *disk, size_t disk_size);
enum pc286_boot_drive { PC286_BOOT_AUTO, PC286_BOOT_FLOPPY, PC286_BOOT_HARD_DISK };
pc286 *pc286_create_system_with_boot_drive(const uint8_t *rom, size_t rom_size,
                                          const uint8_t *floppy, size_t floppy_size,
                                          const uint8_t *disk, size_t disk_size,
                                          enum pc286_boot_drive boot_drive);
/* Built-in firmware only: opt in to BIOS sector writes on the owned copy.
 * Rejects NULL, absent floppy or external firmware; FDC writes are unchanged. */
bool pc286_enable_floppy_writes(pc286 *machine);
/* Borrowed owned image, PC286_FLOPPY_SIZE bytes, valid until destruction.
 * NULL for NULL machine or absent floppy. Input media is never modified. */
const uint8_t *pc286_floppy(const pc286 *machine);
bool pc286_load_ega_rom(pc286 *machine, const uint8_t *rom, size_t size);
bool pc286_set_ega_font(pc286 *machine, const uint8_t *font, size_t size);
/* Built-in firmware only: fail every valid floppy read beginning at this CHS. */
bool pc286_fail_floppy_read(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector);
/* Built-in firmware only: fail every valid hard-disk read beginning at CHS. */
bool pc286_fail_hard_disk_read(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector);
/* Same selection, but inject only its first valid matching read. */
bool pc286_fail_hard_disk_read_once(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector);
/* Fail exactly the positive one-based matching read occurrence. */
bool pc286_fail_hard_disk_read_at(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector, uint32_t occurrence);
/* Built-in firmware only: fail every valid hard-disk write beginning at CHS. */
bool pc286_fail_hard_disk_write(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector);
/* Fail exactly the positive one-based matching valid write occurrence. */
bool pc286_fail_hard_disk_write_at(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector, uint32_t occurrence);
/* Built-in firmware only: fail one valid hard-disk reset occurrence. */
bool pc286_fail_hard_disk_reset_at(pc286 *machine, uint32_t occurrence);
const uint8_t *pc286_hard_disk(const pc286 *machine);
bool pc286_inject_scancode(pc286 *machine, uint8_t scancode);
void pc286_expect_input(pc286 *machine, bool pending);
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
