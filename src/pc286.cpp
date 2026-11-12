#include "pc286.h"
#include "ega.h"
#include "../third_party/mame-i286/i286.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>

using boothush_i286::FirmwareRegisters;

namespace {

constexpr uint32_t rom_base = 0xf0000;
constexpr uint32_t reset_rom_base = 0xff0000;
constexpr uint32_t text_base = 0xb8000;
constexpr uint16_t firmware_entries = 0x0200;
constexpr uint16_t carry = 1;
constexpr unsigned sector_size = 512;
constexpr unsigned sectors_per_track = 15;
constexpr unsigned cylinders = 80;
constexpr unsigned heads = 2;

constexpr uint8_t reset_jump[] = {0xea, 0x00, 0x00, 0x00, 0xf0};
constexpr uint8_t floppy_bootstrap[] = {
    0xfa,                    // cli
    0x31, 0xc0,              // xor ax, ax
    0x8e, 0xd8,              // mov ds, ax
    0x8e, 0xc0,              // mov es, ax
    0x8e, 0xd0,              // mov ss, ax
    0xbc, 0x00, 0x7c,        // mov sp, 7c00h
    0xfb,                    // sti
    0xbb, 0x00, 0x7c,        // mov bx, 7c00h
    0xb8, 0x01, 0x02,        // mov ax, 0201h
    0xb9, 0x01, 0x00,        // mov cx, 0001h
    0x31, 0xd2,              // xor dx, dx
    0xcd, 0x13,              // int 13h
    0x72, 0x0d,              // jc halt
    0x81, 0x3e, 0xfe, 0x7d, 0x55, 0xaa, // cmp word [7dfeh], aa55h
    0x75, 0x05,              // jne halt
    0xea, 0x00, 0x7c, 0x00, 0x00, // jmp 0000h:7c00h
    0xf4,                    // halt: hlt
};

uint16_t read_word(const uint8_t *bytes)
{
    return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
}

void write_word(uint8_t *bytes, uint16_t value)
{
    bytes[0] = uint8_t(value);
    bytes[1] = uint8_t(value >> 8);
}

void set_carry(FirmwareRegisters &regs, bool set)
{
    regs.flags = uint16_t((regs.flags & ~carry) | (set ? carry : 0));
}

} // namespace

struct pc286 final : boothush_i286::Bus {
    std::array<uint8_t, PC286_RAM_SIZE> ram{};
    std::array<uint8_t, PC286_ROM_SIZE> rom{};
    std::unique_ptr<uint8_t[]> floppy;
    Ega ega;
    boothush_i286::i80286_cpu_device cpu{*this};
    uint64_t cycles = 0;
    const char *fault = nullptr;
    bool post_written = false;
    uint8_t post = 0;

    std::array<uint8_t, 128> cmos{};
    uint8_t cmos_index = 0;
    uint8_t kbc_command_byte = 0;
    uint8_t kbc_output_port = 3;
    uint8_t kbc_reply = 0;
    bool kbc_reply_ready = false;
    bool kbc_self_tested = false;
    uint8_t kbc_next_write = 0;
    uint8_t floppy_status = 0;

    pc286()
    {
        // Fixed RTC values make boot diagnostics repeatable.
        cmos[0x06] = 1;
        cmos[0x07] = 1;
        cmos[0x08] = 1;
        cmos[0x09] = 0x84;
        cmos[0x0a] = 0x26;
        cmos[0x0b] = 2;
        cmos[0x0d] = 0x80;
        write_word(cmos.data() + 0x15, PC286_RAM_SIZE / 1024);
        cmos[0x32] = 0x19;
        update_cmos_checksum();
    }

    void update_cmos_checksum()
    {
        uint16_t sum = 0;
        for (unsigned reg = 0x10; reg <= 0x2d; ++reg) {
            sum = uint16_t(sum + cmos[reg]);
        }
        cmos[0x2e] = uint8_t(sum >> 8);
        cmos[0x2f] = uint8_t(sum);
    }

