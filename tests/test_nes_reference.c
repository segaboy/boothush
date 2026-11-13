/* Bounded smoke comparison with an independent implementation. */
#include <agnes.h>
#include <boothush/nes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only strict NROM is handed to the reference core. Its loader borrows data. */
static int compare_supplied_rom(const char *path) {
    FILE *file=fopen(path,"rb"); long size; unsigned char *rom; agnes_t *ref;
    bh_nes *machine=NULL; uint32_t steps=0; unsigned i,frames=0;
    if (!file || fseek(file,0,SEEK_END) || (size=ftell(file))<16 || size>16+32768+8192 || fseek(file,0,SEEK_SET)) return 10;
    rom=malloc((size_t)size); if (!rom) { fclose(file); return 11; }
    if (fread(rom,1,(size_t)size,file)!=(size_t)size) { fclose(file); free(rom); return 12; }
    fclose(file);
    if (memcmp(rom,"NES\x1a",4) || (rom[4]!=1 && rom[4]!=2) || rom[5]>1 ||
        (rom[6]&0xfe) || (rom[7]!=0 && rom[7]!=8) ||
        (size_t)size!=16u+(size_t)rom[4]*16384u+(size_t)rom[5]*8192u ||
        bh_nes_create(rom,(size_t)size,0,&machine)!=BH_NES_OK) { free(rom); return 13; }
    ref=agnes_make();
    if (!ref || !agnes_load_ines_data(ref,rom,(size_t)size)) return 14;
    for (i=0;i<400000;i++) {
        bool new_frame=false;
        if (!agnes_tick(ref,&new_frame)) return 15;
        if (new_frame) ++frames;
    }
    if (!frames || bh_nes_run(machine,400000,&steps)!=BH_NES_OK || steps!=400000) return 16;
    { bh_nes_state state={0}; state.size=sizeof(state);
      if (bh_nes_get_state(machine,&state)!=BH_NES_OK || !state.ppu_frames) return 17;
      printf("Supplied NROM: reference %u frames; BootHush %llu frames in 400000 instructions each\n",
             frames,(unsigned long long)state.ppu_frames); }
    agnes_destroy(ref); bh_nes_destroy(machine); free(rom); return 0;
}

int main(void) {
    unsigned char *rom=calloc(1,16+16384+8192), *frame=malloc(BH_NES_FRAME_BYTES);
    agnes_t *reference=agnes_make(); bh_nes *machine=NULL; bool new_frame=false;
    uint32_t steps=0; unsigned i;
    if (!rom || !frame || !reference) return 1;
    memcpy(rom,"NES\x1a",4); rom[4]=1; rom[5]=1;
    /* A deterministic, read-only NROM loop and reset vector. */
    rom[16]=0x4c; rom[17]=0; rom[18]=0x80; rom[16+0x3ffc]=0; rom[16+0x3ffd]=0x80;
    if (!agnes_load_ines_data(reference,rom,16+16384+8192) ||
        bh_nes_create(rom,16+16384+8192,0,&machine)!=BH_NES_OK) return 2;
    for (i=0;i<100000 && !new_frame;i++) if (!agnes_tick(reference,&new_frame)) return 3;
    if (!new_frame || bh_nes_run(machine,100000,&steps)!=BH_NES_OK || steps!=100000 ||
        bh_nes_frame(machine,frame,BH_NES_FRAME_BYTES)!=BH_NES_OK) return 4;
    /* Both framebuffers should be spatially uniform for this disabled-PPU fixture.
       Exact RGB values need not match because the cores use different palettes. */
    { agnes_color_t p=agnes_get_screen_pixel(reference,0,0);
      agnes_color_t q=agnes_get_screen_pixel(reference,128,120);
      if (p.r!=q.r || p.g!=q.g || p.b!=q.b ||
          memcmp(frame,frame+3*(120*256+128),3)!=0) return 5; }
    bh_nes_destroy(machine); agnes_destroy(reference); free(frame); free(rom);
    puts("Agnès and BootHush both completed the bounded NROM fixture");
    { const char *path=getenv("BOOTHUSH_NES_ROM"); if (path && *path) return compare_supplied_rom(path); }
    return 0;
}
