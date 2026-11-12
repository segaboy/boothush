#include "pcat_floppy.h"

#include <algorithm>
#include <utility>

namespace {

constexpr unsigned sector_size = 512;
constexpr unsigned sectors_per_track = 15;
constexpr unsigned heads = 2;
constexpr unsigned cylinder_count = 80;

constexpr std::uint8_t dor_reset = 0x04;
constexpr std::uint8_t dor_dma_irq = 0x08;

constexpr std::uint8_t specify_command = 0x03;
constexpr std::uint8_t sense_drive_command = 0x04;
constexpr std::uint8_t write_data_command = 0x05;
constexpr std::uint8_t read_data_command = 0x06;
constexpr std::uint8_t recalibrate_command = 0x07;
constexpr std::uint8_t sense_interrupt_command = 0x08;
constexpr std::uint8_t write_deleted_command = 0x09;
constexpr std::uint8_t read_id_command = 0x0a;
constexpr std::uint8_t read_deleted_command = 0x0c;
constexpr std::uint8_t format_track_command = 0x0d;
constexpr std::uint8_t seek_command = 0x0f;

std::size_t command_length(std::uint8_t opcode)
{
    switch (opcode & 0x1f) {
    case write_data_command:
    case read_data_command:
    case write_deleted_command:
    case read_deleted_command:
        return 9;
    case specify_command:
    case seek_command:
        return 3;
    case sense_drive_command:
    case recalibrate_command:
    case read_id_command:
        return 2;
    case format_track_command:
        return 6;
    default:
        return 1;
    }
}

bool is_dma_page(std::uint16_t port)
{
    switch (port) {
    case 0x81: case 0x82: case 0x83: case 0x87:
    case 0x89: case 0x8a: case 0x8b: case 0x8f:
        return true;
    default:
        return false;
    }
}

std::size_t media_offset(unsigned cylinder, unsigned head, unsigned sector)
{
    return ((cylinder * heads + head) * sectors_per_track + sector - 1) * sector_size;
}

} // namespace

std::uint8_t PcatFloppy::Dma8237::read(std::uint8_t reg)
{
    if (reg < 8) {
        const DmaChannel &channel = channels[reg >> 1];
        const std::uint16_t value = (reg & 1) ? channel.count : channel.address;
        const std::uint8_t part = std::uint8_t(high_byte ? value >> 8 : value);
        high_byte = !high_byte;
        return part;
    }

    switch (reg) {
    case 8: {
        const auto status = std::uint8_t((request << 4) | terminal_count);
        terminal_count = 0;
        return status;
    }
    case 0x0d:
        return 0;
    case 0x0f:
        return mask;
    default:
        return 0xff;
    }
}

void PcatFloppy::Dma8237::write(std::uint8_t reg, std::uint8_t value)
{
    if (reg < 8) {
        DmaChannel &channel = channels[reg >> 1];
        std::uint16_t &current = (reg & 1) ? channel.count : channel.address;
        std::uint16_t &base = (reg & 1) ? channel.base_count : channel.base_address;
        if (high_byte) {
            current = std::uint16_t((current & 0xff) | (std::uint16_t(value) << 8));
        } else {
            current = std::uint16_t((current & 0xff00) | value);
        }
        base = current;
        high_byte = !high_byte;
        return;
    }

    const auto channel_bit = std::uint8_t(1u << (value & 3));
    switch (reg) {
    case 8:
        command = value;
        break;
    case 9:
        request = std::uint8_t((request & ~channel_bit) | ((value & 4) ? channel_bit : 0));
        break;
    case 0x0a:
        mask = std::uint8_t((mask & ~channel_bit) | ((value & 4) ? channel_bit : 0));
        break;
    case 0x0b:
        channels[value & 3].mode = value;
        break;
    case 0x0c:
        high_byte = false;
        break;
    case 0x0d:
        command = 0;
        mask = 0x0f;
        terminal_count = 0;
        request = 0;
        high_byte = false;
        break;
    case 0x0e:
        mask = 0;
        break;
    case 0x0f:
        mask = value & 0x0f;
        break;
    }
}

