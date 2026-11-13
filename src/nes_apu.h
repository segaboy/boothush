#ifndef BOOTHUSH_NES_APU_H
#define BOOTHUSH_NES_APU_H
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Approximate NTSC APU. DMA is serviced by the machine at instruction boundaries.
class NesApu {
public:
    bool capture() {try {pcm.reserve(44100u*30);} catch(...) {return false;}capture_enabled=true;return true;}
    void write(uint16_t addr,uint8_t value) {
        ++writes;
        if(addr>=0x4000 && addr<=0x4007) {
            unsigned ch=(addr-0x4000)/4,reg=addr&3;Pulse &p=pulse[ch];
            if(reg==0) p.control=value;
            if(reg==1) {p.sweep=value;p.sweep_reload=true;}
            if(reg==2) p.period=uint16_t((p.period&0x700)|value);
            if(reg==3) {p.period=uint16_t((p.period&0xff)|((value&7)<<8));if(enabled&(1<<ch))p.length=lengths[value>>3];p.seq=0;p.env.start=true;}
        } else if(addr==0x4008) tri_control=value;
        else if(addr==0x400a) tri_period=uint16_t((tri_period&0x700)|value);
        else if(addr==0x400b) {tri_period=uint16_t((tri_period&0xff)|((value&7)<<8));if(enabled&4)tri_length=lengths[value>>3];tri_reload=true;}
        else if(addr==0x400c) noise_control=value;
        else if(addr==0x400e) noise_mode=value;
        else if(addr==0x400f) {if(enabled&8)noise_length=lengths[value>>3];noise_env.start=true;}
        else if(addr==0x4010) {dmc_control=value;if(!(value&0x80))dmc_irq=false;}
        else if(addr==0x4011) dmc_level=value&0x7f;
        else if(addr==0x4012) dmc_start=value;
        else if(addr==0x4013) dmc_length=value;
        else if(addr==0x4015) {
            enabled=value&0x1f;
            if(!(value&1))pulse[0].length=0;if(!(value&2))pulse[1].length=0;
            if(!(value&4))tri_length=0;if(!(value&8))noise_length=0;
            if(!(value&0x10))dmc_remaining=0;else if(!dmc_remaining)restart_dmc();
            dmc_irq=false;
        } else if(addr==0x4017) {
            five_step=(value&0x80)!=0;irq_inhibit=(value&0x40)!=0;if(irq_inhibit)frame_irq=false;
            frame_clock=0;if(five_step){quarter();half();}
        }
    }
    uint8_t read_status() {
        uint8_t value=uint8_t((pulse[0].length?1:0)|(pulse[1].length?2:0)|(tri_length?4:0)|(noise_length?8:0)|
                              (dmc_remaining?0x10:0)|(frame_irq?0x40:0)|(dmc_irq?0x80:0));
        frame_irq=false;return value;
    }
    bool irq() const {return frame_irq || dmc_irq;}
    bool dmc_dma_needed() const {return !dmc_buffer_full && dmc_remaining;}
    uint16_t dmc_dma_address() const {return dmc_address;}
    void dmc_dma_supply(uint8_t value) {
        if(!dmc_dma_needed()) return;
        dmc_buffer=value;dmc_buffer_full=true;dmc_used=true;
        dmc_address=dmc_address==0xffff?0x8000:uint16_t(dmc_address+1);
        if(!--dmc_remaining) {if(dmc_control&0x40)restart_dmc();else if(dmc_control&0x80)dmc_irq=true;}
    }
    void advance(unsigned n) {
        while(n--) {
            ++clock; ++frame_clock;
            if(frame_clock==3729 || frame_clock==11186) quarter();
            else if(frame_clock==7457) {quarter();half();}
            else if(!five_step && frame_clock==14915) {quarter();half();if(!irq_inhibit)frame_irq=true;frame_clock=0;}
            else if(five_step && frame_clock==18641) {quarter();half();frame_clock=0;}
            if(!(clock&1)) for(auto &p:pulse) {if(!p.timer){p.timer=p.period;p.seq=uint8_t((p.seq+1)&7);}else --p.timer;}
            if(!tri_timer){tri_timer=tri_period;if(tri_length&&tri_linear&&tri_period>1)tri_seq=uint8_t((tri_seq+1)&31);}else --tri_timer;
            if(!noise_timer){noise_timer=uint16_t(noise_periods[noise_mode&15]-1);unsigned tap=(noise_mode&0x80)?6:1;
                unsigned bit=(noise_shift^(noise_shift>>tap))&1;noise_shift=uint16_t((noise_shift>>1)|(bit<<14));}else --noise_timer;
            if(!dmc_timer) {
                dmc_timer=uint16_t(dmc_periods[dmc_control&15]-1);
                if(!dmc_silence) {
                    if(dmc_shift&1) {if(dmc_level<=125)dmc_level+=2;}
                    else if(dmc_level>=2)dmc_level-=2;
                }
                dmc_shift>>=1;
                if(!--dmc_bits) {dmc_bits=8;if(dmc_buffer_full){dmc_shift=dmc_buffer;dmc_buffer_full=false;dmc_silence=false;}else dmc_silence=true;}
            } else --dmc_timer;
            if(capture_enabled) {sample_phase+=44100;if(sample_phase>=1789773){sample_phase-=1789773;sample();}}
        }
    }
    const std::vector<int16_t> &samples() const {return pcm;}
    void consume_samples(size_t count) {pcm.erase(pcm.begin(),pcm.begin()+count);}
    bool truncated() const {return was_truncated;}
    uint64_t write_count() const {return writes;}
    bool dmc_was_used() const {return dmc_used;}
    uint8_t dmc_output() const {return dmc_level;}
private:
    static constexpr uint8_t lengths[32]={10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
    static constexpr uint16_t noise_periods[16]={4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};
    static constexpr uint16_t dmc_periods[16]={428,380,340,320,286,254,226,214,190,160,142,128,106,84,72,54};
    struct Envelope {bool start=false;uint8_t divider=0,decay=0;};
    struct Pulse {uint16_t period=0,timer=0;uint8_t control=0,sweep=0,length=0,seq=0,sweep_divider=0;bool sweep_reload=false;Envelope env;};
    void restart_dmc() {dmc_address=uint16_t(0xc000+unsigned(dmc_start)*64);dmc_remaining=uint16_t(unsigned(dmc_length)*16+1);}
    void envelope(Envelope &e,uint8_t control) {
        if(e.start) {e.start=false;e.decay=15;e.divider=control&15;}
        else if(e.divider) --e.divider;
        else {e.divider=control&15;if(e.decay)--e.decay;else if(control&0x20)e.decay=15;}
    }
    void quarter() {
        envelope(pulse[0].env,pulse[0].control);envelope(pulse[1].env,pulse[1].control);envelope(noise_env,noise_control);
        if(tri_reload)tri_linear=tri_control&0x7f;else if(tri_linear)--tri_linear;
        if(!(tri_control&0x80))tri_reload=false;
    }
    int target(const Pulse &p,unsigned channel) const {
        int delta=p.period>>(p.sweep&7);
        return (p.sweep&8)?int(p.period)-delta-(channel==0?1:0):int(p.period)+delta;
    }
    void half() {
        for(unsigned i=0;i<2;i++) {
            Pulse &p=pulse[i];if(p.length && !(p.control&0x20))--p.length;
            if(!p.sweep_divider && (p.sweep&0x80) && (p.sweep&7) && p.period>=8 && target(p,i)>=0 && target(p,i)<=0x7ff)
                p.period=uint16_t(target(p,i));
            if(!p.sweep_divider || p.sweep_reload){p.sweep_divider=(p.sweep>>4)&7;p.sweep_reload=false;}else --p.sweep_divider;
        }
        if(tri_length && !(tri_control&0x80))--tri_length;
        if(noise_length && !(noise_control&0x20))--noise_length;
    }
    unsigned pulse_output(unsigned i) const {
        static constexpr uint8_t duty[4]={0x02,0x06,0x1e,0xf9};const Pulse &p=pulse[i];
        if(!p.length || p.period<8 || target(p,i)>0x7ff || !(duty[p.control>>6]&(1u<<p.seq)))return 0;
        return (p.control&0x10)?p.control&15:p.env.decay;
    }
    void sample() {
        if(pcm.size()>=44100u*30){was_truncated=true;return;}
        const unsigned p=pulse_output(0)+pulse_output(1);
        const unsigned t=(tri_length&&tri_linear)?(tri_seq<16?15-tri_seq:tri_seq-16):0;
        const unsigned n=(noise_length && !(noise_shift&1))?((noise_control&0x10)?noise_control&15:noise_env.decay):0;
        const double pulse_mix=p?95.88/(8128.0/p+100.0):0.0;
        const double part=t/8227.0+n/12241.0+dmc_level/22638.0;
        const double in=pulse_mix+(part?159.79/(1.0/part+100.0):0.0);
        const double out=0.9873*(last_output+in-last_input);last_input=in;last_output=out;
        pcm.push_back(int16_t(std::clamp(out,-1.0,1.0)*30000));
    }
    Pulse pulse[2]{};Envelope noise_env{};
    uint64_t clock=0,writes=0;uint32_t frame_clock=0,sample_phase=0;
    uint16_t tri_period=0,tri_timer=0,noise_timer=0,noise_shift=1,dmc_address=0,dmc_remaining=0,dmc_timer=0;
    uint8_t enabled=0,tri_control=0,tri_length=0,tri_linear=0,tri_seq=0,noise_control=0,noise_mode=0,noise_length=0,dmc_level=0;
    bool tri_reload=false,five_step=false,irq_inhibit=false,frame_irq=false,capture_enabled=false,was_truncated=false;
    uint8_t dmc_control=0,dmc_start=0,dmc_length=0,dmc_shift=0,dmc_bits=8,dmc_buffer=0;
    bool dmc_irq=false,dmc_buffer_full=false,dmc_silence=true,dmc_used=false;
    double last_input=0,last_output=0;
    std::vector<int16_t> pcm;
};
#endif
