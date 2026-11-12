#include "mac128k.h"

#include "Moira.h"

#include <array>
#include <cstring>
#include <memory>
#include <new>

namespace {

// Macintosh 128K startup and display wiring: ClasSICK 0.97 SPEC-0018.
constexpr uint32_t rom_base = 0x400000;
constexpr uint32_t overlay_ram_base = 0x600000;
constexpr uint32_t main_frame_base = 0x1a700;
constexpr uint32_t alternate_frame_base = 0x12700;
constexpr uint32_t via_base = 0xefe1fe;
constexpr uint32_t via_register_stride = 0x200;
constexpr uint32_t via_register_count = 16;
constexpr uint8_t overlay_select = 0x10;
constexpr uint8_t main_frame_select = 0x40;
constexpr uint8_t mouse_button = 0x08;
constexpr uint8_t mouse_x_phase = 0x10;
constexpr uint8_t mouse_y_phase = 0x20;
constexpr uint8_t via_ca1_flag = 0x02;
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
    Cpu cpu{*this};

    bool overlay() const
    {
        return (port_pins(port_a, direction_a) & overlay_select) != 0;
    }

    void update_irq()
    {
        const uint8_t via_irq = (interrupt_flags & interrupt_enable) != 0 ? 1 : 0;
        const uint8_t scc_irq = scc_irq_enabled && scc_pending() != 0 ? 2 : 0;
        cpu.setIPL(via_irq | scc_irq);
    }

    uint8_t scc_pending() const
    {
        return (scc[scc_channel_a].irq_pending() ? scc_ext_pending_a : 0) |
               (scc[scc_channel_b].irq_pending() ? scc_ext_pending_b : 0);
    }

    void tick(uint64_t cycle)
    {
        if (cycle >= next_vbl_cycle) {
            const uint64_t elapsed = (cycle - next_vbl_cycle) / cycles_per_vbl + 1;
            vbl += elapsed;
            next_vbl_cycle += elapsed * cycles_per_vbl;
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
        throw AccessFault{};
    }

    uint8_t *writable_memory(uint32_t address, size_t size)
    {
        const uint32_t base = overlay() ? overlay_ram_base : 0;
        if (contains(base, MAC128K_RAM_SIZE, address, size)) {
            return ram.data() + (address - base);
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
            return port_pins(port_b, direction_b, inputs);
        }
        case 1:
            clear_interrupts(via_ca1_flag);
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
        case 11:
            return auxiliary_control;
        case 12:
            return 0;
        case 13:
            return interrupt_flags | ((interrupt_flags & interrupt_enable) != 0 ? via_irq_bit : 0);
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
            return;
        case 1:
            clear_interrupts(via_ca1_flag);
            port_a = value;
            return;
        case 2:
            direction_b = value;
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
        case 11:
            if ((value & ~via_t1_continuous) != 0) {
                fault(address, "write8");
            }
            auxiliary_control = value;
            return;
        case 12:
            if (value != 0) {
                fault(address, "write8");
            }
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
        if (size == 1 && (address == scc_read_a || address == scc_read_b)) {
            return read_scc(address);
        }
        const uint32_t via_offset = address - via_base;
        if (size == 1 && via_offset < via_register_count * via_register_stride &&
            via_offset % via_register_stride == 0) {
            return read_via(via_offset / via_register_stride, address);
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
        if (size == 1 && (address == scc_write_a || address == scc_write_b)) {
            write_scc(address, static_cast<uint8_t>(value));
            update_irq();
            return;
        }
        const uint32_t via_offset = address - via_base;
        if (size == 1 && via_offset < via_register_count * via_register_stride &&
            via_offset % via_register_stride == 0) {
            write_via(via_offset / via_register_stride, address, static_cast<uint8_t>(value));
            return;
        }
        if (uint8_t *bytes = writable_memory(address, size)) {
            for (size_t i = size; i != 0; --i) {
                bytes[i - 1] = static_cast<uint8_t>(value);
                value >>= 8;
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

void mac128k_run(mac128k *machine, uint32_t cycles)
{
    if (machine->fault_operation) {
        return;
    }
    const auto start_cycle = machine->cpu.getClock();
    try {
        while (machine->cpu.getClock() - start_cycle < cycles) {
            machine->cpu.execute();
            if (machine->cpu.isHalted()) {
                machine->fault(machine->cpu.getPC(), "halt");
            }
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

void mac128k_set_mouse_button(mac128k *machine, bool pressed)
{
    machine->mouse_pressed = pressed;
}

void mac128k_set_mouse_phase(mac128k *machine, uint8_t x_phase, uint8_t y_phase)
{
    machine->scc[scc_channel_a].set_phase(x_phase);
    machine->scc[scc_channel_b].set_phase(y_phase);
    machine->update_irq();
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
