#include "nes.h"
#include "nes_ppu.h"
#include "nes_apu.h"
#include <array>
#include <cstring>
#include <new>
#include <vector>

namespace {
constexpr uint8_t C=1, Z=2, I=4, D=8, V=64, N=128;
constexpr uint8_t cycles[256]={
 7,6,0,0,0,3,5,0,3,2,2,0,0,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
 6,6,0,0,3,3,5,0,4,2,2,0,4,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
 6,6,0,0,0,3,5,0,3,2,2,0,3,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
 6,6,0,0,0,3,5,0,4,2,2,0,5,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
 0,6,0,0,3,3,3,0,2,0,2,0,4,4,4,0, 2,6,0,0,4,4,4,0,2,5,2,0,0,5,0,0,
 2,6,2,0,3,3,3,0,2,2,2,0,4,4,4,0, 2,5,0,0,4,4,4,0,2,4,2,0,4,4,4,0,
 2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0,
 2,6,0,0,3,3,5,0,2,2,2,0,4,4,6,0, 2,5,0,0,0,4,6,0,2,4,0,0,0,4,7,0};
}
struct nes {
    std::vector<uint8_t> prg;
    std::vector<uint8_t> prg_ram;
    std::array<uint8_t,2048> ram{};
    unsigned mapper=0, submapper=0, header_region=0, bank=0, ppu_fraction=0;
    size_t chr_rom_size=0, chr_ram_size=0;
    bool pal=false, battery=false;
    NesPpu ppu;
    NesApu apu;
    uint16_t pc=0, fault_address=0, instruction_pc=0;
    uint8_t a=0,x=0,y=0,sp=0xfd,p=0x24, opcode=0;
    uint64_t steps=0, cpu_cycles=0, dma_transfers=0;
    bool dma_pending=false;
    uint8_t controller_live[2]{}, controller_shift[2]{};
    bool controller_strobe=false;
    bool controller_witness_enabled=false;
    uint64_t controller_witness_first=0,controller_witness_last=0,controller_witness_dropped=0;
    std::vector<nes_controller_witness_event> controller_witness;
    nes_execution_filter execution_filter{};
    std::vector<nes_execution_event> execution_witness;
    uint64_t execution_dropped=0;
    uint32_t instruction_bank=UINT32_MAX;
    uint8_t interrupt_context=0;
    bool execution_enabled=false;
    unsigned extra_cycles=0;
    const char *fault=nullptr;
    uint32_t code_bank(uint16_t addr) const {
        if(addr<0x8000) return UINT32_MAX;
        if(mapper==2) return addr<0xc000?bank:uint32_t(prg.size()/16384-1);
        return uint32_t(((addr-0x8000)%prg.size())/16384);
    }
    void trace_execution(uint8_t kind,uint16_t address,uint8_t value,uint8_t old,uint8_t old_valid,
                         unsigned before,unsigned after,bool success) {
        if(!execution_enabled || cpu_cycles<execution_filter.cycle_first || cpu_cycles>execution_filter.cycle_last) return;
        uint32_t first=kind==0?execution_filter.exec_first:kind==1?execution_filter.read_first:execution_filter.write_first;
        uint32_t length=kind==0?execution_filter.exec_length:kind==1?execution_filter.read_length:execution_filter.write_length;
        if(!length || address<first || uint32_t(address)-first>=length) return;
        if(execution_witness.size()==execution_filter.limit) {++execution_dropped;return;}
        execution_witness.push_back({cpu_cycles,steps,ppu.frame_count(),instruction_pc,address,instruction_bank,before,after,kind,value,old,old_valid,uint8_t(success),interrupt_context});
    }
    void trace_controller(uint8_t kind,uint8_t port,uint8_t value) {
        if(!controller_witness_enabled || cpu_cycles<controller_witness_first || cpu_cycles>controller_witness_last) return;
        if(controller_witness.size()==8192) {++controller_witness_dropped;return;}
        controller_witness.push_back({cpu_cycles,steps,ppu.frame_count(),instruction_pc,kind,port,value,uint8_t(controller_strobe)});
    }
    bool peek(uint16_t addr, uint8_t &v) const {
        if (addr<0x2000) { v=ram[addr&0x7ff]; return true; }
        if (addr>=0x6000 && addr<0x8000 && !prg_ram.empty()) {v=prg_ram[addr-0x6000];return true;}
        if (addr>=0x8000) {
            size_t offset=mapper==2 ? (addr<0xc000 ? size_t(bank)*16384+(addr-0x8000) : prg.size()-16384+(addr-0xc000)) : (addr-0x8000)%prg.size();
            v=prg[offset]; return true;
        }
        return false;
    }
    uint8_t read_value(uint16_t addr) {
        uint8_t v=0;
        if (fault) return v;
        if(addr>=0x2000 && addr<0x4000) return ppu.read(addr&7);
        if(addr==0x4015) return apu.read_status();
        if(addr==0x4016 || addr==0x4017) {
            unsigned port=addr-0x4016;
            uint8_t bit=uint8_t((controller_strobe?controller_live[port]:controller_shift[port])&1);
            if(!controller_strobe) controller_shift[port]=uint8_t((controller_shift[port]>>1)|0x80);
            trace_controller(1,uint8_t(port+1),uint8_t(0x40|bit));
            return uint8_t(0x40|bit); // Deterministic approximation of upper open-bus bits.
        }
        if (!peek(addr,v) && !fault) { fault="unsupported_bus_read"; fault_address=addr; }
        return v;
    }
    uint8_t read(uint16_t addr) {
        if(fault) return 0;
        uint8_t v=read_value(addr);
        if(execution_enabled) trace_execution(1,addr,v,0,0,bank,bank,!fault);
        return v;
    }
    void write_value(uint16_t addr,uint8_t v) {
        if (fault) return;
        if (addr<0x2000) ram[addr&0x7ff]=v;
        else if(addr<0x4000) ppu.write(addr&7,v);
        else if(addr==0x4014) {
            std::array<uint8_t,256> data{};
            for(unsigned i=0;i<256;i++) {
                uint16_t source=uint16_t((uint16_t(v)<<8)|i);
                if(!peek(source,data[i])) {fault="unsupported_dma_source";fault_address=source;return;}
            }
            for(uint8_t b:data) ppu.write(4,b);
            dma_pending=true;++dma_transfers;
        }
        else if(addr==0x4016) {
            bool strobe=(v&1)!=0;
            if(strobe || controller_strobe) {
                controller_shift[0]=controller_live[0];controller_shift[1]=controller_live[1];
            }
            controller_strobe=strobe;
            trace_controller(0,0,v);
        }
        else if((addr>=0x4000 && addr<=0x4013) || addr==0x4015 || addr==0x4017) apu.write(addr,v);
        else if(addr>=0x6000 && addr<0x8000 && !prg_ram.empty()) prg_ram[addr-0x6000]=v;
        else if(addr>=0x8000 && mapper==2) bank=unsigned(v)&unsigned(prg.size()/16384-1);
        else if(!fault) {fault="unsupported_bus_write";fault_address=addr;}
    }
    void write(uint16_t addr,uint8_t v) {
        if(fault) return;
        if(!execution_enabled) {write_value(addr,v);return;}
        uint8_t old=0;bool valid=(addr<0x2000 || (addr>=0x6000 && addr<0x8000 && !prg_ram.empty())) && peek(addr,old);
        unsigned before=bank;write_value(addr,v);
        trace_execution(2,addr,v,old,uint8_t(valid),before,bank,!fault);
    }
    void advance(unsigned elapsed) {
        cpu_cycles+=elapsed;
        unsigned dots=elapsed*(pal?16u:15u)+ppu_fraction;
        ppu_fraction=dots%5;ppu.advance(dots/5);apu.advance(elapsed);
    }
    uint8_t byte() {uint8_t v=read(pc); pc=uint16_t(pc+1); return v;}
    uint16_t word() {uint8_t lo=byte(); uint8_t hi=byte(); return uint16_t(lo | uint16_t(hi)<<8);}
    void nz(uint8_t v) {p=uint8_t((p&~(N|Z))|(v?0:Z)|(v&N));}
    void compare(uint8_t lhs,uint8_t rhs) {p=uint8_t((p&~C)|(lhs>=rhs?C:0));nz(uint8_t(lhs-rhs));}
    void adc(uint8_t v) {
        const uint8_t old=a; const unsigned sum=unsigned(a)+v+(p&C?1:0); a=uint8_t(sum);
        p=uint8_t((p&~(C|V))|(sum>255?C:0)|((~(old^v)&(old^a)&0x80)?V:0)); nz(a);
    }
    void branch(bool take) {const int8_t off=int8_t(byte()); if(take) {uint16_t before=pc;pc=uint16_t(pc+off);++extra_cycles;if((before&0xff00)!=(pc&0xff00))++extra_cycles;}}
    uint16_t zpword(uint8_t addr) {
        const uint8_t lo=read(addr),hi=read(uint8_t(addr+1));
        return uint16_t(lo|uint16_t(hi)<<8);
    }
    void push(uint8_t value) {write(uint16_t(0x100|sp),value);if(!fault) --sp;}
    uint8_t pop() {++sp;return read(uint16_t(0x100|sp));}
    bool group1() {
        if ((opcode&3)!=1) return false;
        const unsigned op=opcode>>5, mode=(opcode>>2)&7;
        if(op==4 && mode==2) return false; // STA has no immediate form.
        uint16_t address=0;
        switch(mode) {
        case 0: address=zpword(uint8_t(byte()+x));break;
        case 1: address=byte();break;
        case 2: address=pc;pc=uint16_t(pc+1);break;
        case 3: address=word();break;
        case 4: {uint16_t base=zpword(byte());address=uint16_t(base+y);if(op!=4 && (base&0xff00)!=(address&0xff00))++extra_cycles;break;}
        case 5: address=uint8_t(byte()+x);break;
        case 6: {uint16_t base=word();address=uint16_t(base+y);if(op!=4 && (base&0xff00)!=(address&0xff00))++extra_cycles;break;}
        case 7: {uint16_t base=word();address=uint16_t(base+x);if(op!=4 && (base&0xff00)!=(address&0xff00))++extra_cycles;break;}
        }
        if(op==4) {write(address,a);return true;}
        uint8_t value=read(address);
        switch(op) {
        case 0:a|=value;nz(a);break; case 1:a&=value;nz(a);break;
        case 2:a^=value;nz(a);break; case 3:adc(value);break;
        case 5:a=value;nz(a);break; case 6:compare(a,value);break;
        case 7:adc(uint8_t(~value));break;
        }
        return true;
    }
    void shift(unsigned op, uint16_t address, bool accumulator) {
        uint8_t v=accumulator?a:read(address), old=v;
        const uint8_t carry=(p&C)?1:0;
        switch(op) {
        case 0:v=uint8_t(v<<1);p=uint8_t((p&~C)|(old>>7));break;
        case 1:v=uint8_t((v<<1)|carry);p=uint8_t((p&~C)|(old>>7));break;
        case 2:v=uint8_t(v>>1);p=uint8_t((p&~C)|(old&1));break;
        case 3:v=uint8_t((v>>1)|(carry<<7));p=uint8_t((p&~C)|(old&1));break;
        }
        nz(v);if(accumulator) a=v;else write(address,v);
    }
    void increment_memory(uint16_t address, int delta) {
        uint8_t v=uint8_t(read(address)+delta);nz(v);write(address,v);
    }
    uint16_t absolute_x() {return uint16_t(word()+x);}
    uint16_t absolute_y() {return uint16_t(word()+y);}
    uint16_t indexed_read(uint8_t index) {uint16_t base=word(),addr=uint16_t(base+index);if((base&0xff00)!=(addr&0xff00))++extra_cycles;return addr;}
    bool step() {
        if(fault) return false;
        instruction_pc=pc;if(execution_enabled) instruction_bank=code_bank(pc);interrupt_context=0;
        if(apu.dmc_dma_needed()) {
            uint16_t address=apu.dmc_dma_address();uint8_t value=0;
            if(!peek(address,value)){fault="unsupported_dmc_dma_source";fault_address=address;return false;}
            apu.dmc_dma_supply(value);advance(4);
        }
        bool nmi=ppu.take_nmi();
        if(nmi || (apu.irq() && !(p&I))) {
            interrupt_context=nmi?1:2;
            push(uint8_t(pc>>8));push(uint8_t(pc));push(uint8_t((p&~0x10)|0x20));p|=I;
            uint16_t vector=nmi?0xfffa:0xfffe;
            uint8_t lo=read(vector),hi=read(uint16_t(vector+1));pc=uint16_t(lo|uint16_t(hi)<<8);
            advance(7);
            if(fault) return false;
            interrupt_context=0;
        }
        extra_cycles=0;
        const uint8_t old_a=a,old_x=x,old_y=y,old_p=p,old_sp=sp;
        instruction_pc=pc;if(execution_enabled) instruction_bank=code_bank(pc);opcode=byte(); if(fault) {pc=instruction_pc;return false;}
        if(execution_enabled) trace_execution(0,instruction_pc,opcode,0,0,bank,bank,true);
        if (!group1()) switch(opcode) {
        case 0xa2: x=byte();nz(x);break; case 0xa0: y=byte();nz(y);break;
        case 0xa6: x=read(byte());nz(x);break; case 0xb6: x=read(uint8_t(byte()+y));nz(x);break;
        case 0xae: x=read(word());nz(x);break; case 0xbe: x=read(indexed_read(y));nz(x);break;
        case 0xa4: y=read(byte());nz(y);break; case 0xb4: y=read(uint8_t(byte()+x));nz(y);break;
        case 0xac: y=read(word());nz(y);break; case 0xbc: y=read(indexed_read(x));nz(y);break;
        case 0x86: {uint16_t v=byte();write(v,x);break;} case 0x84: {uint16_t v=byte();write(v,y);break;}
        case 0x96: {uint16_t v=uint8_t(byte()+y);write(v,x);break;}
        case 0x94: {uint16_t v=uint8_t(byte()+x);write(v,y);break;}
        case 0x8e: {uint16_t v=word();write(v,x);break;} case 0x8c: {uint16_t v=word();write(v,y);break;}
        case 0xe8: ++x;nz(x);break; case 0xca: --x;nz(x);break;
        case 0xc8: ++y;nz(y);break; case 0x88: --y;nz(y);break;
        case 0xaa: x=a;nz(x);break; case 0xa8: y=a;nz(y);break;
        case 0x8a: a=x;nz(a);break; case 0x98: a=y;nz(a);break;
        case 0x9a: sp=x;break;case 0xba: x=sp;nz(x);break;
        case 0xe0: compare(x,byte());break; case 0xc0: compare(y,byte());break;
        case 0xe4: compare(x,read(byte()));break; case 0xec: compare(x,read(word()));break;
        case 0xc4: compare(y,read(byte()));break; case 0xcc: compare(y,read(word()));break;
        case 0x24: case 0x2c: {uint16_t addr=opcode==0x24?byte():word();uint8_t v=read(addr);
            p=uint8_t((p&~(N|V|Z))|(v&(N|V))|((v&a)?0:Z));break;}
        case 0x0a: case 0x2a: case 0x4a: case 0x6a: shift(opcode>>5,0,true);break;
        case 0x06: case 0x26: case 0x46: case 0x66: {uint16_t v=byte();shift(opcode>>5,v,false);break;}
        case 0x16: case 0x36: case 0x56: case 0x76: {uint16_t v=uint8_t(byte()+x);shift(opcode>>5,v,false);break;}
        case 0x0e: case 0x2e: case 0x4e: case 0x6e: {uint16_t v=word();shift(opcode>>5,v,false);break;}
        case 0x1e: case 0x3e: case 0x5e: case 0x7e: {uint16_t v=absolute_x();shift(opcode>>5,v,false);break;}
        case 0xc6: case 0xe6: {uint16_t v=byte();increment_memory(v,opcode==0xc6?-1:1);break;}
        case 0xd6: case 0xf6: {uint16_t v=uint8_t(byte()+x);increment_memory(v,opcode==0xd6?-1:1);break;}
        case 0xce: case 0xee: {uint16_t v=word();increment_memory(v,opcode==0xce?-1:1);break;}
        case 0xde: case 0xfe: {uint16_t v=absolute_x();increment_memory(v,opcode==0xde?-1:1);break;}
        case 0x18: p&=uint8_t(~C);break; case 0x38: p|=C;break;
        case 0x58: p&=uint8_t(~I);break; case 0x78: p|=I;break;
        case 0xd8: p&=uint8_t(~D);break; case 0xf8: p|=D;break;
        case 0xb8: p&=uint8_t(~V);break;
        case 0x4c: pc=word();break;
        case 0x6c: {uint16_t ptr=word();uint8_t lo=read(ptr);uint8_t hi=read(uint16_t((ptr&0xff00)|uint8_t(ptr+1)));pc=uint16_t(lo|uint16_t(hi)<<8);break;}
        case 0x20: {uint16_t dest=word();uint16_t ret=uint16_t(pc-1);if(!fault){push(uint8_t(ret>>8));push(uint8_t(ret));pc=dest;}break;}
        case 0x60: {uint8_t lo=pop(),hi=pop();pc=uint16_t((lo|uint16_t(hi)<<8)+1);break;}
        case 0x40: {p=uint8_t((pop()&~0x10)|0x20);uint8_t lo=pop(),hi=pop();pc=uint16_t(lo|uint16_t(hi)<<8);break;}
        case 0x48: push(a);break;case 0x08: push(uint8_t(p|0x30));break;
        case 0x68: a=pop();nz(a);break;case 0x28: p=uint8_t((pop()&~0x10)|0x20);break;
        case 0x00: {byte();if(!fault){push(uint8_t(pc>>8));push(uint8_t(pc));push(uint8_t(p|0x30));p|=I;
                     uint8_t lo=read(0xfffe),hi=read(0xffff);pc=uint16_t(lo|uint16_t(hi)<<8);}break;}
        case 0xf0: branch((p&Z)!=0);break; case 0xd0: branch((p&Z)==0);break;
        case 0x90: branch((p&C)==0);break; case 0xb0: branch((p&C)!=0);break;
        case 0x30: branch((p&N)!=0);break; case 0x10: branch((p&N)==0);break;
        case 0x50: branch((p&V)==0);break; case 0x70: branch((p&V)!=0);break;
        case 0xea: break;
        default: fault="unsupported_opcode";fault_address=instruction_pc;break;
        }
        if(fault) {pc=instruction_pc;a=old_a;x=old_x;y=old_y;p=old_p;sp=old_sp;return false;}
        ++steps;unsigned elapsed=cycles[opcode]+extra_cycles;
        if(dma_pending) {elapsed+=513+unsigned((cpu_cycles+elapsed)&1);dma_pending=false;}
        advance(elapsed);return true;
    }
};

