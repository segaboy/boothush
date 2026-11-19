#include "mac128k.h"

#include "iwm.h"
#include "mac_keyboard.h"
#include "mac_rtc.h"
#include "Moira.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace {

// Macintosh 128K startup and display wiring: ClasSICK 0.97 SPEC-0018.
constexpr uint32_t rom_base = 0x400000;
constexpr uint32_t overlay_ram_base = 0x600000;
constexpr uint32_t normal_ram_aperture = 0x400000;
constexpr uint32_t overlay_ram_aperture = 0x80000;
constexpr uint32_t ram_offset_mask = MAC128K_RAM_SIZE - 1;
static_assert((MAC128K_RAM_SIZE & ram_offset_mask) == 0);
constexpr uint32_t main_frame_base = 0x1a700;
constexpr uint32_t alternate_frame_base = 0x12700;
constexpr uint32_t iwm_base = 0xc00000;
constexpr uint32_t iwm_size = 0x200000;
constexpr uint32_t via_base = 0xefe1fe;
constexpr uint32_t via_register_stride = 0x200;
constexpr uint32_t via_register_count = 16;
constexpr uint8_t overlay_select = 0x10;
constexpr uint8_t floppy_select = 0x20;
constexpr uint8_t main_frame_select = 0x40;
constexpr uint8_t mouse_button = 0x08;
constexpr uint8_t mouse_x_phase = 0x10;
constexpr uint8_t mouse_y_phase = 0x20;
constexpr uint8_t rtc_data = 0x01;
constexpr uint8_t via_ca2_flag = 0x01;
constexpr uint8_t via_ca1_flag = 0x02;
constexpr uint8_t via_shift_flag = 0x04;
constexpr uint8_t via_shift_mode = 0x1c;
constexpr uint8_t via_t1_flag = 0x40;
constexpr uint8_t via_t1_continuous = 0x40;
constexpr uint8_t via_irq_bit = 0x80;
constexpr uint64_t cycles_per_vbl = 130240;
constexpr uint64_t cycles_per_via_tick = 10;

// The current ClasSICK ROMs use the upper control-port aliases in SPEC-0072.
// Register selection and external/status latching follow Zilog UM0109.
constexpr uint32_t scc_read_b = 0x9ffff8;
constexpr uint32_t scc_read_a = 0x9ffffa;
constexpr uint32_t scc_write_b = 0xbffff9;
constexpr uint32_t scc_write_a = 0xbffffb;
constexpr uint8_t scc_tx_empty = 0x04;
constexpr uint8_t scc_dcd = 0x08;
constexpr uint8_t scc_master_irq_enable = 0x08;
constexpr uint8_t scc_ext_pending_a = 0x08;
constexpr uint8_t scc_ext_pending_b = 0x01;
constexpr size_t scc_channel_a = 1;
constexpr size_t scc_channel_b = 0;

// This escapes the CPU's guest exception handling on an unimplemented bus access.
struct AccessFault {};

struct SccChannel {
    uint8_t phase = 2;
    uint8_t status_mask = 0;
    bool status_irq_enabled = false;
    bool dcd_latched = false;
    bool dcd_latch = false;

    bool live_dcd() const { return (phase & 1) != 0; }
    bool dcd_irq_enabled() const { return (status_mask & scc_dcd) != 0; }
    bool irq_pending() const { return status_irq_enabled && dcd_irq_enabled() && dcd_latched; }
    bool dcd_status() const { return dcd_irq_enabled() && dcd_latched ? dcd_latch : live_dcd(); }

    void set_phase(uint8_t next_phase)
    {
        const bool next_dcd = (next_phase & 1) != 0;
        if (dcd_irq_enabled() && !dcd_latched && next_dcd != live_dcd()) {
            dcd_latched = true;
            dcd_latch = next_dcd;
        }
        phase = next_phase;
    }

    void acknowledge_dcd()
    {
        // An odd number of further transitions leaves another change pending.
        dcd_latched = dcd_latched && dcd_latch != live_dcd();
        dcd_latch = live_dcd();
    }

    void reset()
    {
        status_mask = 0;
        status_irq_enabled = false;
        dcd_latched = false;
    }
};

class Cpu final : public moira::Moira {
public:
    explicit Cpu(mac128k &machine) : machine_(machine) {}

private:
    mac128k &machine_;

    void sync(int cycles) override;
    void willInterrupt(moira::u8 level) override;
    moira::u8 read8(moira::u32 address) const override;
    moira::u16 read16(moira::u32 address) const override;
    moira::u32 read32(moira::u32 address) const override;
    void write8(moira::u32 address, moira::u8 value) const override;
    void write16(moira::u32 address, moira::u16 value) const override;
    void write32(moira::u32 address, moira::u32 value) const override;
};

bool contains(uint32_t base, uint32_t capacity, uint32_t address, size_t size)
{
    return address >= base && address - base <= capacity &&
           size <= capacity - (address - base);
}

uint8_t port_pins(uint8_t output, uint8_t direction, uint8_t inputs = 0xff)
{
    return static_cast<uint8_t>((output & direction) | (inputs & ~direction));
}

} // namespace