std::size_t PcatFloppy::Dma8237::write_device_data(
    unsigned channel_index, std::uint8_t page, std::span<const std::uint8_t> data,
    std::span<std::uint8_t> ram, bool &reached_terminal_count)
{
    DmaChannel &channel = channels[channel_index];
    const auto channel_bit = std::uint8_t(1u << channel_index);
    reached_terminal_count = false;

    if ((command & 4) || (mask & channel_bit) || (channel.mode & 0x0c) != 4) {
        return 0;
    }

    const std::size_t remaining = std::size_t(channel.count) + 1;
    const std::size_t transferred = std::min(remaining, data.size());
    const std::uint32_t page_address = std::uint32_t(page) << 16;
    const int step = (channel.mode & 0x20) ? -1 : 1;
    for (const auto value : data.first(transferred)) {
        const std::uint32_t address = page_address | channel.address;
        if (address < ram.size()) {
            ram[address] = value;
        }
        channel.address = std::uint16_t(channel.address + step);
    }
    if (transferred == remaining) {
        reached_terminal_count = true;
        terminal_count |= channel_bit;
        request &= std::uint8_t(~channel_bit);
        if (channel.mode & 0x10) {
            channel.address = channel.base_address;
            channel.count = channel.base_count;
        } else {
            mask |= channel_bit;
            channel.count = 0xffff;
        }
    } else {
        channel.count = std::uint16_t(remaining - transferred - 1);
    }
    return transferred;
}

PcatFloppy::PcatFloppy(std::span<std::uint8_t> ram)
    : ram_(ram)
{
}

void PcatFloppy::set_media(std::span<const std::uint8_t> media)
{
    const auto new_media = media.size() == image_size ? media : std::span<const std::uint8_t>{};
    if ((dor_ & dor_reset) && new_media.data() != media_.data()) {
        disk_changed_ = true;
        pio_position_ = pio_end_ = 0;
    }
    media_ = new_media;
}

bool PcatFloppy::handles_port(std::uint16_t port) const
{
    return port < 0x10 || (port >= 0xc0 && port <= 0xde && !(port & 1)) ||
           is_dma_page(port) || (port >= 0x3f2 && port <= 0x3f5) || port == 0x3f7;
}

std::uint8_t PcatFloppy::read_port(std::uint16_t port)
{
    if (port < 0x10) {
        return dma1_.read(std::uint8_t(port));
    }
    if (port >= 0xc0 && port <= 0xde && !(port & 1)) {
        return dma2_.read(std::uint8_t((port - 0xc0) >> 1));
    }
    if (is_dma_page(port)) {
        return dma_pages_[port - 0x80];
    }

    switch (port) {
    case 0x3f4:
        return main_status();
    case 0x3f5:
        return read_fifo();
    case 0x3f7:
        return disk_changed_ ? 0x80 : 0;
    default:
        return 0xff;
    }
}

void PcatFloppy::write_port(std::uint16_t port, std::uint8_t value)
{
    if (port < 0x10) {
        dma1_.write(std::uint8_t(port), value);
        return;
    }
    if (port >= 0xc0 && port <= 0xde && !(port & 1)) {
        dma2_.write(std::uint8_t((port - 0xc0) >> 1), value);
        return;
    }
    if (is_dma_page(port)) {
        dma_pages_[port - 0x80] = value;
        return;
    }

    switch (port) {
    case 0x3f2:
        write_dor(value);
        break;
    case 0x3f5:
        write_fifo(value);
        break;
    case 0x3f7:
        data_rate_ = value & 3;
        break;
    }
}

bool PcatFloppy::take_irq()
{
    return std::exchange(irq_pending_, false);
}

void PcatFloppy::signal_irq()
{
    if (dor_ & dor_dma_irq) {
        irq_pending_ = true;
    }
}

void PcatFloppy::write_dor(std::uint8_t value)
{
    const std::uint8_t previous = std::exchange(dor_, value);
    if (!(value & dor_reset)) {
        reset_controller();
        return;
    }
    if (!(previous & dor_reset)) {
        reset_controller();
        signal_irq();
    } else if (!(previous & dor_dma_irq) && seek_pending_ != 0) {
        signal_irq();
    }
}

