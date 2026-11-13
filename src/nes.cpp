#include "nes.h"
#include <array>
#include <cstring>
#include <new>
#include <vector>

namespace {
constexpr uint8_t C=1, Z=2, I=4, D=8, V=64, N=128;
}
struct nes {
    std::vector<uint8_t> prg;
    std::array<uint8_t,2048> ram{};
    uint16_t pc=0, fault_address=0, instruction_pc=0;
    uint8_t a=0,x=0,y=0,sp=0xfd,p=0x24, opcode=0;
    uint64_t steps=0;
    const char *fault=nullptr;
    bool peek(uint16_t addr, uint8_t &v) const {
        if (addr<0x2000) { v=ram[addr&0x7ff]; return true; }
        if (addr>=0x8000) { v=prg[(addr-0x8000)%prg.size()]; return true; }
        return false;
    }
    uint8_t read(uint16_t addr) {
        uint8_t v=0;
        if (fault) return v;
        if (!peek(addr,v) && !fault) { fault="unsupported_bus_read"; fault_address=addr; }
        return v;
    }
    void write(uint16_t addr,uint8_t v) {
        if (fault) return;
        if (addr<0x2000) ram[addr&0x7ff]=v;
        else if(!fault) {fault="unsupported_bus_write";fault_address=addr;}
    }
    uint8_t byte() {uint8_t v=read(pc); pc=uint16_t(pc+1); return v;}
    uint16_t word() {uint8_t lo=byte(); uint8_t hi=byte(); return uint16_t(lo | uint16_t(hi)<<8);}
    void nz(uint8_t v) {p=uint8_t((p&~(N|Z))|(v?0:Z)|(v&N));}
    void compare(uint8_t lhs,uint8_t rhs) {p=uint8_t((p&~C)|(lhs>=rhs?C:0));nz(uint8_t(lhs-rhs));}
    void adc(uint8_t v) {
        const uint8_t old=a; const unsigned sum=unsigned(a)+v+(p&C?1:0); a=uint8_t(sum);
        p=uint8_t((p&~(C|V))|(sum>255?C:0)|((~(old^v)&(old^a)&0x80)?V:0)); nz(a);
    }
    void branch(bool take) {const int8_t off=int8_t(byte()); if(take) pc=uint16_t(pc+off);}
    bool step() {
        if(fault) return false;
        const uint8_t old_a=a,old_x=x,old_y=y,old_p=p;
        instruction_pc=pc; opcode=byte(); if(fault) {pc=instruction_pc;return false;}
        switch(opcode) {
        case 0xa9: a=byte();nz(a);break; case 0xa5: a=read(byte());nz(a);break;
        case 0xad: {uint16_t v=word();a=read(v);nz(a);break;}
        case 0xa2: x=byte();nz(x);break; case 0xa0: y=byte();nz(y);break;
        case 0x85: {uint16_t v=byte();write(v,a);break;} case 0x8d: {uint16_t v=word();write(v,a);break;}
        case 0x86: {uint16_t v=byte();write(v,x);break;} case 0x84: {uint16_t v=byte();write(v,y);break;}
        case 0x8e: {uint16_t v=word();write(v,x);break;} case 0x8c: {uint16_t v=word();write(v,y);break;}
        case 0xe8: ++x;nz(x);break; case 0xca: --x;nz(x);break;
        case 0xc8: ++y;nz(y);break; case 0x88: --y;nz(y);break;
        case 0xaa: x=a;nz(x);break; case 0xa8: y=a;nz(y);break;
        case 0x8a: a=x;nz(a);break; case 0x98: a=y;nz(a);break;
        case 0x69: adc(byte());break; case 0xe9: adc(uint8_t(~byte()));break;
        case 0xc9: compare(a,byte());break; case 0xe0: compare(x,byte());break; case 0xc0: compare(y,byte());break;
        case 0x18: p&=uint8_t(~C);break; case 0x38: p|=C;break;
        case 0x58: p&=uint8_t(~I);break; case 0x78: p|=I;break;
        case 0xd8: p&=uint8_t(~D);break; case 0xf8: p|=D;break;
        case 0x4c: pc=word();break;
        case 0xf0: branch((p&Z)!=0);break; case 0xd0: branch((p&Z)==0);break;
        case 0x90: branch((p&C)==0);break; case 0xb0: branch((p&C)!=0);break;
        case 0x30: branch((p&N)!=0);break; case 0x10: branch((p&N)==0);break;
        case 0x50: branch((p&V)==0);break; case 0x70: branch((p&V)!=0);break;
        case 0xea: break;
        default: fault="unsupported_opcode";fault_address=instruction_pc;break;
        }
        if(fault) {pc=instruction_pc;a=old_a;x=old_x;y=old_y;p=old_p;return false;}
        ++steps; return true;
    }
};

nes *nes_create(const uint8_t *rom,size_t size,const char **error) {
    if(!error) return nullptr;
    *error="unsupported or invalid iNES NROM cartridge";
    if(!rom || size<16 || std::memcmp(rom,"NES\x1a",4) || (rom[4]!=1 && rom[4]!=2) ||
       rom[5]>1 || (rom[6]&0xfe) || rom[7]!=0) return nullptr;
    for(unsigned i=8;i<16;i++) if(rom[i]) return nullptr;
    const size_t prg_size=size_t(rom[4])*16384, chr_size=size_t(rom[5])*8192;
    if(size!=16+prg_size+chr_size) return nullptr;
    auto *m=new(std::nothrow) nes; if(!m){*error="cannot allocate NES machine";return nullptr;}
    try {m->prg.assign(rom+16,rom+16+prg_size);} catch(...) {delete m;*error="cannot allocate NES cartridge";return nullptr;}
    uint8_t lo=0,hi=0; m->peek(0xfffc,lo);m->peek(0xfffd,hi); m->pc=uint16_t(lo|uint16_t(hi)<<8);
    *error=nullptr;return m;
}
void nes_destroy(nes *m) {delete m;}
bool nes_step(nes *m) {return m->step();}
nes_state nes_get_state(const nes *m) {return {m->pc,m->fault_address,m->a,m->x,m->y,m->sp,m->p,m->opcode,m->steps,m->fault};}
bool nes_inspect(const nes *m,uint16_t address,uint8_t *out,size_t size) {
    if(size>0x10000u-address || (!out&&size)) return false;
    for(size_t i=0;i<size;i++) if(!m->peek(uint16_t(address+i),out[i])) return false;
    return true;
}
