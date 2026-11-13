#ifndef BOOTHUSH_NES_H
#define BOOTHUSH_NES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct nes nes;
typedef struct {
    uint16_t pc, fault_address;
    uint8_t a, x, y, sp, flags, fault_opcode;
    uint64_t steps, cpu_cycles, ppu_frames, apu_writes, dma_transfers;
    bool dmc_used, audio_truncated;
    const char *fault;
} nes_state;
nes *nes_create(const uint8_t *rom, size_t size, const char **error);
void nes_destroy(nes *machine);
bool nes_step(nes *machine);
void nes_set_controller(nes *machine, unsigned port, uint8_t buttons);
bool nes_capture_audio(nes *machine);
const int16_t *nes_audio(const nes *machine, size_t *sample_count);
nes_state nes_get_state(const nes *machine);
bool nes_inspect(const nes *machine, uint16_t address, uint8_t *out, size_t size);
/* Last completed 256x240 RGB frame, or NULL if none has completed. */
const uint8_t *nes_frame(const nes *machine);
#ifdef __cplusplus
}
#endif
#endif
