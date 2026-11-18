#include "pc286.h"
#include "ega.h"
#include "pc_at_devices.h"
#include "pcat_floppy.h"
#include "pcat_hard_disk.h"
#include "pcat_keyboard.h"
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
constexpr uint32_t ega_rom_base = 0xc0000;
constexpr uint32_t text_base = 0xb8000;
constexpr uint16_t firmware_entries = 0x0200;
constexpr uint16_t timer_entry = 0x0300;
constexpr uint16_t timer_hook_entry = 0x0310;
constexpr uint16_t carry = 1;
constexpr uint16_t zero = 0x40;
constexpr unsigned sector_size = 512;
constexpr unsigned sectors_per_track = 15;
constexpr unsigned cylinders = 80;
constexpr unsigned heads = 2;
constexpr uint64_t pit_rate = 1193182;
constexpr uint64_t bios_tick_period = UINT64_C(6000000) * 65536;
constexpr uint32_t bios_ticks_per_day = 0x1800b0;

constexpr uint8_t reset_jump[] = {0xea, 0x00, 0x00, 0x00, 0xf0};
constexpr uint8_t timer_handler[] = {
    0x50, 0xcd, 0x1c,        // push ax; int 1ch
    0xb0, 0x20, 0xe6, 0x20,  // mov al,20h; out 20h,al (EOI)
    0x58, 0xcf,              // pop ax; iret
};
constexpr size_t boot_drive_offset = 23;
constexpr uint8_t bootstrap[] = {
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
    0xba, 0x00, 0x00,        // mov dx, boot drive (00h or 80h)
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

uint32_t read_dword(const uint8_t *bytes)
{
    return uint32_t(read_word(bytes)) | (uint32_t(read_word(bytes + 2)) << 16);
}

void write_dword(uint8_t *bytes, uint32_t value)
{
    write_word(bytes, uint16_t(value));
    write_word(bytes + 2, uint16_t(value >> 16));
}

void set_carry(FirmwareRegisters &regs, bool set)
{
    regs.flags = uint16_t((regs.flags & ~carry) | (set ? carry : 0));
}

uint8_t keyboard_ascii(uint8_t scan, uint8_t flags, bool extended)
{
    constexpr char normal[] =
        "\0\x1b" "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'\x60\0\\zxcvbnm,./\0*\0 ";
    constexpr char shifted[] =
        "\0\x1b" "!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    static_assert(sizeof(normal) == 59 && sizeof(shifted) == 59);
    if (flags & 8) {
        return 0;
    }
    if (extended) {
        return scan == 0x1c ? '\r' : scan == 0x35 ? '/' : 0;
    }
    if (scan >= sizeof(normal) - 1) {
        return 0;
    }
    const uint8_t base = normal[scan];
    const bool letter = base >= 'a' && base <= 'z';
    if (letter && (flags & 4)) {
        return uint8_t(base - 'a' + 1);
    }
    const bool use_shift = ((flags & 3) != 0) != (letter && (flags & 0x40));
    return use_shift ? shifted[scan] : base;
}

} // namespace

struct pc286 final : boothush_i286::Bus {
    std::array<uint8_t, PC286_RAM_SIZE> ram{};
    std::array<uint8_t, PC286_ROM_SIZE> rom{};
    std::array<uint8_t, PC286_EGA_ROM_MAX> ega_rom{};
    size_t ega_rom_size = 0;
    std::unique_ptr<uint8_t[]> floppy;
    std::unique_ptr<uint8_t[]> hard_disk;
    Ega ega;
    PcAtDevices devices;
    PcatFloppy fdc{ram};
    PcatHardDisk hdc;
    PcatKeyboard keyboard;
    boothush_i286::i80286_cpu_device cpu{*this};
    uint64_t cycles = 0;
    std::unique_ptr<pc286_witness_event[]> witness;
    size_t witness_count = 0;
    uint64_t witness_dropped = 0;
    uint32_t witness_mem_start = 0, witness_mem_end = 0;
    uint32_t witness_io_start = 0, witness_io_end = 0;
    uint64_t witness_first = 0, witness_last = 0;
    uint32_t witness_pc = 0;
    uint16_t witness_cs = 0, witness_ip = 0;

