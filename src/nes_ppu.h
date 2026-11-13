#ifndef BOOTHUSH_NES_PPU_H
#define BOOTHUSH_NES_PPU_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <vector>

// Approximate dot renderer and instruction-boundary CPU/PPU register model.
class NesPpu {
public:
    NesPpu() : working(256*240*3), completed(256*240*3) {}
    void cartridge(const uint8_t *data, size_t size, bool vertical) {
        chr.fill(0);chr_writable=size==0;vertical_mirroring=vertical;
        if(size) std::copy_n(data,8192,chr.begin());
    }
    uint8_t memory_read(uint16_t a) const {
        a &= 0x3fff;
        if(a<0x2000) return chr[a];
        if(a<0x3f00) {
            unsigned off=(a-0x2000)&0x0fff, table=off>>10;
            unsigned physical=vertical_mirroring?(table&1):(table>>1);
            return nametable[physical*1024+(off&1023)];
        }
        unsigned idx=a&31;if((idx&0x13)==0x10) idx-=16;
        return palette[idx];
    }
    void memory_write(uint16_t a, uint8_t value) {
        a &= 0x3fff;
        if(a<0x2000) {if(chr_writable) chr[a]=value;return;}
        if(a<0x3f00) {
            unsigned off=(a-0x2000)&0x0fff, table=off>>10;
            unsigned physical=vertical_mirroring?(table&1):(table>>1);
            nametable[physical*1024+(off&1023)]=value;return;
        }
        unsigned idx=a&31;if((idx&0x13)==0x10) idx-=16;
        palette[idx]=value&0x3f;
    }
    uint8_t read(unsigned r) {
        r&=7;
        if(r==2) {uint8_t v=uint8_t((status&0xe0)|(latch&0x1f));status&=uint8_t(~0x80);write_second=false; update_nmi();latch=v;return v;}
        if(r==4) {latch=oam[oam_addr];return latch;}
        if(r==7) {
            uint16_t addr=vram&0x3fff;uint8_t value=memory_read(addr),out=buffer;
            if(addr>=0x3f00) {out=uint8_t((value&0x3f)|(latch&0xc0));buffer=memory_read(uint16_t(addr-0x1000));}
            else buffer=value;
            vram=uint16_t((vram+((ctrl&4)?32:1))&0x7fff);latch=out;return out;
        }
        return latch;
    }
    void write(unsigned r,uint8_t value) {
        r&=7;latch=value;
        switch(r) {
        case 0: ctrl=value;temp=uint16_t((temp&~0x0c00)|((value&3)<<10));update_nmi();break;
        case 1: mask=value;break;
        case 3: oam_addr=value;break;
        case 4: oam[oam_addr++]=value;break;
        case 5:
            if(!write_second) {fine_x=value&7;temp=uint16_t((temp&~0x001f)|(value>>3));}
            else temp=uint16_t((temp&~0x73e0)|((value&7)<<12)|((value&0xf8)<<2));
            write_second=!write_second;break;
        case 6:
            if(!write_second) temp=uint16_t((temp&0x00ff)|((value&0x3f)<<8));
            else {temp=uint16_t((temp&0x7f00)|value);vram=temp;}
            write_second=!write_second;break;
        case 7: memory_write(vram,value);vram=uint16_t((vram+((ctrl&4)?32:1))&0x7fff);break;
        }
    }
    void advance(uint32_t dots) {
        while(dots--) {
            if(scanline==261 && dot==339 && (frames&1) && (mask&0x18)) {dot=0;scanline=0;++frames;}
            else {++dot;if(dot>=341){dot=0;if(++scanline>=262){scanline=0;++frames;}}}
            if(scanline<240 && dot==1) {line_v=vram;select_sprites();}
            if(scanline<240 && dot>=1 && dot<=256) render_pixel(dot-1,scanline);
            if((mask&0x18) && (scanline<240 || scanline==261)) {
                if(dot==256 && scanline<240) increment_y();
                if(dot==257) vram=uint16_t((vram&~0x041f)|(temp&0x041f));
                if(scanline==261 && dot>=280 && dot<=304) vram=uint16_t((vram&~0x7be0)|(temp&0x7be0));
            }
            if(scanline==240 && dot==1){completed=working;frame_ready=true;}
            if(scanline==241 && dot==1){status|=0x80;update_nmi();}
            if(scanline==261 && dot==1){status&=uint8_t(~0xe0);update_nmi();}
        }
    }
    bool take_nmi() {bool pending=nmi_pending;nmi_pending=false;return pending;}
    uint64_t frame_count() const {return frames;}
    const uint8_t *frame() const {return frame_ready?completed.data():nullptr;}
private:
    void increment_y() {
        if((vram&0x7000)!=0x7000) {vram=uint16_t(vram+0x1000);return;}
        vram&=uint16_t(~0x7000);unsigned row=(vram&0x03e0)>>5;
        if(row==29) {row=0;vram^=0x0800;} else if(row==31) row=0;else ++row;
        vram=uint16_t((vram&~0x03e0)|(row<<5));
    }
    void select_sprites() {
        sprite_count=0;unsigned found=0,height=(ctrl&0x20)?16:8;
        if(!(mask&0x18)) return;
        for(unsigned i=0;i<64;i++) {
            int row=int(scanline)-int(oam[i*4])-1;
            if(row>=0 && row<int(height)) {if(sprite_count<8) sprites[sprite_count++]=uint8_t(i);++found;}
        }
        if(found>8) status|=0x20; // Simplified overflow, not the hardware bug.
    }
    void render_pixel(unsigned px,unsigned py) {
        unsigned bg=0,bg_palette=0,sp=0,sp_palette=0;bool behind=false;unsigned sprite_id=255;
        if((mask&8) && (px>=8 || (mask&2))) {
            unsigned sx=((line_v&0x0400)?256:0)+(line_v&31)*8+fine_x+px;
            unsigned sy=((line_v&0x0800)?240:0)+((line_v>>5)&31)*8+((line_v>>12)&7);
            sx%=512;sy%=480;unsigned nt=(sy/240)*2+(sx/256), xx=sx%256, yy=sy%240;
            unsigned tx=xx/8,ty=yy/8, base=0x2000+nt*1024;
            uint8_t tile=memory_read(uint16_t(base+ty*32+tx));
            unsigned pattern=((ctrl&0x10)?0x1000:0)+unsigned(tile)*16+(yy&7), bit=7-(xx&7);
            bg=((memory_read(uint16_t(pattern))>>bit)&1)|(((memory_read(uint16_t(pattern+8))>>bit)&1)<<1);
            uint8_t attr=memory_read(uint16_t(base+0x3c0+(ty/4)*8+tx/4));
            bg_palette=(attr>>((ty&2)*2+(tx&2)))&3;
        }
        if((mask&0x10) && (px>=8 || (mask&4))) {
            unsigned height=(ctrl&0x20)?16:8;
            for(unsigned k=0;k<sprite_count;k++) {
                unsigned i=sprites[k], sx=oam[i*4+3];if(px<sx || px>=sx+8) continue;
                uint8_t attr=oam[i*4+2],tile=oam[i*4+1];
                unsigned row=unsigned(int(py)-int(oam[i*4])-1), col=px-sx;
                if(attr&0x80) row=height-1-row;
                if(attr&0x40) col=7-col;
                unsigned pattern;
                if(height==16) pattern=(unsigned(tile&1)<<12)+unsigned(tile&0xfe)*16+(row/8)*16+(row&7);
                else pattern=((ctrl&8)?0x1000:0)+unsigned(tile)*16+row;
                unsigned bit=7-col;
                unsigned pix=((memory_read(uint16_t(pattern))>>bit)&1)|(((memory_read(uint16_t(pattern+8))>>bit)&1)<<1);
                if(pix) {sp=pix;sp_palette=attr&3;behind=(attr&0x20)!=0;sprite_id=i;break;}
            }
        }
        if(bg && sp && sprite_id==0 && px<255) status|=0x40;
        uint16_t address=0x3f00;
        if(sp && (!bg || !behind)) address=uint16_t(0x3f10+sp_palette*4+sp);
        else if(bg) address=uint16_t(0x3f00+bg_palette*4+bg);
        uint8_t colour=memory_read(address);if(mask&1) colour&=0x30;
        // Approximate RGB display palette; not a simulation of composite video.
        static constexpr uint32_t rgb[64]={
            0x666666,0x002a88,0x1412a7,0x3b00a4,0x5c007e,0x6e0040,0x6c0600,0x561d00,0x333500,0x0b4800,0x005200,0x004f08,0x00404d,0x000000,0x000000,0x000000,
            0xadadad,0x155fd9,0x4240ff,0x7527fe,0xa01acc,0xb71e7b,0xb53120,0x994e00,0x6b6d00,0x388700,0x0c9300,0x008f32,0x007c8d,0x000000,0x000000,0x000000,
            0xfffeff,0x64b0ff,0x9290ff,0xc676ff,0xf36aff,0xfe6ecc,0xfe8170,0xea9e22,0xbcbe00,0x88d800,0x5ce430,0x45e082,0x48cdde,0x4f4f4f,0x000000,0x000000,
            0xfffeff,0xc0dfff,0xd3d2ff,0xe8c8ff,0xfbc2ff,0xfec4ea,0xfeccc5,0xf7d8a5,0xe4e594,0xcfef96,0xbdf4ab,0xb3f3cc,0xb5ebf2,0xb8b8b8,0x000000,0x000000};
        uint32_t c=rgb[colour&63];size_t offset=(size_t(py)*256+px)*3;
        working[offset]=uint8_t(c>>16);working[offset+1]=uint8_t(c>>8);working[offset+2]=uint8_t(c);
    }
    void update_nmi() {
        bool level=(status&0x80)&&(ctrl&0x80);
        if(level&&!nmi_level) nmi_pending=true;
        nmi_level=level;
    }
    std::array<uint8_t,8192> chr{};
    std::array<uint8_t,2048> nametable{};
    std::array<uint8_t,32> palette{};
    std::array<uint8_t,256> oam{};
    std::vector<uint8_t> working,completed;
    std::array<uint8_t,8> sprites{};
    bool chr_writable=false,vertical_mirroring=false,write_second=false,nmi_level=false,nmi_pending=false;
    uint8_t ctrl=0,mask=0,status=0,latch=0,buffer=0,oam_addr=0,fine_x=0,sprite_count=0;
    bool frame_ready=false;
    uint16_t vram=0,temp=0,line_v=0,dot=0,scanline=0;
    uint64_t frames=0;
};
#endif
