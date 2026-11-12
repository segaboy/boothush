#include "pcat_hard_disk.h"

#include <algorithm>

namespace {
constexpr uint8_t ready = 0x50;
constexpr uint8_t data_request = 0x08;
constexpr uint8_t error_flag = 0x01;
constexpr uint8_t busy = 0x80;
constexpr uint8_t disable_irq = 0x02;
constexpr uint8_t software_reset = 0x04;
constexpr uint8_t aborted = 0x04;
constexpr uint8_t id_not_found = 0x10;
} // namespace

void PcatHardDisk::mount(std::span<uint8_t> image)
{
    media_ = image.size() == image_size ? image : std::span<uint8_t>{};
    reset();
}

bool PcatHardDisk::handles_port(uint16_t port) const
{
    return (port >= 0x1f0 && port <= 0x1f7) || port == 0x3f6;
}

bool PcatHardDisk::selected() const
{
    return !media_.empty() && (drive_head_ & 0x10) == 0;
}

void PcatHardDisk::reset()
{
    error_ = 1;
    count_ = 1;
    sector_ = 1;
    cylinder_ = 0;
    drive_head_ = 0xa0;
    status_ = selected() ? ready : 0;
    transfer_ = Transfer::idle;
    irq_ = false;
}

void PcatHardDisk::raise_irq()
{
    irq_ = (control_ & disable_irq) == 0;
}

bool PcatHardDisk::take_irq()
{
    const bool pending = irq_;
    irq_ = false;
    return pending;
}

unsigned PcatHardDisk::sector_index() const
{
    const unsigned head = drive_head_ & 0x0f;
    if (cylinder_ >= cylinders || head >= heads || sector_ < 1 || sector_ > sectors_per_track) {
        return sector_count;
    }
    return (unsigned(cylinder_) * heads + head) * sectors_per_track + sector_ - 1;
}

void PcatHardDisk::fail(uint8_t error)
{
    transfer_ = Transfer::idle;
    error_ = error;
    status_ = ready | error_flag;
    raise_irq();
}

void PcatHardDisk::complete()
{
    transfer_ = Transfer::idle;
    status_ = ready;
    raise_irq();
}

void PcatHardDisk::command(uint8_t opcode)
{
    irq_ = false;
    transfer_ = Transfer::idle;
    if (!selected() || (control_ & software_reset)) {
        return;
    }

    error_ = 0;
    if (opcode >= 0x10 && opcode <= 0x1f) {
        cylinder_ = 0;
        complete();
        return;
    }
    if (opcode == 0x90) {
        error_ = 1; // Controller self-test passed.
        complete();
        return;
    }
    if (opcode == 0x91) {
        if ((drive_head_ & 0x0f) + 1u == heads && count_ == sectors_per_track) {
            complete();
        } else {
            fail(aborted);
        }
        return;
    }
    if (opcode >= 0x70 && opcode <= 0x7f) {
        if (cylinder_ < cylinders && (drive_head_ & 0x0f) < heads) {
            complete();
        } else {
            fail(id_not_found);
        }
        return;
    }

    const bool read = opcode == 0x20 || opcode == 0x21;
    const bool write = opcode == 0x30 || opcode == 0x31;
    const bool verify = opcode == 0x40 || opcode == 0x41;
    if ((!read && !write && !verify) || (drive_head_ & 0x40)) {
        fail(aborted);
        return;
    }
    lba_ = sector_index();
    remaining_ = count_ != 0 ? count_ : 256;
    if (remaining_ > sector_count - lba_) {
        fail(id_not_found);
        return;
    }
    if (verify) {
        count_ = 0;
        complete();
        return;
    }

    transfer_ = read ? Transfer::read : Transfer::write;
    status_ = ready | data_request;
    offset_ = 0;
    if (read) {
        raise_irq();
    }
}

void PcatHardDisk::finish_sector()
{
    const bool writing = transfer_ == Transfer::write;
    if (writing) {
        std::copy(buffer_.begin(), buffer_.end(), media_.begin() + lba_ * sector_size);
    }
    ++lba_;
    --remaining_;
    --count_;
    cylinder_ = uint16_t(lba_ / (heads * sectors_per_track));
    drive_head_ = uint8_t((drive_head_ & 0xf0) | ((lba_ / sectors_per_track) % heads));
    sector_ = uint8_t(lba_ % sectors_per_track + 1);
    offset_ = 0;
    if (remaining_ == 0) {
        transfer_ = Transfer::idle;
        status_ = ready;
        if (writing) {
            raise_irq();
        }
    } else {
        raise_irq();
    }
}

uint8_t PcatHardDisk::read_data_byte()
{
    if (transfer_ != Transfer::read) {
        return 0xff;
    }
    const uint8_t value = media_[lba_ * sector_size + offset_++];
    if (offset_ == sector_size) {
        finish_sector();
    }
    return value;
}

void PcatHardDisk::write_data_byte(uint8_t value)
{
    if (transfer_ == Transfer::write) {
        buffer_[offset_++] = value;
        if (offset_ == sector_size) {
            finish_sector();
        }
    }
}

uint16_t PcatHardDisk::read_data_word()
{
    const uint8_t low = read_data_byte();
    return uint16_t(low) | (uint16_t(read_data_byte()) << 8);
}

void PcatHardDisk::write_data_word(uint16_t value)
{
    write_data_byte(uint8_t(value));
    write_data_byte(uint8_t(value >> 8));
}

uint8_t PcatHardDisk::read_port(uint16_t port)
{
    if (port == 0x1f6) {
        return drive_head_;
    }
    if (!selected()) {
        return (port == 0x1f7 || port == 0x3f6) ? 0 : 0xff;
    }
    switch (port) {
    case 0x1f0: return read_data_byte();
    case 0x1f1: return error_;
    case 0x1f2: return count_;
    case 0x1f3: return sector_;
    case 0x1f4: return uint8_t(cylinder_);
    case 0x1f5: return uint8_t(cylinder_ >> 8);
    case 0x1f7:
        irq_ = false;
        return status_;
    case 0x3f6: return status_;
    default: return 0xff;
    }
}

void PcatHardDisk::write_port(uint16_t port, uint8_t value)
{
    switch (port) {
    case 0x1f0: write_data_byte(value); break;
    case 0x1f2: count_ = value; break;
    case 0x1f3: sector_ = value; break;
    case 0x1f4: cylinder_ = uint16_t((cylinder_ & 0xff00) | value); break;
    case 0x1f5: cylinder_ = uint16_t((cylinder_ & 0x00ff) | (uint16_t(value) << 8)); break;
    case 0x1f6:
        drive_head_ = value;
        if (!selected()) {
            transfer_ = Transfer::idle;
            irq_ = false;
            status_ = 0;
        } else if (transfer_ == Transfer::idle) {
            status_ = ready;
        }
        break;
    case 0x1f7: command(value); break;
    case 0x3f6:
        if (value & software_reset) {
            status_ = selected() ? busy : 0;
            transfer_ = Transfer::idle;
            irq_ = false;
        } else if (control_ & software_reset) {
            reset();
        }
        if (value & disable_irq) {
            irq_ = false;
        }
        control_ = value;
        break;
    }
}