    void observe(uint8_t kind, uint32_t address, uint32_t value, uint8_t width)
    {
        if (!witness || cycles < witness_first || cycles > witness_last) return;
        if (witness_count == PC286_WITNESS_CAPACITY) { ++witness_dropped; return; }
        witness[witness_count++] = {cycles, witness_pc, address, value, witness_cs, witness_ip, kind, width};
    }
    const char *fault = nullptr;
    bool post_written = false;
    uint8_t post = 0;
    bool built_in_firmware = false;
    bool floppy_writes_enabled = false;
    bool read_failure_enabled = false;
    uint8_t read_failure_cylinder = 0, read_failure_head = 0, read_failure_sector = 0;
    uint64_t read_failures = 0;
    bool hard_read_failure_enabled = false;
    bool hard_read_failure_once = false;
    uint32_t hard_read_failure_at = 0;
    uint64_t hard_read_matches = 0;
    uint16_t hard_read_failure_cylinder = 0;
    uint8_t hard_read_failure_head = 0, hard_read_failure_sector = 0;
    uint64_t hard_read_failures = 0;
    bool hard_write_failure_enabled = false;
    uint32_t hard_write_failure_at = 0;
    uint64_t hard_write_matches = 0;
    uint16_t hard_write_failure_cylinder = 0;
    uint8_t hard_write_failure_head = 0, hard_write_failure_sector = 0;
    uint64_t hard_write_failures = 0;
    uint32_t hard_reset_failure_at = 0;
    uint64_t hard_reset_matches = 0, hard_reset_failures = 0;
    bool future_input = false;
    bool extended_scan = false;
    unsigned pause_bytes = 0;
    uint64_t bios_ticks_elapsed = 0;
    uint64_t next_bios_tick = (bios_tick_period + pit_rate - 1) / pit_rate;

    void set_firmware_vector(uint8_t number, uint16_t offset)
    {
        uint8_t *const vector = ram.data() + 4 * number;
        write_word(vector, offset);
        write_word(vector + 2, uint16_t(rom_base >> 4));
    }

    void initialize_firmware()
    {
        built_in_firmware = true;
        rom.fill(0xff);
        std::copy_n(reset_jump, sizeof(reset_jump), rom.data() + PC286_ROM_SIZE - 16);
        std::copy_n(bootstrap, sizeof(bootstrap), rom.data());
        for (const uint8_t number : {0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x1a}) {
            set_firmware_vector(number, uint16_t(firmware_entries + number));
            rom[firmware_entries + number] = 0xcf; // IRET fallback.
        }
        set_firmware_vector(0x08, timer_entry);
        set_firmware_vector(0x1c, timer_hook_entry);
        std::copy_n(timer_handler, sizeof(timer_handler), rom.data() + timer_entry);
        rom[timer_hook_entry] = 0xcf;
        devices.write_port(0x20, 0x11);
        devices.write_port(0x21, 0x08);
        devices.write_port(0x21, 0x04);
        devices.write_port(0x21, 0x01);
        devices.write_port(0x21, 0xfe); // BIOS IRQ0 timer enabled.
        devices.write_port(0x43, 0x36);
        devices.write_port(0x40, 0);
        devices.write_port(0x40, 0); // PIT0 divisor 65,536 (18.2 Hz).

        write_word(ram.data() + 0x410, floppy ? 1 : 0);
        write_word(ram.data() + 0x413, PC286_RAM_SIZE / 1024);
        ram[0x475] = hard_disk ? 1 : 0;
        keyboard.write_port(0x64, 0x60);
        keyboard.write_port(0x60, 0x40); // BIOS keyboard services receive translated set 1.
        set_video_mode(3);

        const bool boot_floppy = floppy && read_word(floppy.get() + sector_size - 2) == 0xaa55;
        const bool boot_hard_disk = hard_disk &&
            read_word(hard_disk.get() + sector_size - 2) == 0xaa55;
        if (!boot_floppy && !boot_hard_disk) {
            fault = "missing_boot_signature";
        } else if (!boot_floppy) {
            rom[boot_drive_offset] = 0x80;
        }
    }

    uint8_t read_memory_value(uint32_t address)
    {
        if (address < ram.size()) {
            return ram[address];
        }
        if (address >= rom_base && address < rom_base + PC286_ROM_SIZE) {
            return rom[address - rom_base];
        }
        if (address >= ega_rom_base && address < ega_rom_base + ega_rom_size) {
            return ega_rom[address - ega_rom_base];
        }
        // When A20 is low, the same high BIOS selection survives a warm reset.
        const uint32_t reset_base = keyboard.a20_enabled() ? reset_rom_base : 0xef0000;
        if (address >= reset_base && address < reset_base + PC286_ROM_SIZE) {
            return rom[address - reset_base];
        }
        return ega.handles_memory(address) ? ega.read_memory(address) : 0xff;
    }

    uint8_t read_memory(uint32_t address) override
    {
        const uint8_t value = read_memory_value(address);
        if (witness && address >= witness_mem_start && address < witness_mem_end)
            observe(PC286_WITNESS_READ, address, value, 1);
        return value;
    }