void PcatFloppy::reset_controller()
{
    non_dma_ = false;
    irq_pending_ = false;
    command_size_ = command_length_ = 0;
    result_size_ = result_position_ = 0;
    pio_position_ = pio_end_ = 0;
    seek_pending_ = 0x0f;
    for (unsigned unit = 0; unit < cylinders_.size(); ++unit) {
        cylinders_[unit] = 0;
        seek_status_[unit] = std::uint8_t(0xc0 | unit);
    }
}

std::uint8_t PcatFloppy::main_status() const
{
    if (!(dor_ & dor_reset)) {
        return 0;
    }
    if (pio_position_ < pio_end_) {
        return 0xf0;
    }
    if (result_position_ < result_size_) {
        return 0xd0;
    }
    return command_size_ != 0 ? 0x90 : 0x80;
}

void PcatFloppy::write_fifo(std::uint8_t value)
{
    if (!(dor_ & dor_reset) || result_position_ < result_size_) {
        return;
    }
    if (command_size_ == 0) {
        command_length_ = command_length(value);
    }
    command_[command_size_++] = value;
    if (command_size_ == command_length_) {
        command_size_ = 0;
        execute_command();
    }
}

std::uint8_t PcatFloppy::read_fifo()
{
    if (pio_position_ < pio_end_) {
        const std::uint8_t byte = media_[pio_position_++];
        if (pio_position_ == pio_end_) {
            signal_irq();
        }
        return byte;
    }
    if (result_position_ < result_size_) {
        return result_[result_position_++];
    }
    return 0xff;
}

void PcatFloppy::result(std::initializer_list<std::uint8_t> values)
{
    std::copy(values.begin(), values.end(), result_.begin());
    result_size_ = values.size();
    result_position_ = 0;
}

void PcatFloppy::data_result(std::uint8_t st0, std::uint8_t st1, std::uint8_t st2,
                            std::uint8_t cylinder, std::uint8_t head, std::uint8_t sector,
                            std::uint8_t size)
{
    result({st0, st1, st2, cylinder, head, sector, size});
}

bool PcatFloppy::motor_enabled(std::uint8_t unit) const
{
    return unit == 0 && (dor_ & 3) == unit && (dor_ & 0x10);
}

void PcatFloppy::complete_seek(std::uint8_t unit, std::uint8_t cylinder)
{
    if (motor_enabled(unit) && cylinder < cylinder_count) {
        cylinders_[unit] = cylinder;
        seek_status_[unit] = std::uint8_t(0x20 | (command_[1] & 7));
        if (!media_.empty()) {
            disk_changed_ = false;
        }
    } else {
        seek_status_[unit] = std::uint8_t(0x78 | (command_[1] & 7));
    }
    seek_pending_ |= std::uint8_t(1u << unit);
    signal_irq();
}

void PcatFloppy::sense_interrupt()
{
    for (unsigned unit = 0; unit < cylinders_.size(); ++unit) {
        const auto bit = std::uint8_t(1u << unit);
        if (seek_pending_ & bit) {
            seek_pending_ &= std::uint8_t(~bit);
            result({seek_status_[unit], cylinders_[unit]});
            return;
        }
    }
    result({0x80});
}

void PcatFloppy::read_id()
{
    const std::uint8_t unit = command_[1] & 3;
    const std::uint8_t head = (command_[1] >> 2) & 1;
    const std::uint8_t st0 = command_[1] & 7;
    if (!motor_enabled(unit) || media_.empty()) {
        data_result(std::uint8_t(st0 | 0x48), 4, 0, cylinders_[unit], head, 1, 2);
    } else if ((command_[0] & 0x40) == 0 || data_rate_ != 0) {
        data_result(std::uint8_t(st0 | 0x40), 4, 0, cylinders_[unit], head, 1, 2);
    } else {
        data_result(st0, 0, 0, cylinders_[unit], head, 1, 2);
    }
    signal_irq();
}

