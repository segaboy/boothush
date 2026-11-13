#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/nes.cpp"
#include <cassert>

static nes blank() {
    nes m; m.prg.assign(16384,0);m.pc=0x8000;
    m.prg[0x3ffe]=0x00;m.prg[0x3fff]=0x80;
    return m;
}

int main() {
    // The official 6502 opcode table is an independent acceptance list.
    const uint8_t official[]={
        0x00,0x01,0x05,0x06,0x08,0x09,0x0a,0x0d,0x0e,0x10,0x11,0x15,0x16,0x18,0x19,0x1d,0x1e,
        0x20,0x21,0x24,0x25,0x26,0x28,0x29,0x2a,0x2c,0x2d,0x2e,0x30,0x31,0x35,0x36,0x38,0x39,0x3d,0x3e,
        0x40,0x41,0x45,0x46,0x48,0x49,0x4a,0x4c,0x4d,0x4e,0x50,0x51,0x55,0x56,0x58,0x59,0x5d,0x5e,
        0x60,0x61,0x65,0x66,0x68,0x69,0x6a,0x6c,0x6d,0x6e,0x70,0x71,0x75,0x76,0x78,0x79,0x7d,0x7e,
        0x81,0x84,0x85,0x86,0x88,0x8a,0x8c,0x8d,0x8e,0x90,0x91,0x94,0x95,0x96,0x98,0x99,0x9a,0x9d,
        0xa0,0xa1,0xa2,0xa4,0xa5,0xa6,0xa8,0xa9,0xaa,0xac,0xad,0xae,0xb0,0xb1,0xb4,0xb5,0xb6,0xb8,0xb9,0xba,0xbc,0xbd,0xbe,
        0xc0,0xc1,0xc4,0xc5,0xc6,0xc8,0xc9,0xca,0xcc,0xcd,0xce,0xd0,0xd1,0xd5,0xd6,0xd8,0xd9,0xdd,0xde,
        0xe0,0xe1,0xe4,0xe5,0xe6,0xe8,0xe9,0xea,0xec,0xed,0xee,0xf0,0xf1,0xf5,0xf6,0xf8,0xf9,0xfd,0xfe};
    static_assert(sizeof(official)==151);
    bool legal[256]{};for(auto op:official) legal[op]=true;
    for(unsigned op=0;op<256;op++) {
        nes m=blank();m.prg[0]=uint8_t(op);m.prg[1]=0x10;m.prg[2]=0;
        m.ram[0x10]=0x40;m.ram[0x11]=0;m.ram[0x40]=0x81;
        if(legal[op]) {assert(cycles[op]!=0);assert(m.step());assert(m.steps==1 && m.cpu_cycles>=2);}
        else {assert(!m.step());assert(m.fault && std::strcmp(m.fault,"unsupported_opcode")==0);}
    }
    // Exhaustively compare all binary ADC and SBC inputs against signed and
    // unsigned arithmetic, including the NES's ignored decimal flag.
    for(unsigned a=0;a<256;a++) for(unsigned b=0;b<256;b++) for(unsigned carry=0;carry<2;carry++) {
        for(unsigned subtract=0;subtract<2;subtract++) {
            nes m;m.a=uint8_t(a);m.p=uint8_t(0x28|carry);
            m.adc(uint8_t(subtract?~b:b));
            const int unsigned_result=subtract?int(a)-int(b)-int(1-carry):int(a)+int(b)+int(carry);
            const int sa=a<128?int(a):int(a)-256, sb=b<128?int(b):int(b)-256;
            const int signed_result=subtract?sa-sb-int(1-carry):sa+sb+int(carry);
            const uint8_t val=uint8_t(unsigned_result);
            const bool expected_c=subtract?unsigned_result>=0:unsigned_result>255;
            assert(m.a==val && bool(m.p&C)==expected_c && bool(m.p&Z)==(val==0));
            assert(bool(m.p&N)==bool(val&0x80) && bool(m.p&V)==(signed_result < -128 || signed_result > 127));
            assert(m.p&D);
        }
    }
    // Exercise every accumulator-group addressing mode with independently
    // arranged pointers and a known target, for both reads and stores.
    for(unsigned mode=0;mode<8;mode++) {
        for(unsigned store=0;store<2;store++) {
            if(store && mode==2) continue;
            nes m=blank();m.x=2;m.y=2;m.a=0xa5;
            m.prg[0]=uint8_t((store?0x80:0xa0)|(mode<<2)|1);
            m.prg[1]=mode==2?0xa5:((mode==1||mode==3)?0x30:(mode==5||mode>=6)?0x2e:0x10);
            m.prg[2]=0;m.ram[0x30]=0xa5;
            if(mode==0) {m.ram[0x12]=0x30;m.ram[0x13]=0;}
            if(mode==4) {m.ram[0x10]=0x2e;m.ram[0x11]=0;}
            if(store) m.ram[0x30]=0;
            assert(m.step());
            assert((store?m.ram[0x30]:m.a)==0xa5);
        }
    }
    // RMW operations set carry from the shifted-out bit and update RAM.
    {nes m=blank();m.prg[0]=0x06;m.prg[1]=0x20;m.prg[2]=0x66;m.prg[3]=0x20;
     m.ram[0x20]=0x81;assert(m.step() && m.ram[0x20]==2 && (m.p&C));
     assert(m.step() && m.ram[0x20]==0x81 && !(m.p&C) && (m.p&N));}
    // Zero-page indexed accesses wrap, and BIT copies N/V from memory.
    {nes m=blank();m.prg[0]=0xb5;m.prg[1]=0xff;m.prg[2]=0x24;m.prg[3]=0x02;
     m.x=2;m.ram[1]=0x0f;m.ram[2]=0xc0;
     assert(m.step() && m.a==0x0f);assert(m.step() && (m.p&(N|V|Z))==(N|V|Z));}
    // Indexed-indirect zero-page pointer wraps from FF to 00.
    {nes m=blank();m.prg[0]=0xa1;m.prg[1]=0xfe;m.x=1;
     m.ram[0xff]=0x34;m.ram[0]=0x01;m.ram[0x134]=0x5a;
     assert(m.step() && m.a==0x5a && m.pc==0x8002);}
    // JMP indirect reproduces the 6502 page-wrap behavior.
    {nes m=blank();m.prg[0]=0x6c;m.prg[1]=0xff;m.prg[2]=0x01;
     m.ram[0x1ff]=0x78;m.ram[0x100]=0x56;m.ram[0x200]=0x99;
     assert(m.step() && m.pc==0x5678);}
    // JSR/RTS preserve the return address on the page-one stack.
    {nes m=blank();m.prg[0]=0x20;m.prg[1]=0x10;m.prg[2]=0x80;m.prg[0x10]=0x60;
     assert(m.step() && m.pc==0x8010 && m.sp==0xfb && m.ram[0x1fd]==0x80 && m.ram[0x1fc]==0x02);
     assert(m.step() && m.pc==0x8003 && m.sp==0xfd);}
    // BRK enters IRQ vector and RTI restores PC and architectural flags.
    {nes m=blank();m.prg[0]=0;m.prg[0x3ffe]=0x10;m.prg[0x3fff]=0x80;m.prg[0x10]=0x40;m.p=0x21;
     assert(m.step() && m.pc==0x8010 && m.sp==0xfa && m.ram[0x1fb]==0x31);
     assert(m.step() && m.pc==0x8002 && m.sp==0xfd && m.p==0x21);}
    // Taken branches and indexed reads account for page crossings.
    {nes m=blank();m.pc=0x80fd;m.prg[0xfd]=0xd0;m.prg[0xfe]=1;
     assert(m.step() && m.pc==0x8100 && m.cpu_cycles==4);}
    {nes m=blank();m.prg[0]=0xbd;m.prg[1]=0xfe;m.prg[2]=0;m.x=3;m.ram[0x101]=0x42;
     assert(m.step() && m.a==0x42 && m.cpu_cycles==5);}
    // NROM vertical and horizontal nametable mappings, plus palette aliases.
    {NesPpu v;v.cartridge(nullptr,0,true);v.memory_write(0x2000,0x12);v.memory_write(0x2400,0x34);
     assert(v.memory_read(0x2800)==0x12 && v.memory_read(0x2c00)==0x34 && v.memory_read(0x3000)==0x12);
     v.memory_write(0x3f10,0xff);assert(v.memory_read(0x3f00)==0x3f && v.memory_read(0x3f30)==0x3f);
     NesPpu h;h.cartridge(nullptr,0,false);h.memory_write(0x2000,0x56);h.memory_write(0x2800,0x78);
     assert(h.memory_read(0x2400)==0x56 && h.memory_read(0x2c00)==0x78);}
    // PPUDATA buffering and automatic increment, with CHR RAM and ROM.
    {NesPpu q;q.cartridge(nullptr,0,true);q.write(6,0x20);q.write(6,0x00);q.write(7,0x42);q.write(7,0x43);
     q.write(6,0x20);q.write(6,0x00);assert(q.read(7)==0 && q.read(7)==0x42 && q.read(7)==0x43);
     q.memory_write(0x100,0x55);assert(q.memory_read(0x100)==0x55);
     std::array<uint8_t,8192> chr{};chr[0x100]=0x66;q.cartridge(chr.data(),chr.size(),true);
     q.memory_write(0x100,0x77);assert(q.memory_read(0x100)==0x66);}
    // PPU vblank raises one NMI edge; status reads clear vblank.
    {NesPpu q;q.cartridge(nullptr,0,true);q.write(0,0x80);q.advance(241*341+1);
     assert(q.take_nmi() && !q.take_nmi());assert(q.read(2)&0x80);assert(!(q.read(2)&0x80));}
    {NesPpu q;q.cartridge(nullptr,0,true);q.write(1,8);q.advance(262*341);assert(q.frame_count()==1);
     q.advance(262*341-2);assert(q.frame_count()==1);q.advance(1);assert(q.frame_count()==2);}
    {NesPpu q;q.cartridge(nullptr,0,true);q.advance(2*262*341-1);assert(q.frame_count()==1);
     q.advance(1);assert(q.frame_count()==2);}
    // A generated pattern and palette produce a known framebuffer pixel.
    {NesPpu q;q.cartridge(nullptr,0,true);q.memory_write(0x2000,1);q.memory_write(0x10,0x80);
     q.memory_write(0x3f01,0x21);q.write(1,0x0a);q.advance(240*341+1);
     const uint8_t *frame=q.frame();assert(frame && frame[0]==0x64 && frame[1]==0xb0 && frame[2]==0xff);
     assert(frame[3]==0x66 && frame[4]==0x66 && frame[5]==0x66);}
    // Opaque sprite 0 overlapping opaque background raises the hit flag.
    {NesPpu q;q.cartridge(nullptr,0,true);q.memory_write(0x2000,1);q.memory_write(0x10,0x80);q.memory_write(0x11,0x80);
     q.write(3,0);q.write(4,0);q.write(4,1);q.write(4,0);q.write(4,0);q.write(1,0x1e);
     q.advance(341+1);assert(q.read(2)&0x40);}
    {nes m=blank();m.prg[0x3ffa]=0x10;m.prg[0x3ffb]=0x80;m.prg[0x10]=0xe8;
     m.ppu.write(0,0x80);m.ppu.advance(241*341+1);
     assert(m.step() && m.pc==0x8011 && m.x==1 && m.sp==0xfa && m.cpu_cycles==9);}
    // OAM DMA copies an entire RAM page and accounts for the CPU stall.
    {nes m=blank();m.prg[0]=0xa9;m.prg[1]=2;m.prg[2]=0x8d;m.prg[3]=0x14;m.prg[4]=0x40;
     for(unsigned i=0;i<256;i++) m.ram[0x200+i]=uint8_t(i);
     assert(m.step() && m.step() && m.dma_transfers==1 && m.cpu_cycles==519);
     m.ppu.write(3,0);assert(m.ppu.read(4)==0);m.ppu.write(3,0xff);assert(m.ppu.read(4)==0xff);}
    {nes m=blank();m.prg[0]=0xa9;m.prg[1]=0x40;m.prg[2]=0x8d;m.prg[3]=0x14;m.prg[4]=0x40;
     assert(m.step() && !m.step() && m.dma_transfers==0 && m.fault_address==0x4000);}
    // Standard serial controller order: A, B, Select, Start, Up, Down, Left, Right.
    {nes m=blank();m.controller_live[0]=0x89;m.write(0x4016,1);
     assert((m.read(0x4016)&1)==1 && (m.read(0x4016)&1)==1);m.write(0x4016,0);
     const bool expected[]={1,0,0,1,0,0,0,1,1,1};
     for(bool bit:expected) assert(bool(m.read(0x4016)&1)==bit);
     assert((m.read(0x4017)&1)==0);}
    // A pulse with a constant envelope generates non-silent PCM; disabling
    // its length counter eventually settles the output filter back to zero.
    {NesApu a;assert(a.capture());a.write(0x4017,0x40);a.write(0x4015,1);
     a.write(0x4000,0xbf);a.write(0x4002,0xfd);a.write(0x4003,0x08);
     a.advance(20000);assert(a.read_status()&1);
     bool nonzero=false;for(int16_t sample:a.samples()) if(sample>100 || sample< -100)nonzero=true;assert(nonzero);
     a.write(0x4015,0);assert(!(a.read_status()&1));a.advance(20000);
     assert(!a.truncated() && !a.samples().empty());}
    // NTSC pulse timer 253 should produce approximately 440 Hz, independent
    // of the internal duty-sequencer phase and filter startup transient.
    {NesApu a;assert(a.capture());a.write(0x4017,0x40);a.write(0x4015,1);
     a.write(0x4000,0xbf);a.write(0x4002,0xfd);a.write(0x4003,0x08);a.advance(1789773);
     const auto &s=a.samples();assert(s.size()==44100);unsigned crossings=0;
     for(size_t i=1;i<s.size();i++) if(s[i-1]<=0 && s[i]>0)++crossings;
     assert(crossings>=438 && crossings<=442);}
    // Four-step frame IRQ is visible and cleared by the status read.
    {NesApu a;a.advance(29828);assert(!a.irq());a.advance(1);assert(a.irq());assert(a.read_status()&0x40);assert(!a.irq());
     a.write(0x4017,0x40);a.advance(20000);assert(!a.irq());}
    {nes m=blank();m.prg[0x3ffe]=0x10;m.prg[0x3fff]=0x80;m.prg[0x10]=0xe8;m.p&=uint8_t(~I);
     m.apu.advance(29829);assert(m.step() && m.pc==0x8011 && m.x==1 && m.cpu_cycles==9);}
    // Region clocks use CPU cycles: PAL's 16:5 PPU ratio and 312 scanlines.
    {NesApu a;a.set_pal(true);a.advance(33252);assert(!a.irq());a.advance(1);assert(a.irq());}
    {NesApu a;a.set_pal(true);assert(a.capture());a.advance(1662607);assert(a.samples().size()==44100);}
    {nes m=blank();m.pal=true;m.ppu.set_pal(true);m.advance(33247);assert(m.ppu.frame_count()==0);
     m.advance(1);assert(m.ppu.frame_count()==1);}
    {NesPpu p;p.set_pal(true);p.write(1,0x18);p.advance(341*312*2);assert(p.frame_count()==2);}
    // DMC fetch, shift order, saturation behavior, and persistent IRQ flag.
    {NesApu a;a.write(0x4017,0x40);a.write(0x4010,0x8f);a.write(0x4011,0x40);
     a.write(0x4012,0);a.write(0x4013,0);a.write(0x4015,0x10);
     assert(a.dmc_dma_needed() && a.dmc_dma_address()==0xc000);a.dmc_dma_supply(0xff);
     assert(a.dmc_was_used() && a.irq() && (a.read_status()&0x80) && a.irq());
     a.advance(54*16);assert(a.dmc_output()==0x50);a.write(0x4015,0);assert(!a.irq());}
    // The memory reader wraps FFFF to 8000 and loops at the configured length.
    {NesApu a;a.write(0x4017,0x40);a.write(0x4010,0x4f);a.write(0x4012,0xff);a.write(0x4013,4);a.write(0x4015,0x10);
     for(unsigned i=0;i<64;i++){assert(a.dmc_dma_needed());a.dmc_dma_supply(0);a.advance(54*8);}
     assert(a.dmc_dma_needed() && a.dmc_dma_address()==0x8000);a.dmc_dma_supply(0);
     a.advance(54*8);assert(a.dmc_dma_needed() && a.dmc_dma_address()==0xffc0);}
    // The CPU model services DMC fetches at a boundary and charges four cycles.
    {nes m=blank();m.prg[0]=0xea;m.apu.write(0x4017,0x40);m.apu.write(0x4015,0x10);
     assert(m.step() && m.cpu_cycles==6 && m.apu.dmc_was_used());}
    return 0;
}