    void write_memory(uint32_t address, uint8_t value) override
    {
        if (witness && address >= witness_mem_start && address < witness_mem_end)
            observe(PC286_WITNESS_WRITE, address, value, 1);
        if (address < ram.size()) {
            ram[address] = value;
        } else if (ega.handles_memory(address)) {
            ega.write_memory(address, value);
        }
    }

    uint8_t read_port_value(uint16_t port)
    {
        if (port == 0x60 || port == 0x64) {
            return keyboard.read_port(port);
        }
        if (devices.handles_port(port)) {
            return devices.read_port(port);
        }
        if (fdc.handles_port(port)) {
            return fdc.read_port(port);
        }
        if (hdc.handles_port(port)) {
            return hdc.read_port(port);
        }
        if (port == 0x3ba || port == 0x3da) {
            ega.set_cycles(cycles);
        }
        return ega.handles_port(port) ? ega.read_port(port) : 0xff;
    }

    uint8_t read_port(uint16_t port) override
    {
        const uint8_t value = read_port_value(port);
        if (witness && port >= witness_io_start && port < witness_io_end)
            observe(PC286_WITNESS_IN, port, value, 1);
        return value;
    }

    void write_port(uint16_t port, uint8_t value) override
    {
        if (witness && port >= witness_io_start && port < witness_io_end)
            observe(PC286_WITNESS_OUT, port, value, 1);
        if (port == 0x80) {
            post_written = true;
            post = value;
        } else if (port == 0x60 || port == 0x64) {
            keyboard.write_port(port, value);
        } else if (devices.handles_port(port)) {
            devices.write_port(port, value);
        } else if (fdc.handles_port(port)) {
            fdc.write_port(port, value);
        } else if (hdc.handles_port(port)) {
            hdc.write_port(port, value);
        } else if (ega.handles_port(port)) {
            ega.write_port(port, value);
        }
    }

    uint16_t read_io_word(uint16_t port) override
    {
        if (port != 0x1f0) return Bus::read_io_word(port);
        const uint16_t value = hdc.read_data_word();
        if (witness && port >= witness_io_start && port < witness_io_end)
            observe(PC286_WITNESS_IN, port, value, 2);
        return value;
    }

    void write_io_word(uint16_t port, uint16_t value) override
    {
        if (port == 0x1f0) {
            if (witness && port >= witness_io_start && port < witness_io_end)
                observe(PC286_WITNESS_OUT, port, value, 2);
            hdc.write_data_word(value);
        } else {
            Bus::write_io_word(port, value);
        }
    }

    uint8_t acknowledge_interrupt() override
    {
        const uint8_t vector = devices.acknowledge_interrupt();
        observe(PC286_WITNESS_IRQ, 0, vector, 0);
        return vector;
    }

    void update_interrupts()
    {
        if (keyboard.take_irq1()) {
            devices.pulse_irq(1);
        }
        if (fdc.take_irq()) {
            devices.pulse_irq(6);
        }
        if (hdc.take_irq()) {
            devices.pulse_irq(14);
        }
        cpu.set_irq(devices.interrupt_pending());
    }

    bool terminal_halt() const
    {
        return cpu.halted() && (!cpu.interruptible() ||
            (!devices.interrupt_pending() && !devices.timed_interrupt_possible() && !future_input));
    }

    void advance(uint32_t elapsed)
    {
        cycles += elapsed;
        devices.advance_cycles(elapsed);
        if (built_in_firmware && cycles >= next_bios_tick) {
            const uint64_t ticks = cycles * pit_rate / bios_tick_period;
            const uint64_t updated = read_dword(ram.data() + 0x46c) + ticks - bios_ticks_elapsed;
            write_dword(ram.data() + 0x46c, uint32_t(updated % bios_ticks_per_day));
            ram[0x470] = uint8_t(ram[0x470] + updated / bios_ticks_per_day);
            bios_ticks_elapsed = ticks;
            next_bios_tick = ((ticks + 1) * bios_tick_period + pit_rate - 1) / pit_rate;
        }
        if (keyboard.take_reset_request()) {
            cpu.reset();
        }
        cpu.set_a20(keyboard.a20_enabled());
        update_interrupts();
    }

    void disk_status(FirmwareRegisters &regs, bool hard, uint8_t status, uint8_t count = 0)
    {
        ram[hard ? 0x474 : 0x441] = status;
        regs.ax = uint16_t((uint16_t(status) << 8) | count);
        set_carry(regs, status != 0);
    }

