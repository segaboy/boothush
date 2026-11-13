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
    uint64_t steps;
    const char *fault;
} nes_state;
nes *nes_create(const uint8_t *rom, size_t size, const char **error);
void nes_destroy(nes *machine);
bool nes_step(nes *machine);
nes_state nes_get_state(const nes *machine);
bool nes_inspect(const nes *machine, uint16_t address, uint8_t *out, size_t size);
#ifdef __cplusplus
}
#endif
#endif
