#include <boothush/nes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    uint8_t *rom=calloc(1,16+16384+8192), *frame=malloc(BH_NES_FRAME_BYTES);
    bh_nes *a=NULL, *b=NULL; uint32_t executed=99; size_t pending=0, read=0;
    int16_t samples[1024]; uint8_t value=0;
    bh_nes_state state={0};
    CHECK(rom && frame && bh_nes_api_version()==BH_NES_API_VERSION);
    memcpy(rom,"NES\x1a",4); rom[4]=1; rom[5]=1;
    /* Strobe and read controller 1 repeatedly, storing its first bit in RAM. */
    { const uint8_t program[]={0xa9,1,0x8d,0x16,0x40,0xa9,0,0x8d,0x16,0x40,0xad,0x16,0x40,0x85,0,0x4c,0,0x80};
      memcpy(rom+16,program,sizeof(program)); }
    rom[16+0x3ffc]=0; rom[16+0x3ffd]=0x80;
    CHECK(bh_nes_create(rom,3,0,&a)==BH_NES_INVALID_CARTRIDGE && !a);
    CHECK(bh_nes_create(rom,16+16384+8192,1,&a)==BH_NES_OK);
    CHECK(bh_nes_create(rom,16+16384+8192,0,&b)==BH_NES_OK);
    { bh_nes *p=NULL; bh_nes_cartridge_info info={0}; info.size=sizeof(info);
      CHECK(bh_nes_create_region(rom,16+16384+8192,0,BH_NES_REGION_PAL,&p)==BH_NES_OK);
      CHECK(bh_nes_get_cartridge_info(p,&info)==BH_NES_OK && info.mapper==0 && info.region==BH_NES_REGION_PAL && info.header_region==0);
      bh_nes_destroy(p); }
    memset(rom,0,16+16384+8192); free(rom); /* Both instances own their cartridge. */
    CHECK(bh_nes_frame(a,frame,BH_NES_FRAME_BYTES)==BH_NES_NO_FRAME);
    CHECK(bh_nes_set_controller(a,0,BH_NES_A)==BH_NES_OK);
    CHECK(bh_nes_set_controller(a,2,0)==BH_NES_INVALID_ARGUMENT);
    CHECK(bh_nes_read_ppu(a,0,&value,1)==BH_NES_OK && value==0);
    CHECK(bh_nes_read_oam(a,0,&value,1)==BH_NES_OK && value==0);
    CHECK(bh_nes_read_ppu(a,0x3fff,&value,2)==BH_NES_INVALID_ARGUMENT);
    CHECK(bh_nes_read_oam(a,255,&value,2)==BH_NES_INVALID_ARGUMENT);
    CHECK(bh_nes_run(a,0,&executed)==BH_NES_INVALID_ARGUMENT && executed==0);
    CHECK(bh_nes_run(a,50000,&executed)==BH_NES_OK && executed==50000);
    state.size=sizeof(state); CHECK(bh_nes_get_state(a,&state)==BH_NES_OK);
    CHECK(state.steps==50000 && state.ppu_frames>0 && !state.fault[0]);
    CHECK(bh_nes_read(a,0,&value,1)==BH_NES_OK && (value&1));
    { uint8_t pair[2]={42,43}; CHECK(bh_nes_read(a,0x1fff,pair,2)==BH_NES_UNMAPPED && pair[0]==42 && pair[1]==43); }
    CHECK(bh_nes_read(a,0xffff,&value,2)==BH_NES_INVALID_ARGUMENT);
    frame[0]=123; CHECK(bh_nes_frame(a,frame,1)==BH_NES_BUFFER_TOO_SMALL && frame[0]==123);
    CHECK(bh_nes_frame(a,frame,BH_NES_FRAME_BYTES)==BH_NES_OK);
    CHECK(bh_nes_audio_read(a,NULL,0,&pending)==BH_NES_OK && pending>0);
    CHECK(bh_nes_audio_read(a,samples,1024,&read)==BH_NES_OK && read>0 && read<=pending);
    { size_t left=0; CHECK(bh_nes_audio_read(a,NULL,0,&left)==BH_NES_OK && left==pending-read); }
    state.size=sizeof(state); CHECK(bh_nes_get_state(b,&state)==BH_NES_OK && state.steps==0);
    CHECK(bh_nes_run(b,50,&executed)==BH_NES_OK);
    CHECK(bh_nes_read(b,0,&value,1)==BH_NES_OK && !(value&1));
    CHECK(bh_nes_audio_read(b,NULL,0,&pending)==BH_NES_OK && pending==0);
    bh_nes_destroy(a); bh_nes_destroy(b); bh_nes_destroy(NULL); free(frame);
    /* Unsupported opcodes fault without counting an instruction and remain sticky. */
    rom=calloc(1,16+16384+8192); CHECK(rom);
    memcpy(rom,"NES\x1a",4); rom[4]=1; rom[5]=1; rom[16]=0x02;
    rom[16+0x3ffc]=0; rom[16+0x3ffd]=0x80;
    CHECK(bh_nes_create(rom,16+16384+8192,0,&a)==BH_NES_OK); free(rom);
    CHECK(bh_nes_run(a,10,&executed)==BH_NES_CPU_FAULT && executed==0);
    state.size=sizeof(state); CHECK(bh_nes_get_state(a,&state)==BH_NES_OK);
    CHECK(state.steps==0 && state.fault_opcode==2 && state.fault[0]);
    CHECK(bh_nes_run(a,10,&executed)==BH_NES_CPU_FAULT && executed==0);
    bh_nes_destroy(a);
    puts("NES public C API passed"); return 0;
}