    void disk_transfer(FirmwareRegisters &regs, bool hard, unsigned operation)
    {
        const unsigned count = regs.ax & 0xff;
        const unsigned cylinder = (regs.cx >> 8) | ((regs.cx & 0xc0) << 2);
        const unsigned sector = regs.cx & 0x3f;
        const unsigned head = regs.dx >> 8;
        const unsigned drive_sectors = hard ? PcatHardDisk::sectors_per_track : sectors_per_track;
        const unsigned drive_cylinders = hard ? PcatHardDisk::cylinders : cylinders;
        const unsigned drive_heads = hard ? PcatHardDisk::heads : heads;
        if (count == 0 || count > 127 || sector == 0 || sector > drive_sectors ||
            (!hard && count > drive_sectors - sector + 1) ||
            cylinder >= drive_cylinders || head >= drive_heads) {
            disk_status(regs, hard, 4);
            return;
        }
        const unsigned lba = (cylinder * drive_heads + head) * drive_sectors + sector - 1;
        if (count > drive_cylinders * drive_heads * drive_sectors - lba) {
            disk_status(regs, hard, 4);
            return;
        }
        if (operation == 0x04) {
            disk_status(regs, hard, 0, uint8_t(count));
            return;
        }

        const uint32_t destination = (uint32_t(regs.es) << 4) + regs.bx;
        const unsigned length = count * sector_size;
        if ((destination & 0xffff) + length > 0x10000 ||
            destination > ram.size() || length > ram.size() - destination) {
            disk_status(regs, hard, 9);
            return;
        }

        if (!hard && operation == 0x02 && read_failure_enabled &&
            cylinder == read_failure_cylinder && head == read_failure_head &&
            sector == read_failure_sector) {
            ++read_failures;
            disk_status(regs, false, 0x20); // Controller failure; no bytes transferred.
            return;
        }
        if (hard && operation == 0x02 && hard_read_failure_enabled &&
            cylinder == hard_read_failure_cylinder && head == hard_read_failure_head &&
            sector == hard_read_failure_sector) {
            ++hard_read_matches;
            if ((hard_read_failure_at && hard_read_matches == hard_read_failure_at) ||
                (!hard_read_failure_at && (!hard_read_failure_once || hard_read_failures == 0))) {
                ++hard_read_failures;
                disk_status(regs, true, 0x20); // Injected controller failure; no transfer.
                return;
            }
        }
        if (hard && operation == 0x03 && hard_write_failure_enabled &&
            cylinder == hard_write_failure_cylinder && head == hard_write_failure_head &&
            sector == hard_write_failure_sector) {
            ++hard_write_matches;
            if (!hard_write_failure_at || hard_write_matches == hard_write_failure_at) {
                ++hard_write_failures;
                disk_status(regs, true, 0x20); // Injected controller failure; no transfer.
                return;
            }
        }

        uint8_t *const media = (hard ? hard_disk.get() : floppy.get()) + lba * sector_size;
        if (operation == 0x03) {
            std::memcpy(media, ram.data() + destination, length);
        } else {
            std::memcpy(ram.data() + destination, media, length);
        }
        disk_status(regs, hard, 0, uint8_t(count));
    }

    void disk_service(FirmwareRegisters &regs)
    {
        const uint8_t drive = uint8_t(regs.dx);
        const bool hard = (drive & 0x80) != 0;
        if ((hard && (drive != 0x80 || !hard_disk)) || (!hard && (drive != 0 || !floppy))) {
            disk_status(regs, hard, 1);
            return;
        }
        const unsigned operation = regs.ax >> 8;
        switch (operation) {
        case 0x00:
            if (hard && hard_reset_failure_at && ++hard_reset_matches == hard_reset_failure_at) {
                ++hard_reset_failures;
                disk_status(regs, true, 0x20);
            } else {
                disk_status(regs, hard, 0);
            }
            break;
        case 0x01:
            disk_status(regs, hard, ram[hard ? 0x474 : 0x441]);
            break;
        case 0x02:
        case 0x03:
        case 0x04:
            if (operation == 0x03 && !hard && !floppy_writes_enabled) {
                disk_status(regs, false, 3); // Floppy is write protected.
            } else {
                disk_transfer(regs, hard, operation);
            }
            break;
        case 0x05:
            disk_status(regs, hard, hard ? 1 : 3);
            break;
        case 0x08: {
            const unsigned last_cylinder = (hard ? PcatHardDisk::cylinders : cylinders) - 1;
            if (!hard) {
                regs.bx = uint16_t((regs.bx & 0xff00) | 2);
            }
            regs.cx = uint16_t(((last_cylinder & 0xff) << 8) |
                               ((last_cylinder >> 2) & 0xc0) |
                               (hard ? PcatHardDisk::sectors_per_track : sectors_per_track));
            regs.dx = uint16_t((((hard ? PcatHardDisk::heads : heads) - 1) << 8) | 1);
            disk_status(regs, hard, 0);
            break;
        }
        case 0x15:
            ram[hard ? 0x474 : 0x441] = 0;
            regs.ax = hard ? 0x0300 : 0x0100;
            if (hard) {
                regs.cx = uint16_t(PcatHardDisk::sector_count >> 16);
                regs.dx = uint16_t(PcatHardDisk::sector_count);
            }
            set_carry(regs, false);
            break;
        default:
            disk_status(regs, hard, 1);
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
        write_word(ram.data() + 0x44c, ram[0x449] == 3 ? 0x1000 : 0x8000);
        std::fill_n(ram.data() + 0x450, 16, 0);
        write_word(ram.data() + 0x460, 0x0b0c);
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
            scroll_text(0, 0, 24, 79, 1, 7);
            --row;
        }
        ram[0x450] = uint8_t(column);
        ram[0x451] = uint8_t(row);
    }

