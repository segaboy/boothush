#include "x64_soft_probe.h"
#include "x64_media_internal.h"
#include "x64_pe_loader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {
constexpr uint64_t image_base = 0x10000000, stack_base = 0x20000000;
constexpr size_t stack_size = 65536;
constexpr uint64_t CF=1, PF=4, AF=16, ZF=64, SF=128, OF=2048;
struct Cpu {
    const std::vector<uint8_t> &code;
    std::array<uint8_t, stack_size> stack{};
    std::array<uint64_t, 16> r{};
    uint64_t rip = 0, flags = 2, steps = 0, fault = 0;
    const char *stop = nullptr;
    uint8_t opcode = 0;

    bool fetch(uint64_t p, size_t n, const uint8_t *&out) {
        if (p < image_base || p - image_base > code.size() || n > code.size() - size_t(p - image_base)) {
            stop="instruction_fetch_fault"; fault=p; return false;
        }
        out=code.data()+size_t(p-image_base); return true;
    }
    bool write64(uint64_t p, uint64_t value) {
        if (p < stack_base || p - stack_base > stack.size() || 8 > stack.size() - size_t(p-stack_base)) {
            stop="stack_write_fault"; fault=p; return false;
        }
        for (unsigned i=0;i<8;i++) stack[size_t(p-stack_base)+i]=uint8_t(value>>(8*i));
        return true;
    }
    static uint64_t le(const uint8_t *p, unsigned n) {
        uint64_t v=0; for (unsigned i=0;i<n;i++) v|=uint64_t(p[i])<<(8*i); return v;
    }
    void sub_flags(uint64_t a, uint64_t b, uint64_t v) {
        flags &= ~(CF|PF|AF|ZF|SF|OF);
        if (a < b) flags|=CF;
        uint8_t x=uint8_t(v); x ^= x>>4; x ^= x>>2; x ^= x>>1;
        if (!(x&1)) flags|=PF;
        if ((a^b^v)&0x10) flags|=AF;
        if (!v) flags|=ZF;
        if (v>>63) flags|=SF;
        if (((a^b)&(a^v))>>63) flags|=OF;
    }
    void test_flags(uint64_t value) {
        flags&=~(CF|PF|ZF|SF|OF); // AF is architecturally undefined; preserve it.
        uint8_t x=uint8_t(value);x^=x>>4;x^=x>>2;x^=x>>1;
        if(!(x&1)) flags|=PF;
        if(!value) flags|=ZF;
        if(value>>63) flags|=SF;
    }
    bool step() {
        const uint8_t *p; if (!fetch(rip,1,p)) return false;
        opcode=p[0]; uint64_t next=rip;
        // push r64, including REX.B extended registers.
        unsigned reg=0, len=0;
        if (p[0]>=0x50 && p[0]<=0x57) {reg=p[0]-0x50; len=1;}
        else if (p[0]==0x41) {if (!fetch(rip,2,p)) return false; if(p[1]>=0x50&&p[1]<=0x57){reg=8+p[1]-0x50;len=2;}}
        if (len) {uint64_t sp=r[4]-8; if(!write64(sp,r[reg])) return false; r[4]=sp; next+=len;}
        else if (opcode==0xe8) {
            if(!fetch(rip,5,p)) return false;
            const int32_t disp=int32_t(uint32_t(le(p+1,4))); uint64_t ret=rip+5, sp=r[4]-8;
            if(!write64(sp,ret)) return false; r[4]=sp; next=ret+uint64_t(int64_t(disp));
        } else if(opcode==0x0f) {
            if(!fetch(rip,6,p)) return false;
            if(p[1]!=0x84 && p[1]!=0x85) {stop="unsupported_instruction";return false;}
            bool equal=(flags&ZF)!=0, take=p[1]==0x84?equal:!equal;
            int32_t disp=int32_t(uint32_t(le(p+2,4)));next=rip+6;
            if(take) next+=uint64_t(int64_t(disp));
        } else if (opcode==0x48) {
            if(!fetch(rip,3,p)) return false;
            if(p[1]==0x85 && (p[2]&0xc0)==0xc0) {
                unsigned source=(p[2]>>3)&7,destination=p[2]&7;
                test_flags(r[source]&r[destination]);next+=3;
            } else if(p[1]==0x8d && p[2]==0x45) {
                if(!fetch(rip,4,p)) return false;r[0]=r[5]+uint64_t(int64_t(int8_t(p[3])));next+=4;
            } else if (p[1]==0x83 && p[2]==0xec) {
                if(!fetch(rip,4,p)) return false; uint64_t b=uint64_t(int64_t(int8_t(p[3]))), a=r[4], v=a-b;
                sub_flags(a,b,v); r[4]=v; next+=4;
            } else if(p[1]==0x81 && p[2]==0xec) {
                if(!fetch(rip,7,p)) return false; uint64_t b=uint64_t(int64_t(int32_t(uint32_t(le(p+3,4))))), a=r[4], v=a-b;
                sub_flags(a,b,v); r[4]=v; next+=7;
            } else if (p[1]>=0xb8 && p[1]<=0xbf) {
                if(!fetch(rip,10,p)) return false; r[p[1]-0xb8]=le(p+2,8); next+=10;
            } else if(p[1]==0x89 && p[2]==0x84) {
                if(!fetch(rip,8,p)) return false; if(p[3]!=0x24){stop="unsupported_instruction";return false;}
                uint64_t a=r[4]+uint64_t(int64_t(int32_t(uint32_t(le(p+4,4)))));
                if(!write64(a,r[0])) return false; next+=8;
            } else if(p[1]==0xc7 && p[2]==0x44) {
                if(!fetch(rip,9,p)) return false; if(p[3]!=0x24){stop="unsupported_instruction";return false;}
                uint64_t a=r[4]+uint64_t(int64_t(int8_t(p[4]))), v=uint64_t(int64_t(int32_t(uint32_t(le(p+5,4)))));
                if(!write64(a,v)) return false; next+=9;
            } else {stop="unsupported_instruction";return false;}
        } else if(opcode==0x4d) {
            // REX.WRB TEST r8..r15,r8..r15; register-direct only.
            if(!fetch(rip,3,p)) return false;
            if(p[1]!=0x85 || (p[2]&0xc0)!=0xc0) {stop="unsupported_instruction";return false;}
            unsigned source=8+((p[2]>>3)&7), destination=8+(p[2]&7);
            test_flags(r[source]&r[destination]);next+=3;
        } else if(opcode==0x49) {
            if(!fetch(rip,3,p)) return false;
            if(p[1]==0x89 && p[2]==0x00) {
                if(!write64(r[8],r[0])) return false;next+=3;
            } else if(p[1]==0xc7 && p[2]==0x40) {
                if(!fetch(rip,8,p)) return false;
                uint64_t address=r[8]+uint64_t(int64_t(int8_t(p[3])));
                uint64_t value=uint64_t(int64_t(int32_t(uint32_t(le(p+4,4)))));
                if(!write64(address,value)) return false;next+=8;
            } else {stop="unsupported_instruction";return false;}
        } else if(opcode==0x4c) {
            if(!fetch(rip,5,p)) return false;
            if(p[1]!=0x8d || p[2]!=0x44 || p[3]!=0x24){stop="unsupported_instruction";return false;}
            r[8]=r[4]+uint64_t(int64_t(int8_t(p[4]))); next+=5;
        } else {stop="unsupported_instruction";return false;}
        rip=next; ++steps; return true;
    }
};
}

const char *x64_soft_probe(const uint8_t *disk, size_t size, uint32_t limit,
                           x64_soft_result *result, uint8_t disk_sha[32], uint8_t efi_sha[32])
{
    if (!disk || !result || !disk_sha || !efi_sha || !limit || limit>1000000) return "invalid software probe request";
    x64_media_info media{}; std::vector<uint8_t> efi;
    const char *error=x64_media_extract_efi(disk,size,media,efi); if(error) return error;
    X64PeImage image; error=image.load(efi,image_base); if(error) return error;
    std::memcpy(disk_sha,media.image_sha256,32); std::memcpy(efi_sha,media.efi_sha256,32);
    Cpu cpu{image.bytes()}; cpu.rip=image.entry_address(); cpu.r[4]=stack_base+stack_size-8;
    while(cpu.steps<limit && cpu.step()) {}
    *result={cpu.steps,cpu.rip,cpu.r[4],cpu.r[0],cpu.r[5],cpu.r[8],cpu.flags,cpu.fault,cpu.opcode,
             cpu.stop ? cpu.stop : "step_limit"};
    return nullptr;
}
