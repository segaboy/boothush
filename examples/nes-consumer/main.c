#include <boothush/nes.h>
#include <stdio.h>
#include <stdlib.h>

/* An external application owns all input/output buffers and schedules execution. */
int main(int argc, char **argv) {
    FILE *file; long size; unsigned char *rom, *frame; bh_nes *nes=NULL;
    bh_nes_result result; bh_nes_state state={0}; uint32_t executed=0;
    if (argc!=2) { fprintf(stderr,"Usage: nes_consumer cartridge.nes\n"); return 2; }
    file=fopen(argv[1],"rb"); if (!file) { perror("cartridge"); return 1; }
    if (fseek(file,0,SEEK_END) || (size=ftell(file))<=0 || size>16+32768+8192 || fseek(file,0,SEEK_SET)) {
        fclose(file); fprintf(stderr,"Cartridge size is unsupported\n"); return 1;
    }
    rom=malloc((size_t)size); frame=malloc(BH_NES_FRAME_BYTES);
    if (!rom || !frame || fread(rom,1,(size_t)size,file)!=(size_t)size) {
        fclose(file); free(rom); free(frame); fprintf(stderr,"Cannot load cartridge\n"); return 1;
    }
    fclose(file); result=bh_nes_create(rom,(size_t)size,0,&nes); free(rom);
    if (result!=BH_NES_OK) { fprintf(stderr,"Create: %s\n",bh_nes_result_string(result)); free(frame); return 1; }
    result=bh_nes_run(nes,400000,&executed);
    state.size=sizeof(state); bh_nes_get_state(nes,&state);
    if (result!=BH_NES_OK) {
        fprintf(stderr,"Run: %s (%s at $%04x)\n",bh_nes_result_string(result),state.fault,state.pc);
        bh_nes_destroy(nes); free(frame); return 1;
    }
    result=bh_nes_frame(nes,frame,BH_NES_FRAME_BYTES);
    if (result!=BH_NES_OK) { fprintf(stderr,"Frame: %s\n",bh_nes_result_string(result)); bh_nes_destroy(nes); free(frame); return 1; }
    printf("Executed %u instructions; %llu frames; first RGB pixel %u,%u,%u\n",executed,
           (unsigned long long)state.ppu_frames,frame[0],frame[1],frame[2]);
    bh_nes_destroy(nes); free(frame); return 0;
}