    void scroll_text(unsigned top, unsigned left, unsigned bottom, unsigned right,
                     int delta, uint8_t attribute)
    {
        if (top > bottom || left > right || bottom >= 25 || right >= 80) {
            return;
        }
        for (unsigned index = 0; index <= bottom - top; ++index) {
            const unsigned row = delta < 0 ? bottom - index : top + index;
            const int source_row = int(row) + delta;
            const bool blank = source_row < int(top) || source_row > int(bottom);
            for (unsigned column = left; column <= right; ++column) {
                const uint32_t address = text_base + 2 * (row * 80 + column);
                if (blank) {
                    ega.write_memory(address, ' ');
                    ega.write_memory(address + 1, attribute);
                } else {
                    const uint32_t source = text_base + 2 * (unsigned(source_row) * 80 + column);
                    ega.write_memory(address, ega.peek_memory(source));
                    ega.write_memory(address + 1, ega.peek_memory(source + 1));
                }
            }
        }
    }

    void video_service(FirmwareRegisters &regs)
    {
        bool handled = false;
        const unsigned operation = regs.ax >> 8;
        const unsigned page = regs.bx >> 8;
        const bool text = ram[0x449] == 3;
        switch (operation) {
        case 0x00:
            handled = set_video_mode(uint8_t(regs.ax));
            break;
        case 0x01:
            write_word(ram.data() + 0x460, regs.cx);
            handled = true;
            break;
        case 0x02:
        case 0x03:
            if (page < 8) {
                uint8_t *const cursor = ram.data() + 0x450 + 2 * page;
                if (operation == 0x02) {
                    write_word(cursor, regs.dx);
                } else {
                    regs.dx = read_word(cursor);
                    regs.cx = read_word(ram.data() + 0x460);
                }
                handled = true;
            }
            break;
        case 0x06:
        case 0x07:
            if (text) {
                int rows = uint8_t(regs.ax);
                if (rows == 0) {
                    rows = 25;
                }
                scroll_text(regs.cx >> 8, uint8_t(regs.cx), regs.dx >> 8, uint8_t(regs.dx),
                            operation == 0x06 ? rows : -rows, uint8_t(page));
                handled = true;
            }
            break;
        case 0x08:
        case 0x09:
        case 0x0a:
            if (text && page < 8) {
                const uint8_t *const cursor = ram.data() + 0x450 + 2 * page;
                if (cursor[0] >= 80 || cursor[1] >= 25) {
                    break;
                }
                const unsigned position = cursor[1] * 80 + cursor[0];
                const uint32_t address = text_base + page * 0x1000 + position * 2;
                if (operation == 0x08) {
                    regs.ax = uint16_t(ega.peek_memory(address) |
                                       (uint16_t(ega.peek_memory(address + 1)) << 8));
                } else {
                    const unsigned count = std::min(unsigned(regs.cx), 25 * 80 - position);
                    for (unsigned index = 0; index < count; ++index) {
                        ega.write_memory(address + 2 * index, uint8_t(regs.ax));
                        if (operation == 0x09) {
                            ega.write_memory(address + 2 * index + 1, uint8_t(regs.bx));
                        }
                    }
                }
                handled = true;
            }
            break;
        case 0x0e:
            handled = text;
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

    bool keyboard_key(bool consume, uint16_t &key)
    {
        uint8_t raw;
        while (keyboard.peek_output(raw)) {
            if (pause_bytes != 0 || raw == 0xe0 || raw == 0xe1) {
                keyboard.read_port(0x60);
                if (pause_bytes != 0) {
                    --pause_bytes;
                } else {
                    extended_scan = raw == 0xe0;
                    pause_bytes = raw == 0xe1 ? 5 : 0;
                }
                continue;
            }
            const uint8_t scan = raw & 0x7f;
            const bool released = (raw & 0x80) != 0;
            const uint8_t modifier = scan == 0x36 && !extended_scan ? 1
                : scan == 0x2a && !extended_scan ? 2
                : scan == 0x1d ? 4
                : scan == 0x38 ? 8 : 0;
            if (modifier != 0) {
                ram[0x417] = uint8_t((ram[0x417] & ~modifier) | (released ? 0 : modifier));
            } else if (scan == 0x3a && !extended_scan) {
                if (!released) {
                    ram[0x417] ^= 0x40;
                }
            } else if (!released) {
                key = uint16_t((uint16_t(scan) << 8) | keyboard_ascii(scan, ram[0x417], extended_scan));
                if (consume) {
                    keyboard.read_port(0x60);
                    extended_scan = false;
                }
                return true;
            }
            keyboard.read_port(0x60);
            extended_scan = false;
        }
        return false;
    }

    void keyboard_service(FirmwareRegisters &regs)
    {
        switch (regs.ax >> 8) {
        case 0x00:
        case 0x10:
            regs.repeat = !keyboard_key(true, regs.ax);
            break;
        case 0x01:
        case 0x11:
            regs.flags = keyboard_key(false, regs.ax)
                ? uint16_t(regs.flags & ~zero) : uint16_t(regs.flags | zero);
            break;
        case 0x02:
        case 0x12: {
            uint16_t key;
            keyboard_key(false, key);
            regs.ax = uint16_t((regs.ax & 0xff00) | ram[0x417]);
            break;
        }
        default:
            set_carry(regs, true);
            break;
        }
    }

    uint8_t rtc_bcd(uint8_t index) const
    {
        const uint8_t value = devices.rtc_register(index);
        return (devices.rtc_register(0x0b) & 4) != 0
            ? uint8_t(((value / 10) << 4) | (value % 10)) : value;
    }

    void time_service(FirmwareRegisters &regs)
    {
        switch (regs.ax >> 8) {
        case 0x00: {
            const uint32_t ticks = read_dword(ram.data() + 0x46c);
            regs.cx = uint16_t(ticks >> 16);
            regs.dx = uint16_t(ticks);
            regs.ax = ram[0x470];
            ram[0x470] = 0;
            break;
        }
        case 0x01:
            write_dword(ram.data() + 0x46c,
                        ((uint32_t(regs.cx) << 16) | regs.dx) % bios_ticks_per_day);
            ram[0x470] = 0;
            break;
        case 0x02:
            regs.cx = uint16_t((uint16_t(rtc_bcd(4)) << 8) | rtc_bcd(2));
            regs.dx = uint16_t((uint16_t(rtc_bcd(0)) << 8) | (devices.rtc_register(0x0b) & 1));
            break;
        case 0x04:
            regs.cx = uint16_t((uint16_t(rtc_bcd(0x32)) << 8) | rtc_bcd(9));
            regs.dx = uint16_t((uint16_t(rtc_bcd(8)) << 8) | rtc_bcd(7));
            break;
        default:
            set_carry(regs, true);
            return;
        }
        set_carry(regs, false);
    }

    bool firmware_interrupt(uint8_t number, FirmwareRegisters &regs) override
    {
        if (!built_in_firmware) {
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
        case 0x15:
            if ((regs.ax >> 8) == 0x88) {
                regs.ax = 0;
                set_carry(regs, false);
            } else {
                regs.ax = uint16_t(0x8600 | (regs.ax & 0xff));
                set_carry(regs, true);
            }
            return true;
        case 0x16:
            keyboard_service(regs);
            return true;
        case 0x1a:
            time_service(regs);
            return true;
        default:
            return false;
        }
    }
};

pc286 *pc286_create(const uint8_t *rom, size_t size)
{
    return pc286_create_system(rom, size, nullptr, 0, nullptr, 0);
}

pc286 *pc286_create_floppy(const uint8_t *image, size_t size)
{
    return pc286_create_system(nullptr, 0, image, size, nullptr, 0);
}

bool pc286_enable_floppy_writes(pc286 *machine)
{
    if (!machine || !machine->built_in_firmware || !machine->floppy) return false;
    machine->floppy_writes_enabled = true;
    return true;
}

const uint8_t *pc286_floppy(const pc286 *machine)
{
    return machine ? machine->floppy.get() : nullptr;
}

bool pc286_fail_floppy_read(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector)
{
    if (!machine || !machine->built_in_firmware || !machine->floppy ||
        cylinder >= cylinders || head >= heads || sector == 0 || sector > sectors_per_track)
        return false;
    machine->read_failure_enabled = true;
    machine->read_failure_cylinder = uint8_t(cylinder);
    machine->read_failure_head = uint8_t(head);
    machine->read_failure_sector = uint8_t(sector);
    return true;
}

bool pc286_fail_hard_disk_read(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector)
{
    if (!machine || !machine->built_in_firmware || !machine->hard_disk ||
        cylinder >= PcatHardDisk::cylinders || head >= PcatHardDisk::heads ||
        sector == 0 || sector > PcatHardDisk::sectors_per_track)
        return false;
    machine->hard_read_failure_enabled = true;
    machine->hard_read_failure_cylinder = uint16_t(cylinder);
    machine->hard_read_failure_head = uint8_t(head);
    machine->hard_read_failure_sector = uint8_t(sector);
    machine->hard_read_failure_once = false;
    machine->hard_read_failure_at = 0;
    machine->hard_read_matches = 0;
    machine->hard_read_failures = 0;
    return true;
}

bool pc286_fail_hard_disk_read_once(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector)
{
    if (!pc286_fail_hard_disk_read(machine, cylinder, head, sector)) return false;
    machine->hard_read_failure_once = true;
    return true;
}

bool pc286_fail_hard_disk_read_at(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector, uint32_t occurrence)
{
    if (!occurrence || !pc286_fail_hard_disk_read(machine, cylinder, head, sector)) return false;
    machine->hard_read_failure_at = occurrence;
    return true;
}

bool pc286_fail_hard_disk_write(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector)
{
    if (!machine || !machine->built_in_firmware || !machine->hard_disk ||
        cylinder >= PcatHardDisk::cylinders || head >= PcatHardDisk::heads ||
        sector == 0 || sector > PcatHardDisk::sectors_per_track)
        return false;
    machine->hard_write_failure_enabled = true;
    machine->hard_write_failure_cylinder = uint16_t(cylinder);
    machine->hard_write_failure_head = uint8_t(head);
    machine->hard_write_failure_sector = uint8_t(sector);
    machine->hard_write_failure_at = 0;
    machine->hard_write_matches = 0;
    machine->hard_write_failures = 0;
    return true;
}

bool pc286_fail_hard_disk_write_at(pc286 *machine, unsigned cylinder, unsigned head, unsigned sector, uint32_t occurrence)
{
    if (!occurrence || !pc286_fail_hard_disk_write(machine, cylinder, head, sector)) return false;
    machine->hard_write_failure_at = occurrence;
    return true;
}

bool pc286_fail_hard_disk_reset_at(pc286 *machine, uint32_t occurrence)
{
    if (!machine || !machine->built_in_firmware || !machine->hard_disk || !occurrence) return false;
    machine->hard_reset_failure_at = occurrence;
    machine->hard_reset_matches = machine->hard_reset_failures = 0;
    return true;
}

pc286 *pc286_create_system(const uint8_t *rom, size_t rom_size,
                           const uint8_t *floppy, size_t floppy_size,
                           const uint8_t *disk, size_t disk_size)
{
    static_assert(PC286_HARD_DISK_SIZE == PcatHardDisk::image_size);
    if ((!rom && !floppy && !disk) ||
        rom_size != (rom ? PC286_ROM_SIZE : 0u) ||
        floppy_size != (floppy ? PC286_FLOPPY_SIZE : 0u) ||
        disk_size != (disk ? PC286_HARD_DISK_SIZE : 0u)) {
        return nullptr;
    }
    auto machine = std::unique_ptr<pc286>(new (std::nothrow) pc286);
    if (!machine) {
        return nullptr;
    }

    if (floppy) {
        machine->floppy.reset(new (std::nothrow) uint8_t[floppy_size]);
        if (!machine->floppy) {
            return nullptr;
        }
        std::memcpy(machine->floppy.get(), floppy, floppy_size);
        machine->fdc.set_media({machine->floppy.get(), floppy_size});
    }
    if (disk) {
        machine->hard_disk.reset(new (std::nothrow) uint8_t[disk_size]);
        if (!machine->hard_disk) {
            return nullptr;
        }
        std::memcpy(machine->hard_disk.get(), disk, disk_size);
        machine->hdc.mount({machine->hard_disk.get(), disk_size});
    }
    machine->devices.configure_drives(floppy != nullptr, disk != nullptr);
    if (rom) {
        std::memcpy(machine->rom.data(), rom, rom_size);
    } else {
        machine->initialize_firmware();
    }
    return machine.release();
}

bool pc286_load_ega_rom(pc286 *machine, const uint8_t *rom, size_t size)
{
    if (!rom || size < 512 || size > PC286_EGA_ROM_MAX || size % 512 != 0 ||
        rom[0] != 0x55 || rom[1] != 0xaa || rom[2] == 0 || size_t(rom[2]) * 512 > size) {
        return false;
    }
    const size_t declared_size = size_t(rom[2]) * 512;
    uint8_t checksum = 0;
    for (size_t offset = 0; offset < declared_size; ++offset) {
        checksum = uint8_t(checksum + rom[offset]);
    }
    if (checksum != 0) {
        return false;
    }
    std::memcpy(machine->ega_rom.data(), rom, declared_size);
    machine->ega_rom_size = declared_size;
    return true;
}

const uint8_t *pc286_hard_disk(const pc286 *machine)
{
    return machine->hard_disk.get();
}

bool pc286_inject_scancode(pc286 *machine, uint8_t scancode)
{
    const bool accepted = machine->keyboard.inject_scancode(scancode);
    machine->update_interrupts();
    return accepted;
}

void pc286_expect_input(pc286 *machine, bool pending)
{
    machine->future_input = pending;
}

void pc286_destroy(pc286 *machine)
{
    delete machine;
}

void pc286_run(pc286 *machine, uint32_t cycles)
{
    uint64_t advanced = 0;
    while (advanced < cycles && machine->fault == nullptr &&
           !machine->terminal_halt() && !machine->cpu.shutdown()) {
        // Keep device time and REP scheduling independent of the host's poll slice.
        machine->witness_pc = machine->cpu.pc();
        machine->witness_cs = machine->cpu.cs();
        machine->witness_ip = machine->cpu.ip();
        if (machine->witness) machine->observe(PC286_WITNESS_STEP, 0, 0, 0);
        const int previous_exception = machine->cpu.last_exception();
        const uint32_t consumed = machine->cpu.halted() && !machine->devices.interrupt_pending()
            ? (machine->devices.timed_interrupt_possible() ? 1 : uint32_t(cycles - advanced))
            : machine->cpu.run(1);
        if (machine->witness && machine->cpu.last_exception() != previous_exception && machine->cpu.last_exception() >= 0)
            machine->observe(PC286_WITNESS_EXCEPTION, 0, machine->cpu.last_exception(), 0);
        if (consumed == 0) {
            machine->fault = "cpu_no_progress";
            break;
        }
        advanced += consumed;
        machine->advance(consumed);
    }
}

bool pc286_start_witness(pc286 *machine, uint32_t mem_start, uint32_t mem_end,
                         uint32_t io_start, uint32_t io_end, uint64_t first_cycle, uint64_t last_cycle)
{
    if (mem_start > mem_end || mem_end > 0x1000000 || io_start > io_end || io_end > 0x10000 || first_cycle > last_cycle) return false;
    machine->witness.reset(new (std::nothrow) pc286_witness_event[PC286_WITNESS_CAPACITY]);
    if (!machine->witness) return false;
    machine->witness_mem_start = mem_start; machine->witness_mem_end = mem_end;
    machine->witness_io_start = io_start; machine->witness_io_end = io_end;
    machine->witness_first = first_cycle; machine->witness_last = last_cycle;
    machine->witness_count = 0; machine->witness_dropped = 0;
    return true;
}

const pc286_witness_event *pc286_witness(const pc286 *machine, size_t *count, uint64_t *dropped)
{
    *count = machine->witness_count; *dropped = machine->witness_dropped;
    return machine->witness.get();
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
    state.halted = machine->terminal_halt();
    state.keyboard_output_queued = static_cast<uint32_t>(machine->keyboard.output_queued());
    state.keyboard_output_capacity = static_cast<uint32_t>(machine->keyboard.output_capacity());
    state.read_failure_enabled = machine->read_failure_enabled;
    state.read_failure_cylinder = machine->read_failure_cylinder;
    state.read_failure_head = machine->read_failure_head;
    state.read_failure_sector = machine->read_failure_sector;
    state.read_failures = machine->read_failures;
    state.hard_read_failure_enabled = machine->hard_read_failure_enabled;
    state.hard_read_failure_once = machine->hard_read_failure_once;
    state.hard_read_failure_at = machine->hard_read_failure_at;
    state.hard_read_matches = machine->hard_read_matches;
    state.hard_read_failure_cylinder = machine->hard_read_failure_cylinder;
    state.hard_read_failure_head = machine->hard_read_failure_head;
    state.hard_read_failure_sector = machine->hard_read_failure_sector;
    state.hard_read_failures = machine->hard_read_failures;
    state.hard_write_failure_enabled = machine->hard_write_failure_enabled;
    state.hard_write_failure_at = machine->hard_write_failure_at;
    state.hard_write_matches = machine->hard_write_matches;
    state.hard_write_failure_cylinder = machine->hard_write_failure_cylinder;
    state.hard_write_failure_head = machine->hard_write_failure_head;
    state.hard_write_failure_sector = machine->hard_write_failure_sector;
    state.hard_write_failures = machine->hard_write_failures;
    state.hard_reset_failure_at = machine->hard_reset_failure_at;
    state.hard_reset_matches = machine->hard_reset_matches;
    state.hard_reset_failures = machine->hard_reset_failures;
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

bool pc286_set_ega_font(pc286 *machine, const uint8_t *font, size_t size)
{
    if (!machine || !font || size != 256 * 14) return false;
    machine->ega.set_text_font(font);
    return true;
}