struct mac128k {
    std::array<uint8_t, MAC128K_ROM_SIZE> rom{};
    std::array<uint8_t, MAC128K_RAM_SIZE> ram{};
    uint8_t port_a = 0;
    uint8_t port_b = 0;
    uint8_t direction_a = 0;
    uint8_t direction_b = 0;
    uint8_t auxiliary_control = 0;
    uint8_t peripheral_control = 0;
    uint8_t interrupt_flags = 0;
    uint8_t interrupt_enable = 0;
    uint16_t timer1_latch = 0;
    uint64_t timer1_deadline = 0;
    std::array<SccChannel, 2> scc{};
    uint8_t scc_pointer = 0;
    uint8_t scc_interrupt_vector = 0;
    bool scc_irq_enabled = false;
    bool mouse_pressed = false;
    uint64_t vbl = 0;
    uint64_t next_vbl_cycle = cycles_per_vbl;
    uint32_t fault_address = 0;
    const char *fault_operation = nullptr;
    mac128k_ram_steps ram_steps{};
    uint8_t irq_level = 0;
    std::unique_ptr<mac128k_mouse_witness_event[]> witness;
    uint64_t witness_first = 0;
    uint64_t witness_last = 0;
    uint64_t witness_dropped = 0;
    size_t witness_count = 0;
    bool witness_detail = false;
    bool witness_cpu = false;
    uint8_t witness_cpu_mask = 8; // 8 denotes no boundary baseline yet.
    uint32_t witness_pending = 0;
    uint32_t witness_ram_first = 0, witness_ram_length = 0;
    uint64_t witness_ram_matched = 0, witness_ram_dropped = 0;
    std::unique_ptr<mac128k_keyboard_event[]> keyboard_witness;
    size_t keyboard_witness_count=0;
    uint32_t keyboard_witness_limit=8192;
    uint64_t keyboard_witness_first=0,keyboard_witness_last=0,keyboard_witness_dropped=0,keyboard_rejected=0;
    std::unique_ptr<mac128k_display_event[]> display_witness;
    size_t display_witness_count=0;
    uint64_t display_first=0,display_last=0,display_dropped=0;
    uint32_t display_ram_first=0,display_ram_length=0;
    boothush::Iwm iwm{};
    MacKeyboard keyboard{};
    MacRtc rtc{};
    Cpu cpu{*this};

    void keyboard_observe(uint64_t cycle,uint8_t kind,uint8_t value,uint8_t queue,uint64_t due=0) {
        if(!keyboard_witness || cycle<keyboard_witness_first || cycle>keyboard_witness_last) return;
        if(keyboard_witness_count==keyboard_witness_limit) {++keyboard_witness_dropped;return;}
        keyboard_witness[keyboard_witness_count++]={cycle,due,cpu.getPC(),kind,value,queue};
    }
    static void keyboard_trace(void *context,uint64_t cycle,MacKeyboard::TraceKind kind,uint8_t value,size_t queue) {
        auto *machine=static_cast<mac128k *>(context);
        machine->keyboard_observe(cycle,static_cast<uint8_t>(kind),value,static_cast<uint8_t>(queue));
    }
    void display_observe(uint8_t kind,uint32_t address=0,uint32_t value=0,uint8_t width=0) {
        uint64_t cycle=static_cast<uint64_t>(cpu.getClock());
        if(!display_witness || cycle<display_first || cycle>display_last) return;
        if(display_witness_count==8192) {++display_dropped;return;}
        display_witness[display_witness_count++]={cycle,vbl,cpu.getPC0(),address,value,kind,width};
    }

    bool overlay() const
    {
        return (port_pins(port_a, direction_a) & overlay_select) != 0;
    }

    bool floppy_select_high() const
    {
        return (port_pins(port_a, direction_a) & floppy_select) != 0;
    }

    void update_irq()
    {
        const uint8_t via_irq = (interrupt_flags & interrupt_enable) != 0 ? 1 : 0;
        const uint8_t scc_irq = scc_irq_enabled && scc_pending() != 0 ? 2 : 0;
        const uint8_t next_irq = via_irq | scc_irq;
        cpu.setIPL(next_irq);
        if (next_irq != irq_level) {
            irq_level = next_irq;
            if (witness) {
                observe(MAC128K_WITNESS_IRQ, static_cast<uint64_t>(cpu.getClock()), 0, 0, irq_level);
            }
        }
    }

    uint8_t scc_pending() const
    {
        return (scc[scc_channel_a].irq_pending() ? scc_ext_pending_a : 0) |
               (scc[scc_channel_b].irq_pending() ? scc_ext_pending_b : 0);
    }

    uint8_t via_ifr() const
    {
        return interrupt_flags | ((interrupt_flags & interrupt_enable) != 0 ? via_irq_bit : 0);
    }