    void initialize_floppy_firmware()
    {
        rom.fill(0xff);
        std::copy_n(reset_jump, sizeof(reset_jump), rom.data() + PC286_ROM_SIZE - 16);
        std::copy_n(floppy_bootstrap, sizeof(floppy_bootstrap), rom.data());
        for (const uint8_t number : {0x10, 0x11, 0x12, 0x13}) {
            uint8_t *const vector = ram.data() + 4 * number;
            write_word(vector, uint16_t(firmware_entries + number));
            write_word(vector + 2, uint16_t(rom_base >> 4));
            rom[firmware_entries + number] = 0xcf; // IRET fallback.
        }

        cmos[0x10] = 0x20; // One 1.2 MB diskette drive at A:.
        cmos[0x14] = 1;
        update_cmos_checksum();
        write_word(ram.data() + 0x410, 1);
        write_word(ram.data() + 0x413, PC286_RAM_SIZE / 1024);
        set_video_mode(3);

        if (read_word(floppy.get() + sector_size - 2) != 0xaa55) {
            fault = "missing_boot_signature";
        }
    }

    uint8_t read_memory(uint32_t address) override
    {
        if (address < ram.size()) {
            return ram[address];
        }
        if (address >= rom_base && address < rom_base + PC286_ROM_SIZE) {
            return rom[address - rom_base];
        }
        if (address >= reset_rom_base && address < reset_rom_base + PC286_ROM_SIZE) {
            return rom[address - reset_rom_base];
        }
        return ega.handles_memory(address) ? ega.read_memory(address) : 0xff;
    }

    void write_memory(uint32_t address, uint8_t value) override
    {
        if (address < ram.size()) {
            ram[address] = value;
        } else if (ega.handles_memory(address)) {
            ega.write_memory(address, value);
        }
    }

    uint8_t read_port(uint16_t port) override
    {
        switch (port) {
        case 0x60:
            if (kbc_reply_ready) {
                kbc_reply_ready = false;
                return kbc_reply;
            }
            return 0xff;
        case 0x64:
            return uint8_t((kbc_self_tested ? 4 : 0) | (kbc_reply_ready ? 1 : 0));
        case 0x71:
            return cmos[cmos_index];
        default:
            if (port == 0x3ba || port == 0x3da) {
                ega.set_cycles(cycles);
            }
            return ega.handles_port(port) ? ega.read_port(port) : 0xff;
        }
    }

    void set_keyboard_reply(uint8_t reply)
    {
        kbc_reply = reply;
        kbc_reply_ready = true;
    }

    void keyboard_command(uint8_t command)
    {
        switch (command) {
        case 0x20:
            set_keyboard_reply(kbc_command_byte);
            break;
        case 0x60:
        case 0xd1:
            kbc_next_write = command;
            break;
        case 0xaa:
            kbc_self_tested = true;
            set_keyboard_reply(0x55);
            break;
        case 0xab:
            set_keyboard_reply(0);
            break;
        case 0xad:
            kbc_command_byte |= 0x10;
            break;
        case 0xae:
            kbc_command_byte &= ~0x10;
            break;
        case 0xd0:
            set_keyboard_reply(kbc_output_port);
            break;
        }
    }

    void write_port(uint16_t port, uint8_t value) override
    {
        switch (port) {
        case 0x60:
            if (kbc_next_write == 0x60) {
                kbc_command_byte = value;
            } else if (kbc_next_write == 0xd1) {
                kbc_output_port = value;
                cpu.set_a20((value & 2) != 0);
            }
            kbc_next_write = 0;
            break;
        case 0x64:
            keyboard_command(value);
            break;
        case 0x70:
            cmos_index = value & 0x7f;
            break;
        case 0x71:
            if (cmos_index != 0x0c && cmos_index != 0x0d) {
                cmos[cmos_index] = value;
            }
            break;
        case 0x80:
            post_written = true;
            post = value;
            break;
        default:
            if (ega.handles_port(port)) {
                ega.write_port(port, value);
            }
            break;
        }
    }

    void disk_status(FirmwareRegisters &regs, uint8_t status, uint8_t count)
    {
        floppy_status = status;
        ram[0x441] = status;
        regs.ax = uint16_t((uint16_t(status) << 8) | count);
        set_carry(regs, status != 0);
    }