void PcatFloppy::read_data()
{
    const std::uint8_t unit = command_[1] & 3;
    const std::uint8_t selected_head = (command_[1] >> 2) & 1;
    const std::uint8_t st0 = command_[1] & 7;
    const std::uint8_t cylinder = command_[2];
    const std::uint8_t head = command_[3];
    const std::uint8_t sector = command_[4];
    const std::uint8_t size = command_[5];
    const std::uint8_t end_sector = command_[6];

    if (!motor_enabled(unit) || media_.empty()) {
        data_result(std::uint8_t(st0 | 0x48), 4, 0, cylinder, head, sector, size);
        signal_irq();
        return;
    }
    if (cylinder != cylinders_[unit] || head != selected_head || size != 2 ||
        sector == 0 || sector > end_sector || end_sector > sectors_per_track ||
        (command_[0] & 0x40) == 0 || data_rate_ != 0) {
        const std::uint8_t st2 = cylinder != cylinders_[unit] ? 0x10 : 0;
        data_result(std::uint8_t(st0 | 0x40), 4, st2, cylinder, head, sector, size);
        signal_irq();
        return;
    }

    const bool next_head = (command_[0] & 0x80) && head == 0 && end_sector == sectors_per_track;
    const unsigned last_head = next_head ? 1 : head;
    const std::size_t begin = media_offset(cylinder, head, sector);
    const std::size_t end = media_offset(cylinder, last_head, end_sector) + sector_size;
    if (non_dma_) {
        pio_position_ = begin;
        pio_end_ = end;
        data_result(std::uint8_t(unit | (last_head << 2)), 0, 0, cylinder,
                    std::uint8_t(last_head), end_sector, size);
        return;
    }

    bool terminal_count = false;
    const bool cascade_enabled = (dma2_.command & 4) == 0 && (dma2_.mask & 1) == 0 &&
                                 (dma2_.channels[0].mode & 0xc0) == 0xc0;
    const std::size_t transferred = (dor_ & dor_dma_irq) && cascade_enabled
        ? dma1_.write_device_data(2, dma_pages_[1], media_.subspan(begin, end - begin),
                                  ram_, terminal_count)
        : 0;
    if (transferred == 0) {
        data_result(std::uint8_t(st0 | 0x40), 0x10, 0, cylinder, head, sector, size);
    } else {
        const unsigned index = unsigned((begin + transferred - 1) / sector_size);
        const auto reached_head = std::uint8_t((index / sectors_per_track) % heads);
        const auto reached_sector = std::uint8_t((index % sectors_per_track) + 1);
        const auto status = std::uint8_t(unit | (reached_head << 2));
        data_result(std::uint8_t(status | (terminal_count ? 0 : 0x40)),
                    terminal_count ? 0 : 0x80, 0, cylinder, reached_head, reached_sector, size);
    }
    signal_irq();
}

void PcatFloppy::execute_command()
{
    const std::uint8_t unit = command_[1] & 3;
    switch (command_[0] & 0x1f) {
    case specify_command:
        non_dma_ = (command_[2] & 1) != 0;
        break;
    case sense_drive_command: {
        std::uint8_t status = command_[1] & 7;
        if (unit == 0) {
            status |= 0x48;  // Two heads and write-protected media.
            if (cylinders_[unit] == 0) {
                status |= 0x10;
            }
            if (motor_enabled(unit) && !media_.empty()) {
                status |= 0x20;
            }
        }
        result({status});
        break;
    }
    case recalibrate_command:
        complete_seek(unit, 0);
        break;
    case seek_command:
        complete_seek(unit, command_[2]);
        break;
    case sense_interrupt_command:
        sense_interrupt();
        break;
    case read_id_command:
        read_id();
        break;
    case read_data_command:
        read_data();
        break;
    case read_deleted_command:
        data_result(std::uint8_t(0x40 | (command_[1] & 7)), 4, 0,
                    command_[2], command_[3], command_[4], command_[5]);
        signal_irq();
        break;
    case write_data_command:
    case write_deleted_command:
        data_result(std::uint8_t(0x40 | (command_[1] & 7)), 2, 0,
                    command_[2], command_[3], command_[4], command_[5]);
        signal_irq();
        break;
    case format_track_command:
        data_result(std::uint8_t(0x40 | (command_[1] & 7)), 2, 0,
                    cylinders_[unit], (command_[1] >> 2) & 1, 1, command_[2]);
        signal_irq();
        break;
    default:
        result({0x80});
        break;
    }
}