    void observe(mac128k_mouse_witness_kind kind, uint64_t cycle, uint32_t address = 0,
                 uint8_t reg = 0, uint8_t value = 0)
    {
        if (cycle < witness_first || cycle > witness_last) {
            return;
        }
        if (witness_count == MAC128K_MOUSE_WITNESS_CAPACITY) {
            ++witness_dropped;
            return;
        }
        const auto &x = scc[scc_channel_a];
        const auto &y = scc[scc_channel_b];
        const uint8_t latched = x.dcd_latched | (y.dcd_latched << 1);
        const uint8_t dcd = x.dcd_status() | (y.dcd_status() << 1);
        witness[witness_count++] = {cycle, address, static_cast<uint8_t>(kind), reg, value,
                                    x.phase, y.phase, mouse_pressed, scc_pending(), latched,
                                    dcd, via_ifr(), interrupt_enable, irq_level,
                                    cpu.getPC0(), 0, 0, witness_pending, 0, 0, 0, 0, 0, 0, 0, 0};
    }

    void observe_cpu_mask(uint32_t instruction)
    {
        const uint64_t cycle = static_cast<uint64_t>(cpu.getClock());
        if (cycle < witness_first || cycle > witness_last) return;
        const uint8_t mask = static_cast<uint8_t>((cpu.getSR() >> 8) & 7);
        if (mask != witness_cpu_mask) {
            observe(MAC128K_WITNESS_CPU_MASK, cycle, instruction, witness_cpu_mask, mask);
            witness_cpu_mask = mask;
        }
    }

    unsigned ca2_mode() const
    {
        return (peripheral_control >> 1) & 7;
    }

    void acknowledge_port_a()
    {
        const unsigned mode = ca2_mode();
        const uint8_t handshake_ca2 = mode == 0 || mode == 2 ? via_ca2_flag : 0;
        clear_interrupts(via_ca1_flag | handshake_ca2);
    }

    void tick(uint64_t cycle)
    {
        if (witness && witness_count == 0) {
            observe(MAC128K_WITNESS_SNAPSHOT, cycle);
        }
        if (cycle >= next_vbl_cycle) {
            const uint64_t elapsed = (cycle - next_vbl_cycle) / cycles_per_vbl + 1;
            vbl += elapsed;
            next_vbl_cycle += elapsed * cycles_per_vbl;
            if(display_witness) display_observe(0,0,static_cast<uint32_t>(elapsed));
            interrupt_flags |= via_ca1_flag;
            update_irq();
        }
        if (timer1_deadline != 0 && cycle >= timer1_deadline) {
            if ((auxiliary_control & via_t1_continuous) != 0) {
                const uint64_t period = timer1_period();
                timer1_deadline += ((cycle - timer1_deadline) / period + 1) * period;
            } else {
                timer1_deadline = 0;
            }
            interrupt_flags |= via_t1_flag;
            update_irq();
        }
        if (keyboard.advance(cycle)) {
            interrupt_flags |= via_shift_flag;
            update_irq();
        }
        if (rtc.advance(cycle) && ca2_mode() < 4) {
            interrupt_flags |= via_ca2_flag;
            update_irq();
        }
    }

    uint64_t timer1_period() const
    {
        // Free-run reload includes two VIA ticks of counter/reload overhead.
        return (static_cast<uint64_t>(timer1_latch) + 2) * cycles_per_via_tick;
    }

    uint16_t timer1_counter() const
    {
        if (timer1_deadline == 0) {
            return 0xffff;
        }
        return static_cast<uint16_t>((timer1_deadline - cpu.getClock() - 1) / cycles_per_via_tick);
    }

    void clear_interrupts(uint8_t mask)
    {
        interrupt_flags &= static_cast<uint8_t>(~mask);
        update_irq();
    }

    [[noreturn]] void fault(uint32_t address, const char *operation)
    {
        fault_address = address;
        fault_operation = operation;
        if(display_witness) display_observe(2,address);
        throw AccessFault{};
    }

    uint8_t *writable_memory(uint32_t address, size_t size)
    {
        const bool startup = overlay();
        const uint32_t base = startup ? overlay_ram_base : 0;
        const uint32_t offset = address & ram_offset_mask;
        if (contains(base, startup ? overlay_ram_aperture : normal_ram_aperture, address, size) &&
            size <= MAC128K_RAM_SIZE - offset) {
            return ram.data() + offset;
        }
        return nullptr;
    }

    const uint8_t *readable_memory(uint32_t address, size_t size)
    {
        if (contains(rom_base, MAC128K_ROM_SIZE, address, size)) {
            return rom.data() + (address - rom_base);
        }
        if (overlay() && contains(0, MAC128K_ROM_SIZE, address, size)) {
            return rom.data() + address;
        }
        return writable_memory(address, size);
    }