    void read_disk(FirmwareRegisters &regs)
    {
        const unsigned count = regs.ax & 0xff;
        const unsigned cylinder = (regs.cx >> 8) | ((regs.cx & 0xc0) << 2);
        const unsigned sector = regs.cx & 0x3f;
        const unsigned head = regs.dx >> 8;
        if (count == 0 || sector == 0 || sector > sectors_per_track ||
            count > sectors_per_track - sector + 1 ||
            cylinder >= cylinders || head >= heads) {
            disk_status(regs, 4, 0);
            return;
        }

        const uint32_t destination = (uint32_t(regs.es) << 4) + regs.bx;
        const unsigned length = count * sector_size;
        if ((destination & 0xffff) + length > 0x10000 ||
            destination > ram.size() || length > ram.size() - destination) {
            disk_status(regs, 9, 0);
            return;
        }

        const unsigned offset =
            ((cylinder * heads + head) * sectors_per_track + sector - 1) * sector_size;
        std::memcpy(ram.data() + destination, floppy.get() + offset, length);
        disk_status(regs, 0, uint8_t(count));
    }

    void disk_service(FirmwareRegisters &regs)
    {
        if ((regs.dx & 0xff) != 0) {
            disk_status(regs, 1, 0);
            return;
        }
        switch (regs.ax >> 8) {
        case 0x00:
            disk_status(regs, 0, 0);
            break;
        case 0x01:
            disk_status(regs, floppy_status, 0);
            break;
        case 0x02:
            read_disk(regs);
            break;
        case 0x03:
        case 0x05:
            disk_status(regs, 3, 0); // The caller's image is write protected.
            break;
        case 0x08:
            regs.bx = uint16_t((regs.bx & 0xff00) | 2);
            regs.cx = uint16_t(((cylinders - 1) << 8) | sectors_per_track);
            regs.dx = uint16_t(((heads - 1) << 8) | 1);
            disk_status(regs, 0, 0);
            break;
        default:
            disk_status(regs, 1, 0);
            break;
        }
    }

    bool set_video_mode(uint8_t mode)
    {
        if (!ega.set_bios_mode(mode)) {
            return false;
        }
        ram[0x449] = mode & 0x7f;
        write_word(ram.data() + 0x44a, 80);
        write_word(ram.data() + 0x450, 0);
        ram[0x462] = 0;
        ram[0x484] = 24;
        return true;
    }

    void teletype(uint8_t character)
    {
        unsigned column = ram[0x450];
        unsigned row = ram[0x451];
        if (column >= 80 || row >= 25) {
            column = row = 0;
        }

        switch (character) {
        case '\a':
            break;
        case '\b':
            if (column != 0) {
                --column;
            }
            break;
        case '\r':
            column = 0;
            break;
        case '\n':
            ++row;
            break;
        default:
            ega.write_memory(text_base + 2 * (row * 80 + column), character);
            if (++column == 80) {
                column = 0;
                ++row;
            }
            break;
        }

        if (row == 25) {
            for (unsigned index = 0; index < 2 * 80 * 24; ++index) {
                ega.write_memory(text_base + index, ega.peek_memory(text_base + 2 * 80 + index));
            }
            for (unsigned index = 0; index < 80; ++index) {
                const uint32_t address = text_base + 2 * (24 * 80 + index);
                ega.write_memory(address, ' ');
                ega.write_memory(address + 1, 7);
            }
            --row;
        }
        ram[0x450] = uint8_t(column);
        ram[0x451] = uint8_t(row);
    }

    void video_service(FirmwareRegisters &regs)
    {
        bool handled = false;
        switch (regs.ax >> 8) {
        case 0x00:
            handled = set_video_mode(uint8_t(regs.ax));
            break;
        case 0x0e:
            handled = ram[0x449] == 3;
            if (handled) {
                teletype(uint8_t(regs.ax));
            }
            break;
        case 0x0f:
            regs.ax = uint16_t((80 << 8) | ram[0x449]);
            regs.bx &= 0x00ff;
            handled = true;
            break;
        }
        if (!handled) {
            regs.ax = uint16_t(0x8600 | (regs.ax & 0xff));
        }
        set_carry(regs, !handled);
    }

