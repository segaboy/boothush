#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/x64_soft_probe.cpp"
#include <cassert>

int main()
{
    // Independently encoded: sub rsp,8; lea r8,[rsp+32]; mov rax, imm64;
    // push rax; mov [rsp+16],rax; call +0; unsupported 0xcc.
    std::vector<uint8_t> code={0x48,0x83,0xec,0x08,0x4c,0x8d,0x44,0x24,0x20,
        0x48,0xb8,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x50,
        0x48,0x89,0x84,0x24,0x10,0,0,0,0xe8,0,0,0,0,0xcc};
    Cpu cpu{code}; cpu.rip=image_base; cpu.r[4]=stack_base+0x100;
    for(int i=0;i<6;i++) assert(cpu.step());
    assert(cpu.steps==6 && cpu.rip==image_base+33 && cpu.r[4]==stack_base+0xe8);
    assert(cpu.r[8]==stack_base+0x118 && cpu.r[0]==0x1122334455667788ULL);
    assert(Cpu::le(cpu.stack.data()+0xf0,8)==0x1122334455667788ULL);
    assert(Cpu::le(cpu.stack.data()+0x100,8)==0x1122334455667788ULL);
    assert(Cpu::le(cpu.stack.data()+0xe8,8)==image_base+33);
    assert(!cpu.step() && std::strcmp(cpu.stop,"unsupported_instruction")==0 && cpu.steps==6);

    // Stack write failure leaves RIP, registers, and instruction count intact.
    std::vector<uint8_t> push={0x50}; Cpu underflow{push};
    underflow.rip=image_base; underflow.r[4]=stack_base;
    assert(!underflow.step() && std::strcmp(underflow.stop,"stack_write_fault")==0);
    assert(underflow.rip==image_base && underflow.r[4]==stack_base && underflow.steps==0);
    // A truncated instruction is never executed.
    std::vector<uint8_t> short_call={0xe8,0}; Cpu truncated{short_call};
    truncated.rip=image_base; truncated.r[4]=stack_base+32;
    assert(!truncated.step() && std::strcmp(truncated.stop,"instruction_fetch_fault")==0 && truncated.steps==0);
    // SUB sets the six arithmetic flags and preserves the reserved bit.
    std::vector<uint8_t> sub={0x48,0x83,0xec,0x01}; Cpu flags{sub};
    flags.rip=image_base; flags.r[4]=0;
    assert(flags.step() && flags.r[4]==UINT64_MAX);
    assert((flags.flags & (CF|PF|AF|ZF|SF|OF))==(CF|PF|AF|SF));
    Cpu overflow{sub}; overflow.rip=image_base; overflow.r[4]=0x8000000000000000ULL;
    assert(overflow.step() && overflow.r[4]==0x7fffffffffffffffULL);
    assert((overflow.flags & OF) != 0 && (overflow.flags & CF) == 0);

    // Extended push, imm32 subtraction and sign-extended immediate store.
    std::vector<uint8_t> extended={0x41,0x57,0x48,0x81,0xec,0x10,0,0,0,
                                   0x48,0xc7,0x44,0x24,0x08,0xff,0xff,0xff,0xff};
    Cpu ext{extended}; ext.rip=image_base; ext.r[4]=stack_base+0x80;
    ext.r[15]=0xdeadbeef12345678ULL;
    assert(ext.step() && ext.step() && ext.step());
    assert(ext.r[4]==stack_base+0x68);
    assert(Cpu::le(ext.stack.data()+0x78,8)==0xdeadbeef12345678ULL);
    assert(Cpu::le(ext.stack.data()+0x70,8)==UINT64_MAX);
    // TEST preserves registers, clears CF/OF, and derives SF/ZF/PF from the AND.
    std::vector<uint8_t> test={0x4d,0x85,0xc8}; // TEST r8,r9
    Cpu t{test};t.rip=image_base;t.r[8]=0x8000000000000001ULL;t.r[9]=UINT64_MAX;t.flags=CF|OF|AF|2;
    assert(t.step() && t.r[8]==0x8000000000000001ULL && t.r[9]==UINT64_MAX);
    assert((t.flags&(CF|OF|SF|ZF|PF|AF))==(SF|AF));
    Cpu z{test};z.rip=image_base;z.r[8]=1;z.r[9]=2;z.flags=CF|OF|2;
    assert(z.step() && (z.flags&(CF|OF|SF|ZF|PF))==(ZF|PF));
    std::vector<uint8_t> unsupported_test={0x4d,0x85,0x00};Cpu bad{unsupported_test};bad.rip=image_base;
    assert(!bad.step() && !std::strcmp(bad.stop,"unsupported_instruction") && bad.steps==0);
    std::vector<uint8_t> short_test={0x4d,0x85};Cpu incomplete{short_test};incomplete.rip=image_base;
    assert(!incomplete.step() && !std::strcmp(incomplete.stop,"instruction_fetch_fault") && incomplete.steps==0);
    std::vector<uint8_t> branch={0x48,0x85,0xc9,0x0f,0x84,0x03,0,0,0,0xcc,0xcc,0xcc,0x48,0x8d,0x45,0xff};
    Cpu jump{branch};jump.rip=image_base;jump.r[1]=0;jump.r[5]=123;
    assert(jump.step() && jump.step() && jump.rip==image_base+12);
    assert(jump.step() && jump.r[0]==122);
    Cpu not_taken{branch};not_taken.rip=image_base;not_taken.r[1]=1;
    assert(not_taken.step() && not_taken.step() && not_taken.rip==image_base+9);
    std::vector<uint8_t> stores={0x49,0x89,0x00,0x49,0xc7,0x40,0x08,0xff,0xff,0xff,0xff};
    Cpu mem{stores};mem.rip=image_base;mem.r[8]=stack_base+16;mem.r[0]=0x1234;
    assert(mem.step() && mem.step());assert(Cpu::le(mem.stack.data()+16,8)==0x1234);
    assert(Cpu::le(mem.stack.data()+24,8)==UINT64_MAX);
    Cpu bad_store{stores};bad_store.rip=image_base;bad_store.r[8]=stack_base+stack_size;
    assert(!bad_store.step() && !std::strcmp(bad_store.stop,"stack_write_fault") && bad_store.steps==0);
    return 0;
}