    uint8_t read_via(uint32_t reg, uint32_t address)
    {
        switch (reg) {
        case 0: {
            uint8_t inputs = static_cast<uint8_t>(~(mouse_button | mouse_x_phase | mouse_y_phase));
            inputs |= mouse_pressed ? 0 : mouse_button;
            inputs |= (scc[scc_channel_a].phase & 2) != 0 ? mouse_x_phase : 0;
            inputs |= (scc[scc_channel_b].phase & 2) != 0 ? mouse_y_phase : 0;
            if (!rtc.data_high()) {
                inputs &= static_cast<uint8_t>(~rtc_data);
            }
            return port_pins(port_b, direction_b, inputs);
        }
        case 1:
            acknowledge_port_a();
            return port_pins(port_a, direction_a);
        case 2:
            return direction_b;
        case 3:
            return direction_a;
        case 4:
            clear_interrupts(via_t1_flag);
            return static_cast<uint8_t>(timer1_counter());
        case 5:
            return static_cast<uint8_t>(timer1_counter() >> 8);
        case 6:
            return static_cast<uint8_t>(timer1_latch);
        case 7:
            return static_cast<uint8_t>(timer1_latch >> 8);
        case 10:
            clear_interrupts(via_shift_flag);
            return keyboard.read_shift_register(cpu.getClock());
        case 11:
            return auxiliary_control;
        case 12:
            return peripheral_control;
        case 13:
            return via_ifr();
        case 14:
            return interrupt_enable | via_irq_bit;
        case 15:
            return port_pins(port_a, direction_a);
        default:
            fault(address, "read8");
        }
    }

    void write_via(uint32_t reg, uint32_t address, uint8_t value)
    {
        switch (reg) {
        case 0:
            port_b = value;
            rtc.set_port_b(port_b, direction_b);
            return;
        case 1:
            acknowledge_port_a();
            port_a = value;
            return;
        case 2:
            direction_b = value;
            rtc.set_port_b(port_b, direction_b);
            return;
        case 3:
            direction_a = value;
            return;
        case 4:
        case 6:
            timer1_latch = (timer1_latch & 0xff00) | value;
            return;
        case 5:
        case 7:
            timer1_latch = (timer1_latch & 0x00ff) | (static_cast<uint16_t>(value) << 8);
            if (reg == 5) {
                timer1_deadline = static_cast<uint64_t>(cpu.getClock()) + timer1_period();
            }
            clear_interrupts(via_t1_flag);
            return;
        case 10:
            clear_interrupts(via_shift_flag);
            keyboard.write_shift_register(value, cpu.getClock());
            return;
        case 11: {
            const uint8_t mode = value & via_shift_mode;
            if ((value & ~(via_t1_continuous | via_shift_mode)) != 0 ||
                (mode != 0 && mode != 0x0c && mode != 0x1c)) {
                fault(address, "write8");
            }
            auxiliary_control = value;
            keyboard.set_auxiliary_control(value, cpu.getClock());
            return;
        }
        case 12:
            peripheral_control = value;
            return;
        case 13:
            clear_interrupts(value);
            return;
        case 14:
            if ((value & via_irq_bit) != 0) {
                interrupt_enable |= value & 0x7f;
            } else {
                interrupt_enable &= static_cast<uint8_t>(~value);
            }
            update_irq();
            return;
        case 15:
            port_a = value;
            return;
        default:
            fault(address, "write8");
        }
    }

    uint8_t read_scc(uint32_t address)
    {
        const bool channel_a = address == scc_read_a;
        const SccChannel &channel = scc[channel_a ? scc_channel_a : scc_channel_b];
        const uint8_t reg = scc_pointer;
        scc_pointer = 0;
        switch (reg) {
        case 0:
            return scc_tx_empty | (channel.dcd_status() ? scc_dcd : 0);
        case 2:
            if (channel_a) {
                return scc_interrupt_vector;
            }
            fault(address, "read8");
        case 3:
            return channel_a ? scc_pending() : 0;
        case 15:
            return channel.status_mask;
        default:
            fault(address, "read8");
        }
    }

    void write_scc(uint32_t address, uint8_t value)
    {
        SccChannel &channel = scc[address == scc_write_a ? scc_channel_a : scc_channel_b];
        const uint8_t reg = scc_pointer;
        scc_pointer = 0;
        switch (reg) {
        case 0: {
            const uint8_t command = value & 0xf8;
            if (command != 0 && command != 0x08 && command != 0x10 && command != 0x38) {
                fault(address, "write8");
            }
            if (command == 0x10) {
                channel.acknowledge_dcd();
            }
            // The Mac uses CPU autovectors, so no SCC IUS is set by a bus acknowledge.
            scc_pointer = value & (command == 0x08 ? 0x0f : 0x07);
            return;
        }
        case 1:
            if ((value & 0xfe) != 0) {
                fault(address, "write8");
            }
            channel.status_irq_enabled = (value & 1) != 0;
            return;
        case 2:
            scc_interrupt_vector = value;
            return;
        case 9:
            if ((value & 0x40) != 0) {
                scc[scc_channel_b].reset();
            }
            if ((value & 0x80) != 0) {
                scc[scc_channel_a].reset();
            }
            if ((value & 0xc0) == 0xc0) {
                scc_interrupt_vector = 0;
            }
            scc_irq_enabled = (value & scc_master_irq_enable) != 0;
            return;
        case 15:
            channel.status_mask = value;
            if (!channel.dcd_irq_enabled()) {
                channel.dcd_latched = false;
            }
            return;
        default:
            fault(address, "write8");
        }
    }

