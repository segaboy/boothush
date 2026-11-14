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
/* region: 0=header/default, 1=NTSC, 2=PAL */
nes *nes_create_region(const uint8_t *rom, size_t size, unsigned region, const char **error);
typedef struct { unsigned mapper, submapper, header_region, pal, selected_bank, battery_flag; size_t prg_rom, chr_rom, prg_ram, chr_ram; } nes_cartridge_info;
typedef struct { uint64_t cycle,step,frame; uint16_t pc; uint8_t kind,port,value,strobe; } nes_controller_witness_event;
/* kind: 0 execution, 1 CPU read, 2 CPU write. UINT32_MAX means no PRG bank. */
typedef struct { uint64_t cycle,step,frame; uint16_t pc,address; uint32_t pc_bank,selected_before,selected_after; uint8_t kind,value,old_value,old_valid,success,interrupt; } nes_execution_event;
typedef struct { uint32_t exec_first,exec_length,read_first,read_length,write_first,write_length,limit; uint64_t cycle_first,cycle_last; } nes_execution_filter;
nes_cartridge_info nes_get_cartridge_info(const nes *machine);
void nes_destroy(nes *machine);
bool nes_step(nes *machine);
void nes_set_controller(nes *machine, unsigned port, uint8_t buttons);
/* Guest reads/writes only, recorded at instruction-start timing. */
bool nes_enable_controller_witness(nes *machine, uint64_t first_cycle, uint64_t last_cycle);
const nes_controller_witness_event *nes_controller_witness(const nes *machine, size_t *count, uint64_t *dropped);
bool nes_enable_execution_witness(nes *machine,const nes_execution_filter *filter);
const nes_execution_event *nes_execution_witness(const nes *machine,size_t *count,uint64_t *dropped);
bool nes_capture_audio(nes *machine);
const int16_t *nes_audio(const nes *machine, size_t *sample_count);
void nes_audio_consume(nes *machine, size_t sample_count);
nes_state nes_get_state(const nes *machine);
bool nes_inspect(const nes *machine, uint16_t address, uint8_t *out, size_t size);
bool nes_inspect_ppu(const nes *machine, uint16_t address, uint8_t *out, size_t size);
bool nes_inspect_oam(const nes *machine, uint16_t address, uint8_t *out, size_t size);
/* Last completed 256x240 RGB frame, or NULL if none has completed. */
const uint8_t *nes_frame(const nes *machine);
#ifdef __cplusplus
}
#endif
#endif