    bool firmware_interrupt(uint8_t number, FirmwareRegisters &regs) override
    {
        if (!floppy) {
            return false;
        }

        // Let guests replace BIOS services by writing their own vectors.
        const uint8_t *const vector = ram.data() + 4 * number;
        if (read_word(vector) != firmware_entries + number ||
            read_word(vector + 2) != rom_base >> 4) {
            return false;
        }

        switch (number) {
        case 0x10:
            video_service(regs);
            return true;
        case 0x11:
            regs.ax = read_word(ram.data() + 0x410);
            return true;
        case 0x12:
            regs.ax = read_word(ram.data() + 0x413);
            return true;
        case 0x13:
            disk_service(regs);
            return true;
        default:
            return false;
        }
    }
};

pc286 *pc286_create(const uint8_t *rom, size_t size)
{
    if (rom == nullptr || size != PC286_ROM_SIZE) {
        return nullptr;
    }
    auto machine = std::unique_ptr<pc286>(new (std::nothrow) pc286);
    if (machine) {
        std::memcpy(machine->rom.data(), rom, size);
    }
    return machine.release();
}

pc286 *pc286_create_floppy(const uint8_t *image, size_t size)
{
    if (image == nullptr || size != PC286_FLOPPY_SIZE) {
        return nullptr;
    }
    auto machine = std::unique_ptr<pc286>(new (std::nothrow) pc286);
    if (!machine) {
        return nullptr;
    }
    machine->floppy.reset(new (std::nothrow) uint8_t[size]);
    if (!machine->floppy) {
        return nullptr;
    }
    std::memcpy(machine->floppy.get(), image, size);
    machine->initialize_floppy_firmware();
    return machine.release();
}

void pc286_destroy(pc286 *machine)
{
    delete machine;
}

void pc286_run(pc286 *machine, uint32_t cycles)
{
    uint64_t advanced = 0;
    while (advanced < cycles && machine->fault == nullptr &&
           !machine->cpu.halted() && !machine->cpu.shutdown()) {
        // Keep device time and REP scheduling independent of the host's poll slice.
        const uint32_t consumed = machine->cpu.run(1);
        if (consumed == 0) {
            machine->fault = "cpu_no_progress";
            break;
        }
        advanced += consumed;
        machine->cycles += consumed;
    }
}

pc286_state pc286_get_state(const pc286 *machine)
{
    pc286_state state{};
    state.cycles = machine->cycles;
    state.pc = machine->cpu.pc();
    state.cs = machine->cpu.cs();
    state.ip = machine->cpu.ip();
    state.last_exception = machine->cpu.last_exception();
    state.post = machine->post;
    state.post_written = machine->post_written;
    state.halted = machine->cpu.halted();
    if (machine->fault != nullptr) {
        state.fault_operation = machine->fault;
        state.fault_address = state.pc;
    } else if (machine->cpu.shutdown()) {
        state.fault_operation = "cpu_shutdown";
        state.fault_address = machine->cpu.fault_pc();
    }
    return state;
}

bool pc286_read_ram(const pc286 *machine, uint32_t address, uint8_t *data, size_t size)
{
    if ((data == nullptr && size != 0) ||
        address > PC286_VIDEO_END || size > PC286_VIDEO_END - address) {
        return false;
    }
    for (size_t index = 0; index < size; ++index, ++address) {
        data[index] = address < machine->ram.size()
            ? machine->ram[address] : machine->ega.peek_memory(address);
    }
    return true;
}

void pc286_frame_rgb(const pc286 *machine, uint8_t data[PC286_FRAME_SIZE])
{
    static_assert(PC286_FRAME_WIDTH == Ega::width);
    static_assert(PC286_FRAME_HEIGHT == Ega::height);
    static_assert(PC286_FRAME_SIZE == Ega::frame_size);
    machine->ega.frame_rgb(data);
}