    uint32_t read_bus(uint32_t address, size_t size, const char *operation)
    {
        /* A 68000 longword beginning in the final word of a physical bank
           has two word transfers with independently decoded addresses. */
        if (size == 4 && (address & ram_offset_mask) == ram_offset_mask - 1 && writable_memory(address, 2)) {
            const uint32_t high = read_bus(address, 2, "read16");
            return (high << 16) | read_bus(address + 2, 2, "read16");
        }
        if ((size == 1 || size == 2) && contains(iwm_base, iwm_size, address, size)) {
            const uint8_t value = iwm.read((address >> 9) & 15, floppy_select_high(), cpu.getClock());
            return size == 1 ? value : (uint32_t{value} << 8) | value;
        }
        if (size == 1 && (address == scc_read_a || address == scc_read_b)) {
            const uint8_t reg = scc_pointer;
            const uint8_t value = read_scc(address);
            if (witness) {
                observe(MAC128K_WITNESS_SCC_READ, static_cast<uint64_t>(cpu.getClock()), address, reg, value);
            }
            return value;
        }
        const uint32_t via_offset = address - via_base;
        if (size == 1 && via_offset < via_register_count * via_register_stride &&
            via_offset % via_register_stride == 0) {
            const uint8_t reg = static_cast<uint8_t>(via_offset / via_register_stride);
            const uint8_t value = read_via(reg, address);
            if (witness && (reg <= 3 || reg >= 11)) {
                observe(MAC128K_WITNESS_VIA_READ, static_cast<uint64_t>(cpu.getClock()), address, reg, value);
            }
            return value;
        }
        if (const uint8_t *bytes = readable_memory(address, size)) {
            uint32_t value = 0;
            for (size_t i = 0; i < size; ++i) {
                value = (value << 8) | bytes[i];
            }
            return value;
        }
        fault(address, operation);
    }

    void write_bus(uint32_t address, size_t size, uint32_t value, const char *operation)
    {
        if (size == 4 && (address & ram_offset_mask) == ram_offset_mask - 1 && writable_memory(address, 2)) {
            write_bus(address, 2, value >> 16, "write16");
            write_bus(address + 2, 2, value & 0xffff, "write16");
            return;
        }
        if ((size == 1 || size == 2) && contains(iwm_base, iwm_size, address, size)) {
            iwm.write((address >> 9) & 15, floppy_select_high(), cpu.getClock(), static_cast<uint8_t>(value));
            return;
        }
        if (size == 1 && (address == scc_write_a || address == scc_write_b)) {
            const uint8_t reg = scc_pointer;
            write_scc(address, static_cast<uint8_t>(value));
            update_irq();
            if (witness) {
                observe(MAC128K_WITNESS_SCC_WRITE, static_cast<uint64_t>(cpu.getClock()), address, reg,
                        static_cast<uint8_t>(value));
            }
            return;
        }
        const uint32_t via_offset = address - via_base;
        if (size == 1 && via_offset < via_register_count * via_register_stride &&
            via_offset % via_register_stride == 0) {
            const uint8_t reg = static_cast<uint8_t>(via_offset / via_register_stride);
            write_via(reg, address, static_cast<uint8_t>(value));
            if (witness && (reg <= 3 || reg >= 11)) {
                observe(MAC128K_WITNESS_VIA_WRITE, static_cast<uint64_t>(cpu.getClock()), address, reg,
                        static_cast<uint8_t>(value));
            }
            return;
        }
        if (uint8_t *bytes = writable_memory(address, size)) {
            const uint32_t original = value;
            for (size_t i = size; i != 0; --i) {
                bytes[i - 1] = static_cast<uint8_t>(value);
                value >>= 8;
            }
            if(display_witness && display_ram_length) {
                const uint32_t offset=static_cast<uint32_t>(bytes-ram.data());
                if(offset<display_ram_first+display_ram_length && display_ram_first<offset+size)
                    display_observe(1,offset,original,static_cast<uint8_t>(size));
            }
            if (witness && witness_detail && witness_ram_length) {
                const uint32_t offset = static_cast<uint32_t>(bytes - ram.data());
                const uint32_t first = std::max(offset, witness_ram_first);
                const uint32_t last = std::min(offset + static_cast<uint32_t>(size),
                                               witness_ram_first + witness_ram_length);
                const uint64_t cycle = static_cast<uint64_t>(cpu.getClock());
                if (first < last && cycle >= witness_first && cycle <= witness_last) {
                    ++witness_ram_matched;
                    const size_t before = witness_count;
                    observe(MAC128K_WITNESS_RAM_WRITE, cycle, address);
                    if (witness_count == before) {
                        ++witness_ram_dropped;
                    } else {
                        auto &event = witness[before];
                        event.ram_offset = offset;
                        event.width = static_cast<uint8_t>(size);
                        event.captured_offset = static_cast<uint8_t>(first - offset);
                        event.captured_width = static_cast<uint8_t>(last - first);
                        const unsigned shift = 8 * (static_cast<unsigned>(size) - event.captured_offset - event.captured_width);
                        const uint32_t mask = event.captured_width == 4 ? UINT32_MAX :
                                              (UINT32_C(1) << (8 * event.captured_width)) - 1;
                        event.captured_value = (original >> shift) & mask;
                    }
                }
            }
            return;
        }
        fault(address, operation);
    }
};

