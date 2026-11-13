#include "boothush/nes.h"
#include "nes.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>

struct bh_nes { nes *core; };

uint32_t bh_nes_api_version(void) { return BH_NES_API_VERSION; }
bh_nes_result bh_nes_create(const uint8_t *rom, size_t size, int audio, bh_nes **out) {
    return bh_nes_create_region(rom,size,audio,BH_NES_REGION_AUTO,out);
}
bh_nes_result bh_nes_create_region(const uint8_t *rom, size_t size, int audio, bh_nes_region region, bh_nes **out) {
    if (!out) return BH_NES_INVALID_ARGUMENT;
    *out = nullptr;
    if (!rom || !size || (audio != 0 && audio != 1) || region < BH_NES_REGION_AUTO || region > BH_NES_REGION_PAL) return BH_NES_INVALID_ARGUMENT;
    const char *error = nullptr;
    nes *core = nes_create_region(rom, size, unsigned(region), &error);
    if (!core) return error && !std::strcmp(error, "cannot allocate NES machine or cartridge")
        ? BH_NES_OUT_OF_MEMORY : BH_NES_INVALID_CARTRIDGE;
    if (audio && !nes_capture_audio(core)) { nes_destroy(core); return BH_NES_OUT_OF_MEMORY; }
    bh_nes *machine = new(std::nothrow) bh_nes{core};
    if (!machine) { nes_destroy(core); return BH_NES_OUT_OF_MEMORY; }
    *out = machine;
    return BH_NES_OK;
}
bh_nes_result bh_nes_get_cartridge_info(const bh_nes *m, bh_nes_cartridge_info *out) {
    if (!m || !out || out->size < sizeof(*out)) return BH_NES_INVALID_ARGUMENT;
    const nes_cartridge_info c=nes_get_cartridge_info(m->core);
    *out={sizeof(*out),c.mapper,c.submapper,c.header_region,c.pal?BH_NES_REGION_PAL:BH_NES_REGION_NTSC,c.selected_bank,c.battery_flag,
          uint32_t(c.prg_rom),uint32_t(c.chr_rom),uint32_t(c.prg_ram),uint32_t(c.chr_ram)};
    return BH_NES_OK;
}
void bh_nes_destroy(bh_nes *m) { if (m) { nes_destroy(m->core); delete m; } }
bh_nes_result bh_nes_run(bh_nes *m, uint32_t max_steps, uint32_t *executed) {
    if (executed) *executed = 0;
    if (!m || !executed || !max_steps || max_steps > BH_NES_MAX_STEPS) return BH_NES_INVALID_ARGUMENT;
    for (; *executed < max_steps; ++*executed) if (!nes_step(m->core)) return BH_NES_CPU_FAULT;
    return BH_NES_OK;
}
bh_nes_result bh_nes_get_state(const bh_nes *m, bh_nes_state *out) {
    if (!m || !out || out->size < sizeof(*out)) return BH_NES_INVALID_ARGUMENT;
    const nes_state s = nes_get_state(m->core);
    bh_nes_state result{};
    result.size = sizeof(result); result.pc=s.pc; result.fault_address=s.fault_address;
    result.a=s.a; result.x=s.x; result.y=s.y; result.sp=s.sp; result.flags=s.flags;
    result.fault_opcode=s.fault_opcode; result.dmc_used=s.dmc_used; result.audio_truncated=s.audio_truncated;
    result.steps=s.steps; result.cpu_cycles=s.cpu_cycles; result.ppu_frames=s.ppu_frames;
    result.apu_writes=s.apu_writes; result.dma_transfers=s.dma_transfers;
    if (s.fault) { std::strncpy(result.fault, s.fault, sizeof(result.fault)-1); }
    *out=result;
    return BH_NES_OK;
}
bh_nes_result bh_nes_set_controller(bh_nes *m, uint32_t port, uint8_t buttons) {
    if (!m || port>1) return BH_NES_INVALID_ARGUMENT;
    nes_set_controller(m->core, port, buttons); return BH_NES_OK;
}
bh_nes_result bh_nes_read(const bh_nes *m, uint16_t address, uint8_t *out, size_t size) {
    if (!m || (!out && size) || size > 0x10000u-address) return BH_NES_INVALID_ARGUMENT;
    if (!size) return BH_NES_OK;
    std::array<uint8_t,65536> data;
    if (!nes_inspect(m->core, address, data.data(), size)) return BH_NES_UNMAPPED;
    std::memcpy(out, data.data(), size); return BH_NES_OK;
}
bh_nes_result bh_nes_read_ppu(const bh_nes *m,uint16_t address,uint8_t *out,size_t size) {
    if (!m || (!out && size) || address>=0x4000 || size>0x4000u-address) return BH_NES_INVALID_ARGUMENT;
    return nes_inspect_ppu(m->core,address,out,size)?BH_NES_OK:BH_NES_INVALID_ARGUMENT;
}
bh_nes_result bh_nes_read_oam(const bh_nes *m,uint16_t address,uint8_t *out,size_t size) {
    if (!m || (!out && size) || address>=256 || size>256u-address) return BH_NES_INVALID_ARGUMENT;
    return nes_inspect_oam(m->core,address,out,size)?BH_NES_OK:BH_NES_INVALID_ARGUMENT;
}
bh_nes_result bh_nes_frame(const bh_nes *m, uint8_t *out, size_t capacity) {
    if (!m || !out) return BH_NES_INVALID_ARGUMENT;
    if (capacity < BH_NES_FRAME_BYTES) return BH_NES_BUFFER_TOO_SMALL;
    const uint8_t *frame = nes_frame(m->core);
    if (!frame) return BH_NES_NO_FRAME;
    std::memcpy(out, frame, BH_NES_FRAME_BYTES); return BH_NES_OK;
}
bh_nes_result bh_nes_audio_read(bh_nes *m, int16_t *out, size_t capacity, size_t *count) {
    if (count) *count=0;
    if (!m || !count || (!out && capacity)) return BH_NES_INVALID_ARGUMENT;
    size_t available=0; const int16_t *samples=nes_audio(m->core, &available);
    if (!out) { *count=available; return BH_NES_OK; }
    *count=std::min(capacity,available);
    if (*count) { std::memcpy(out,samples,*count*sizeof(*out)); nes_audio_consume(m->core,*count); }
    return BH_NES_OK;
}
const char *bh_nes_result_string(bh_nes_result r) {
    switch (r) {
    case BH_NES_OK: return "ok"; case BH_NES_INVALID_ARGUMENT: return "invalid argument";
    case BH_NES_INVALID_CARTRIDGE: return "unsupported or invalid cartridge";
    case BH_NES_OUT_OF_MEMORY: return "out of memory"; case BH_NES_CPU_FAULT: return "CPU or bus fault";
    case BH_NES_NO_FRAME: return "no completed frame"; case BH_NES_BUFFER_TOO_SMALL: return "buffer too small";
    case BH_NES_UNMAPPED: return "address cannot be inspected"; default: return "unknown result";
    }
}
