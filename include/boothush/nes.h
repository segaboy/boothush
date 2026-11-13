#ifndef BOOTHUSH_PUBLIC_NES_H
#define BOOTHUSH_PUBLIC_NES_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# if defined(BOOTHUSH_NES_BUILD)
#  define BH_NES_API __declspec(dllexport)
# else
#  define BH_NES_API __declspec(dllimport)
# endif
#else
# define BH_NES_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define BH_NES_API_VERSION 0x000101u
#define BH_NES_WIDTH 256u
#define BH_NES_HEIGHT 240u
#define BH_NES_FRAME_BYTES (BH_NES_WIDTH * BH_NES_HEIGHT * 3u)
#define BH_NES_AUDIO_RATE 44100u
#define BH_NES_MAX_STEPS 1000000u

typedef struct bh_nes bh_nes;
typedef enum bh_nes_result {
    BH_NES_OK = 0, BH_NES_INVALID_ARGUMENT = 1,
    BH_NES_INVALID_CARTRIDGE = 2, BH_NES_OUT_OF_MEMORY = 3,
    BH_NES_CPU_FAULT = 4, BH_NES_NO_FRAME = 5,
    BH_NES_BUFFER_TOO_SMALL = 6, BH_NES_UNMAPPED = 7
} bh_nes_result;
enum bh_nes_buttons {
    BH_NES_A=1, BH_NES_B=2, BH_NES_SELECT=4, BH_NES_START=8,
    BH_NES_UP=16, BH_NES_DOWN=32, BH_NES_LEFT=64, BH_NES_RIGHT=128
};
typedef struct bh_nes_state {
    uint32_t size; /* Set to sizeof(bh_nes_state) before calling. */
    uint16_t pc, fault_address;
    uint8_t a, x, y, sp, flags, fault_opcode;
    uint8_t dmc_used, audio_truncated;
    uint64_t steps, cpu_cycles, ppu_frames, apu_writes, dma_transfers;
    char fault[32]; /* Empty if no fault; otherwise NUL-terminated. */
} bh_nes_state;
typedef enum bh_nes_region { BH_NES_REGION_AUTO=0, BH_NES_REGION_NTSC=1, BH_NES_REGION_PAL=2 } bh_nes_region;
typedef struct bh_nes_cartridge_info {
    uint32_t size, mapper, submapper, header_region, region, selected_bank, battery_flag;
    uint32_t prg_rom_bytes, chr_rom_bytes, prg_ram_bytes, chr_ram_bytes;
} bh_nes_cartridge_info;

BH_NES_API uint32_t bh_nes_api_version(void);
/* Copies the cartridge. audio_enabled must be 0 or 1. */
BH_NES_API bh_nes_result bh_nes_create(const uint8_t *rom, size_t rom_size,
                                       int audio_enabled, bh_nes **out);
BH_NES_API bh_nes_result bh_nes_create_region(const uint8_t *rom, size_t rom_size,
                                              int audio_enabled, bh_nes_region region, bh_nes **out);
/* header_region: 0=NTSC, 1=PAL, 2=multi; region is the selected model. */
BH_NES_API bh_nes_result bh_nes_get_cartridge_info(const bh_nes *machine, bh_nes_cartridge_info *info);
BH_NES_API void bh_nes_destroy(bh_nes *machine);
BH_NES_API bh_nes_result bh_nes_run(bh_nes *machine, uint32_t max_steps, uint32_t *executed);
BH_NES_API bh_nes_result bh_nes_get_state(const bh_nes *machine, bh_nes_state *state);
/* Ports are 0 and 1; buttons are the bitwise OR of bh_nes_buttons. */
BH_NES_API bh_nes_result bh_nes_set_controller(bh_nes *machine, uint32_t port, uint8_t buttons);
/* Inspects RAM/mirrors and PRG ROM without side effects. */
BH_NES_API bh_nes_result bh_nes_read(const bh_nes *machine, uint16_t address, uint8_t *out, size_t size);
/* Side-effect-free PPU address space ($0000-$3FFF) and 256-byte OAM snapshots. */
BH_NES_API bh_nes_result bh_nes_read_ppu(const bh_nes *machine, uint16_t address, uint8_t *out, size_t size);
BH_NES_API bh_nes_result bh_nes_read_oam(const bh_nes *machine, uint16_t address, uint8_t *out, size_t size);
/* Copies the most recently completed packed RGB frame. */
BH_NES_API bh_nes_result bh_nes_frame(const bh_nes *machine, uint8_t *out, size_t capacity);
/* PCM is signed 16-bit, mono, 44,100 Hz. NULL/0 queries pending samples. */
BH_NES_API bh_nes_result bh_nes_audio_read(bh_nes *machine, int16_t *out, size_t capacity, size_t *count);
BH_NES_API const char *bh_nes_result_string(bh_nes_result result);
#ifdef __cplusplus
}
#endif
#endif