namespace {

void Cpu::sync(int cycles)
{
    Moira::sync(cycles);
    machine_.tick(static_cast<uint64_t>(getClock()));
}

void Cpu::willInterrupt(moira::u8 level)
{
    if (machine_.witness && machine_.witness_detail)
        machine_.observe(MAC128K_WITNESS_IRQ_ACCEPT, static_cast<uint64_t>(getClock()), 0, 0, level);
}

moira::u8 Cpu::read8(moira::u32 address) const
{
    return static_cast<moira::u8>(machine_.read_bus(address, 1, "read8"));
}

moira::u16 Cpu::read16(moira::u32 address) const
{
    return static_cast<moira::u16>(machine_.read_bus(address, 2, "read16"));
}

moira::u32 Cpu::read32(moira::u32 address) const
{
    return machine_.read_bus(address, 4, "read32");
}

void Cpu::write8(moira::u32 address, moira::u8 value) const
{
    machine_.write_bus(address, 1, value, "write8");
}

void Cpu::write16(moira::u32 address, moira::u16 value) const
{
    machine_.write_bus(address, 2, value, "write16");
}

void Cpu::write32(moira::u32 address, moira::u32 value) const
{
    machine_.write_bus(address, 4, value, "write32");
}

} // namespace

mac128k *mac128k_create(const uint8_t rom[MAC128K_ROM_SIZE])
{
    try {
        auto machine = std::make_unique<mac128k>();
        std::memcpy(machine->rom.data(), rom, machine->rom.size());
        machine->cpu.setModel(moira::Model::M68000);
        try {
            machine->cpu.reset();
        } catch (const AccessFault &) {
            // Retain the faulting machine so the caller can report the reset failure.
        }
        return machine.release();
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}

void mac128k_destroy(mac128k *machine)
{
    delete machine;
}

bool mac128k_insert_floppy(mac128k *machine, const uint8_t *image, size_t size)
{
    if (machine == nullptr || image == nullptr) {
        return false;
    }
    try {
        return machine->iwm.insert(std::span(image, size));
    } catch (const std::bad_alloc &) {
        return false;
    }
}

bool mac128k_enable_floppy_writes(mac128k *machine)
{
    return machine && machine->iwm.enable_writes();
}

const uint8_t *mac128k_export_floppy(mac128k *machine, size_t *size)
{
    if (!machine || !size) return nullptr;
    const auto image = machine->iwm.export_image();
    *size = image.size();
    return image.empty() ? nullptr : image.data();
}

bool mac128k_read_floppy_write_state(const mac128k *machine, mac128k_floppy_write_state *state)
{
    if (!machine || !state) return false;
    const auto write = machine->iwm.write_state();
    *state = {write.enabled, write.error, write.gate_open, write.pending_bytes, write.accepted_bytes,
              write.committed_sectors, write.rejected_records};
    return true;
}

bool mac128k_watch_ram_steps(mac128k *machine, uint32_t address, uint32_t length, uint32_t target)
{
    if (!machine || !target || !length || !contains(0, MAC128K_RAM_SIZE, address, length)) return false;
    machine->ram_steps = {address, length, target, 0, 0, 0, 0};
    return true;
}

const mac128k_ram_steps *mac128k_get_ram_steps(const mac128k *machine)
{
    return machine && machine->ram_steps.target ? &machine->ram_steps : nullptr;
}

void mac128k_run(mac128k *machine, uint32_t cycles)
{
    auto &steps = machine->ram_steps;
    if (machine->fault_operation || (steps.target && steps.returned == steps.target)) {
        return;
    }
    const auto start_cycle = machine->cpu.getClock();
    try {
        while (machine->cpu.getClock() - start_cycle < cycles) {
            const uint32_t instruction = machine->cpu.getPC();
            const bool count_step = steps.target && instruction >= steps.address &&
                instruction - steps.address < steps.length && !machine->overlay();
            if (count_step) {
                if (!steps.started) steps.first_pc = instruction;
                steps.last_pc = instruction;
                ++steps.started;
            }
            if (machine->witness && machine->witness_detail) machine->observe_cpu_mask(instruction);
            if (machine->witness && machine->witness_detail && machine->witness_cpu)
                machine->observe(MAC128K_WITNESS_CPU_STEP,
                    static_cast<uint64_t>(machine->cpu.getClock()), instruction, 0,
                    static_cast<uint8_t>((machine->cpu.getSR() >> 8) & 7));
            machine->cpu.execute();
            if (machine->witness && machine->witness_detail) machine->observe_cpu_mask(instruction);
            if (machine->cpu.isHalted()) {
                machine->fault(machine->cpu.getPC(), "halt");
            }
            if (count_step && ++steps.returned == steps.target) break;
        }
    } catch (const AccessFault &) {
        // The faulting bus access has already been recorded.
    }
}

mac128k_state mac128k_get_state(const mac128k *machine)
{
    return {static_cast<uint64_t>(machine->cpu.getClock()), machine->vbl,
            machine->cpu.getPC(), machine->fault_address, machine->fault_operation};
}

bool mac128k_read_cpu_state(const mac128k *machine, mac128k_cpu_state *state)
{
    if (!machine || !state) return false;
    for (int i = 0; i < 8; ++i) {
        state->d[i] = machine->cpu.getD(i);
        state->a[i] = machine->cpu.getA(i);
    }
    state->pc = machine->cpu.getPC();
    state->sr = machine->cpu.getSR();
    return true;
}

bool mac128k_read_floppy_state(const mac128k *machine, mac128k_floppy_state *state)
{
    if (!machine || !state) return false;
    const auto drive = machine->iwm.state(static_cast<uint64_t>(machine->cpu.getClock()));
    *state = {drive.media_present, drive.write_protected, drive.external_selected,
              drive.controller_enabled, drive.motor_on, drive.disk_changed, drive.step_outward,
              machine->floppy_select_high(), drive.track, drive.control, drive.mode,
              drive.step_commands, drive.nibbles_delivered, drive.disabled_data_reads,
              drive.not_ready_data_reads, drive.ignored_data_writes, drive.eject_commands};
    return true;
}

bool mac128k_inject_floppy_bad_checksum(mac128k *machine, unsigned track, unsigned sector)
{
    return mac128k_configure_floppy_bad_checksum(machine, track, sector, false);
}

bool mac128k_configure_floppy_bad_checksum(mac128k *machine, unsigned track, unsigned sector, bool once)
{
    return machine && machine->iwm.inject_bad_checksum(track, sector, once);
}

bool mac128k_read_floppy_checksum_fault(const mac128k *machine, mac128k_floppy_checksum_fault *state)
{
    if (!machine || !state) return false;
    const auto fault = machine->iwm.checksum_fault();
    *state = {fault.enabled, fault.track, fault.sector, fault.altered_nibbles, fault.once};
    return true;
}

void mac128k_set_mouse_button(mac128k *machine, bool pressed)
{
    if (machine->mouse_pressed == pressed) {
        return;
    }
    machine->mouse_pressed = pressed;
    if (machine->witness) {
        machine->observe(MAC128K_WITNESS_BUTTON, static_cast<uint64_t>(machine->cpu.getClock()),
                         0, 0, pressed);
    }
}

void mac128k_set_mouse_phase(mac128k *machine, uint8_t x_phase, uint8_t y_phase)
{
    const bool changed = machine->scc[scc_channel_a].phase != x_phase ||
                         machine->scc[scc_channel_b].phase != y_phase;
    machine->scc[scc_channel_a].set_phase(x_phase);
    machine->scc[scc_channel_b].set_phase(y_phase);
    machine->update_irq();
    if (changed && machine->witness) {
        machine->observe(MAC128K_WITNESS_PHASE, static_cast<uint64_t>(machine->cpu.getClock()));
    }
}

bool mac128k_start_mouse_witness(mac128k *machine, uint64_t first_cycle, uint64_t last_cycle)
{
    if (first_cycle > last_cycle) {
        return false;
    }
    if (!machine->witness) {
        machine->witness.reset(new (std::nothrow) mac128k_mouse_witness_event[MAC128K_MOUSE_WITNESS_CAPACITY]);
        if (!machine->witness) {
            return false;
        }
    }
    machine->witness_first = first_cycle;
    machine->witness_last = last_cycle;
    machine->witness_count = 0;
    machine->witness_cpu_mask = 8;
    machine->witness_cpu = false;
    machine->witness_dropped = 0;
    machine->witness_ram_first = machine->witness_ram_length = 0;
    machine->witness_ram_matched = machine->witness_ram_dropped = 0;
    machine->observe(MAC128K_WITNESS_SNAPSHOT, static_cast<uint64_t>(machine->cpu.getClock()));
    return true;
}

void mac128k_mouse_witness_detailed(mac128k *machine, bool enabled)
{
    machine->witness_detail = enabled;
}

bool mac128k_mouse_witness_cpu(mac128k *machine, bool enabled)
{
    if (!machine || !machine->witness || !machine->witness_detail) return false;
    machine->witness_cpu = enabled;
    return true;
}

bool mac128k_mouse_witness_memory(mac128k *machine, uint32_t first, uint32_t length)
{
    if (!machine->witness || !machine->witness_detail || !length || first >= MAC128K_RAM_SIZE ||
        length > MAC128K_RAM_SIZE - first) return false;
    machine->witness_ram_first = first;
    machine->witness_ram_length = length;
    machine->witness_ram_matched = machine->witness_ram_dropped = 0;
    return true;
}

void mac128k_mouse_witness_memory_stats(const mac128k *machine, uint64_t *matched, uint64_t *dropped)
{
    *matched = machine->witness_ram_matched;
    *dropped = machine->witness_ram_dropped;
}

void mac128k_mouse_witness_pending(mac128k *machine, uint32_t pending)
{
    if (machine->witness_detail) machine->witness_pending = pending;
}

void mac128k_mouse_witness_input(mac128k *machine, enum mac128k_mouse_witness_kind kind,
                                 uint8_t source, int32_t dx, int32_t dy, uint8_t value,
                                 uint32_t pending, uint64_t due, uint64_t deadline)
{
    if (!machine->witness || !machine->witness_detail) return;
    const size_t before = machine->witness_count;
    machine->observe(kind, static_cast<uint64_t>(machine->cpu.getClock()), 0, 0, value);
    if (machine->witness_count == before) return;
    auto &event = machine->witness[before];
    event.source = source; event.dx = dx; event.dy = dy; event.pending = pending;
    event.due = due; event.deadline = deadline;
}

const mac128k_mouse_witness_event *mac128k_mouse_witness(const mac128k *machine, size_t *count, uint64_t *dropped)
{
    *count = machine->witness_count;
    *dropped = machine->witness_dropped;
    return machine->witness.get();
}

bool mac128k_inject_key(mac128k *machine, uint8_t code, bool pressed)
{
    static_assert(MAC128K_MAX_KEY_CODE == MacKeyboard::max_scan_code);
    bool accepted=machine->keyboard.queue_key(code, pressed, machine->cpu.getClock());
    if(!accepted) ++machine->keyboard_rejected;
    return accepted;
}
bool mac128k_keyboard_fault(mac128k *machine,uint8_t command,uint32_t occurrence,uint8_t kind,uint32_t argument)
{
    return machine && machine->keyboard.configure_fault(command,occurrence,kind,argument);
}
bool mac128k_keyboard_fault_status(const mac128k *machine,uint64_t *cycle)
{
    *cycle=machine->keyboard.fault_cycle();return machine->keyboard.fault_triggered();
}

bool mac128k_start_keyboard_witness(mac128k *machine,uint64_t first,uint64_t last)
{
    if(!machine || first>last) return false;
    auto rows=std::unique_ptr<mac128k_keyboard_event[]>(new(std::nothrow) mac128k_keyboard_event[8192]);
    if(!rows) return false;
    machine->keyboard_witness=std::move(rows);machine->keyboard_witness_count=0;
    machine->keyboard_witness_limit=8192;
    machine->keyboard_witness_dropped=0;machine->keyboard_rejected=0;
    machine->keyboard_witness_first=first;machine->keyboard_witness_last=last;
    machine->keyboard.set_trace(&mac128k::keyboard_trace,machine);return true;
}
bool mac128k_keyboard_witness_limit(mac128k *machine,uint32_t limit)
{
    if(!machine || !machine->keyboard_witness || !limit || limit>8192 || machine->keyboard_witness_count>limit) return false;
    machine->keyboard_witness_limit=limit;return true;
}
void mac128k_keyboard_input_due(mac128k *machine,uint64_t due,uint8_t code,bool pressed)
{
    const uint8_t value=static_cast<uint8_t>((code<<1)|1|(pressed?0:0x80));
    machine->keyboard_observe(machine->cpu.getClock(),9,value,static_cast<uint8_t>(machine->keyboard.queue_size()),due);
}
const mac128k_keyboard_event *mac128k_keyboard_witness(const mac128k *machine,size_t *count,uint64_t *dropped,uint64_t *rejected)
{
    *count=machine->keyboard_witness_count;*dropped=machine->keyboard_witness_dropped;*rejected=machine->keyboard_rejected;
    return machine->keyboard_witness.get();
}
bool mac128k_start_display_witness(mac128k *machine,uint64_t first,uint64_t last,uint32_t address,uint32_t length)
{
    if(!machine || first>last || address>MAC128K_RAM_SIZE || length>MAC128K_RAM_SIZE-address) return false;
    auto rows=std::unique_ptr<mac128k_display_event[]>(new(std::nothrow) mac128k_display_event[8192]);
    if(!rows) return false;
    machine->display_witness=std::move(rows);machine->display_first=first;machine->display_last=last;
    machine->display_ram_first=address;machine->display_ram_length=length;machine->display_witness_count=0;machine->display_dropped=0;return true;
}
const mac128k_display_event *mac128k_display_witness(const mac128k *machine,size_t *count,uint64_t *dropped)
{
    *count=machine->display_witness_count;*dropped=machine->display_dropped;return machine->display_witness.get();
}

bool mac128k_read_ram(const mac128k *machine, uint32_t address, uint8_t *data, size_t size)
{
    if (!contains(0, MAC128K_RAM_SIZE, address, size) || (size != 0 && data == nullptr)) {
        return false;
    }
    if (size != 0) {
        std::memcpy(data, machine->ram.data() + address, size);
    }
    return true;
}

const uint8_t *mac128k_frame(const mac128k *machine)
{
    const auto pins = port_pins(machine->port_a, machine->direction_a);
    return machine->ram.data() + ((pins & main_frame_select) != 0 ? main_frame_base : alternate_frame_base);
}