nes *nes_create(const uint8_t *rom,size_t size,const char **error) {return nes_create_region(rom,size,0,error);}
nes *nes_create_region(const uint8_t *rom,size_t size,unsigned region,const char **error) {
    if(!error) return nullptr;
    *error="unsupported or invalid iNES/NES 2.0 cartridge";
    if(!rom || size<16 || std::memcmp(rom,"NES\x1a",4) || region>2 || (rom[6]&0x0c)) return nullptr;
    const bool nes2=(rom[7]&0x0c)==0x08;
    if(!nes2 && (rom[6]&0x02)) return nullptr; // iNES battery RAM has ambiguous sizing/persistence.
    if((rom[7]&0x0f)!=(nes2?0x08:0)) return nullptr;
    unsigned mapper=(rom[6]>>4)|(rom[7]&0xf0), submapper=0, timing=0;
    size_t work_ram=0, chr_ram=0;
    if(nes2) {
        mapper|=unsigned(rom[8]&15)<<8;submapper=rom[8]>>4;
        if(rom[9] || (rom[10]&0xf0) || (rom[11]&0xf0) || rom[12]>2 || rom[13] || rom[14] || rom[15]>1) return nullptr;
        if((rom[10]&15)!=0 && (rom[10]&15)!=7) return nullptr;
        if((rom[11]&15)!=0 && (rom[11]&15)!=7) return nullptr;
        work_ram=(rom[10]&15)?8192:0;chr_ram=(rom[11]&15)?8192:0;timing=rom[12];
        if((rom[5] && chr_ram) || (!rom[5] && !chr_ram)) return nullptr;
    } else {
        if(rom[8] || rom[9]>1) return nullptr;
        for(unsigned i=10;i<16;i++) if(rom[i]) return nullptr;
        timing=rom[9];chr_ram=rom[5]?0:8192;
    }
    if(rom[5]>1 || (mapper==0 && (submapper!=0 || (rom[4]!=1 && rom[4]!=2))) ||
       (mapper==2 && (!nes2 || submapper!=1 || rom[4]<2 || rom[4]>32 || (rom[4]&(rom[4]-1)))) ||
       (mapper!=0 && mapper!=2)) return nullptr;
    const size_t prg_size=size_t(rom[4])*16384, chr_size=size_t(rom[5])*8192;
    if(size!=16+prg_size+chr_size) return nullptr;
    nes *m=nullptr;
    try {m=new nes;m->prg.assign(rom+16,rom+16+prg_size);m->prg_ram.resize(work_ram);} catch(...) {delete m;*error="cannot allocate NES machine or cartridge";return nullptr;}
    m->mapper=mapper;m->submapper=submapper;m->header_region=timing;m->pal=region==2 || (region==0 && timing==1);m->battery=(rom[6]&2)!=0;
    m->chr_rom_size=chr_size;m->chr_ram_size=chr_ram;m->ppu.set_pal(m->pal);m->apu.set_pal(m->pal);
    m->ppu.cartridge(rom+16+prg_size,chr_size,(rom[6]&1)!=0);
    uint8_t lo=0,hi=0; m->peek(0xfffc,lo);m->peek(0xfffd,hi); m->pc=uint16_t(lo|uint16_t(hi)<<8);
    *error=nullptr;return m;
}
nes_cartridge_info nes_get_cartridge_info(const nes *m) {return {m->mapper,m->submapper,m->header_region,m->pal?1u:0u,m->bank,m->battery?1u:0u,m->prg.size(),m->chr_rom_size,m->prg_ram.size(),m->chr_ram_size};}
void nes_destroy(nes *m) {delete m;}
bool nes_step(nes *m) {return m->step();}
void nes_set_controller(nes *m,unsigned port,uint8_t buttons) {if(port<2) m->controller_live[port]=buttons;}
bool nes_enable_execution_witness(nes *m,const nes_execution_filter *f) {
    if(!m || !f || !f->limit || f->limit>65536 || f->cycle_first>f->cycle_last ||
       (!f->exec_length && !f->read_length && !f->write_length) ||
       f->exec_first>65536 || f->exec_length>65536-f->exec_first ||
       f->read_first>65536 || f->read_length>65536-f->read_first ||
       f->write_first>65536 || f->write_length>65536-f->write_first) return false;
    try {m->execution_witness.clear();m->execution_witness.reserve(f->limit);} catch(...) {return false;}
    m->execution_filter=*f;m->execution_dropped=0;m->execution_enabled=true;return true;
}
const nes_execution_event *nes_execution_witness(const nes *m,size_t *count,uint64_t *dropped) {
    *count=m->execution_witness.size();*dropped=m->execution_dropped;return m->execution_witness.data();
}
bool nes_enable_controller_witness(nes *m,uint64_t first,uint64_t last) {
    if(first>last) return false;
    try {m->controller_witness.reserve(8192);} catch(...) {return false;}
    m->controller_witness_first=first;m->controller_witness_last=last;m->controller_witness_enabled=true;return true;
}
const nes_controller_witness_event *nes_controller_witness(const nes *m,size_t *count,uint64_t *dropped) {
    *count=m->controller_witness.size();*dropped=m->controller_witness_dropped;return m->controller_witness.data();
}
bool nes_capture_audio(nes *m) {return m->apu.capture();}
const int16_t *nes_audio(const nes *m,size_t *count) {const auto &s=m->apu.samples();*count=s.size();return s.data();}
void nes_audio_consume(nes *m,size_t count) {m->apu.consume_samples(count);}
nes_state nes_get_state(const nes *m) {return {m->pc,m->fault_address,m->a,m->x,m->y,m->sp,m->p,m->opcode,m->steps,m->cpu_cycles,m->ppu.frame_count(),m->apu.write_count(),m->dma_transfers,m->apu.dmc_was_used(),m->apu.truncated(),m->fault};}
bool nes_inspect(const nes *m,uint16_t address,uint8_t *out,size_t size) {
    if(size>0x10000u-address || (!out&&size)) return false;
    for(size_t i=0;i<size;i++) if(!m->peek(uint16_t(address+i),out[i])) return false;
    return true;
}
bool nes_inspect_ppu(const nes *m,uint16_t address,uint8_t *out,size_t size) {
    if(address>=0x4000 || size>0x4000u-address || (!out&&size)) return false;
    for(size_t i=0;i<size;i++) out[i]=m->ppu.memory_read(uint16_t(address+i));
    return true;
}
bool nes_inspect_oam(const nes *m,uint16_t address,uint8_t *out,size_t size) {
    if(address>=256 || size>256u-address || (!out&&size)) return false;
    for(size_t i=0;i<size;i++) out[i]=m->ppu.oam_read(unsigned(address+i));
    return true;
}
const uint8_t *nes_frame(const nes *m) {return m->ppu.frame();}
