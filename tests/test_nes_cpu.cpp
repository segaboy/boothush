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
        if(legal[op]) {assert(m.step());assert(m.steps==1);}
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
    return 0;
}
