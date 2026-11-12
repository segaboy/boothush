#include "pc_at_devices.h"

#include <algorithm>

namespace {

constexpr unsigned cascade_line = 2;
constexpr std::uint8_t cascade_bit = 1u << cascade_line;
constexpr std::uint32_t cpu_hz = 6000000;
constexpr std::uint32_t rtc_hz = 32768;
constexpr std::uint8_t rtc_set = 0x80;
constexpr std::uint8_t rtc_binary = 0x04;
constexpr std::uint8_t rtc_24hour = 0x02;
constexpr std::uint8_t rtc_periodic = 0x40;
constexpr std::uint8_t rtc_alarm = 0x20;
constexpr std::uint8_t rtc_update = 0x10;

constexpr std::uint8_t line_bit(unsigned line)
{
    return static_cast<std::uint8_t>(1u << line);
}

constexpr unsigned from_bcd(unsigned value)
{
    return ((value >> 4) & 15) * 10 + (value & 15);
}

constexpr unsigned to_bcd(unsigned value)
{
    return (value / 10 * 16) + (value % 10);
}

unsigned period_ticks(unsigned rate)
{
    switch (rate) {
    case 0: return 0;
    case 1: return 256;
    case 2: return 128;
    default: return 1u << (rate - 1);
    }
}

} // namespace

int PcAtDevices::Pic::highest(std::uint8_t bits) const
{
    for (unsigned priority = 1; priority <= 8; ++priority) {
        const unsigned line = (lowest_priority + priority) & 7;
        if ((bits & line_bit(line)) != 0) {
            return static_cast<int>(line);
        }
    }
    return -1;
}

int PcAtDevices::Pic::pending(std::uint8_t external) const
{
    if (init != Init::ready) {
        return -1;
    }
    const auto requests = static_cast<std::uint8_t>((irr | external) & ~mask);
    if (requests == 0) {
        return -1;
    }
    const int selected = highest(static_cast<std::uint8_t>(requests | isr));
    if (selected < 0 || (isr & line_bit(static_cast<unsigned>(selected))) != 0) {
        return -1;
    }
    return selected;
}

void PcAtDevices::Pic::acknowledge(unsigned line)
{
    const auto bit = line_bit(line);
    irr &= static_cast<std::uint8_t>(~bit);
    if (!auto_eoi) {
        isr |= bit;
    } else if (auto_rotate) {
        lowest_priority = line;
    }
}

std::uint8_t PcAtDevices::Pic::read_command(std::uint8_t external)
{
    if (!poll) {
        return read_isr ? isr : static_cast<std::uint8_t>(irr | external);
    }
    poll = false;
    const int selected = pending(external);
    if (selected < 0) {
        return 0;
    }
    acknowledge(static_cast<unsigned>(selected));
    return static_cast<std::uint8_t>(0x80 | selected);
}

void PcAtDevices::Pic::write_command(std::uint8_t value)
{
    if ((value & 0x10) != 0) {
        irr = isr = mask = 0;
        lowest_priority = 7;
        read_isr = poll = auto_eoi = auto_rotate = false;
        single = (value & 2) != 0;
        needs_icw4 = (value & 1) != 0;
        init = Init::vector;
        return;
    }
    if ((value & 8) != 0) {
        if ((value & 2) != 0) {
            read_isr = (value & 1) != 0;
        }
        poll = (value & 4) != 0;
        return;
    }

    const unsigned command = value >> 5;
    const unsigned line = value & 7;
    switch (command) {
    case 0:
    case 4:
        auto_rotate = command == 4;
        break;
    case 1:
    case 5:
        if (const int active = highest(isr); active >= 0) {
            isr &= static_cast<std::uint8_t>(~line_bit(static_cast<unsigned>(active)));
            if (command == 5) {
                lowest_priority = static_cast<unsigned>(active);
            }
        }
        break;
    case 3:
    case 7:
        isr &= static_cast<std::uint8_t>(~line_bit(line));
        if (command == 7) {
            lowest_priority = line;
        }
        break;
    case 6:
        lowest_priority = line;
        break;
    }
}

void PcAtDevices::Pic::write_data(std::uint8_t value)
{
    switch (init) {
    case Init::vector:
        vector_base = value & 0xf8;
        init = single ? (needs_icw4 ? Init::mode : Init::ready) : Init::cascade;
        break;
    case Init::cascade:
        // The PC/AT connects the slave to the master's IRQ2.
        init = needs_icw4 ? Init::mode : Init::ready;
        break;
    case Init::mode:
        auto_eoi = (value & 2) != 0;
        init = Init::ready;
        break;
    case Init::ready:
        mask = value;
        break;
    }
}

void PcAtDevices::Counter::program(std::uint8_t control)
{
    access = (control >> 4) & 3;
    mode = (control >> 1) & 7;
    if (mode >= 6) {
        mode -= 4;
    }
    bcd = (control & 1) != 0;
    loaded = triggered = write_high = read_high = latch_high = false;
    count_latch.reset();
    status_latch.reset();
    phase = 0;
}

void PcAtDevices::Counter::load(std::uint16_t value)
{
    if (bcd) {
        reload = from_bcd(value >> 8) * 100 + from_bcd(value);
        reload = reload == 0 ? 10000 : std::min(reload, 9999u);
    } else {
        reload = value == 0 ? 65536 : value;
    }
    if (mode == 2 || mode == 3) {
        reload = std::max(reload, 2u);
    }
    phase = 0;
    loaded = true;
    triggered = mode != 1 && mode != 5;
}

void PcAtDevices::Counter::write(std::uint8_t value)
{
    if (access == 1) {
        load(value);
    } else if (access == 2) {
        load(static_cast<std::uint16_t>(static_cast<unsigned>(value) << 8));
    } else if (write_high) {
        load(static_cast<std::uint16_t>(partial | (static_cast<unsigned>(value) << 8)));
        write_high = false;
    } else {
        partial = value;
        write_high = true;
    }
}

std::uint16_t PcAtDevices::Counter::count() const
{
    std::uint32_t value = reload - std::min(phase, reload);
    if (mode == 3) {
        const std::uint32_t high_ticks = (reload + 1) / 2;
        if (phase >= high_ticks) {
            value = (reload & ~1u) - 2 * (phase - high_ticks);
        } else if (phase != 0) {
            value = reload + (reload & 1) - 2 * phase;
        }
    }
    if (bcd) {
        value = (to_bcd(value / 100 % 100) << 8) | to_bcd(value % 100);
    }
    return static_cast<std::uint16_t>(value);
}

std::uint8_t PcAtDevices::Counter::read()
{
    if (status_latch) {
        const auto value = *status_latch;
        status_latch.reset();
        return value;
    }
    const std::uint16_t value = count_latch ? *count_latch : count();
    bool &high = count_latch ? latch_high : read_high;
    const bool read_msb = access == 2 || (access == 3 && high);
    if (access == 3) {
        high = !high;
    }
    if (access != 3 || read_msb) {
        count_latch.reset();
    }
    return static_cast<std::uint8_t>(read_msb ? value >> 8 : value);
}

void PcAtDevices::Counter::latch_count()
{
    if (!count_latch) {
        count_latch = count();
        latch_high = false;
    }
}

void PcAtDevices::Counter::latch_status()
{
    if (!status_latch) {
        status_latch = static_cast<std::uint8_t>((output() ? 0x80 : 0) | (!loaded ? 0x40 : 0) |
                       (access << 4) | (mode << 1) | (bcd ? 1 : 0));
    }
}

void PcAtDevices::Counter::set_gate(bool enabled)
{
    if (enabled && !gate) {
        if (mode == 1 || mode == 5) {
            triggered = loaded;
        }
        if (mode != 0 && mode != 4) {
            phase = 0;
        }
    }
    gate = enabled;
}

bool PcAtDevices::Counter::output() const
{
    switch (mode) {
    case 0: return loaded && phase >= reload;
    case 1: return !triggered || phase >= reload;
    case 2: return !loaded || !gate || phase + 1 != reload;
    case 3: return !loaded || !gate || phase < (reload + 1) / 2;
    default: return !triggered || phase != reload;
    }
}

bool PcAtDevices::Counter::will_rise() const
{
    if (!loaded || !triggered || (!gate && mode != 1 && mode != 5)) {
        return false;
    }
    const auto terminal = reload + (mode >= 4 ? 1 : 0);
    return mode == 2 || mode == 3 || phase < terminal;
}

std::uint32_t PcAtDevices::Counter::advance(std::uint32_t ticks)
{
    if (!will_rise()) {
        return 0;
    }
    const std::uint64_t next = std::uint64_t(phase) + ticks;
    if (mode == 2 || mode == 3) {
        phase = static_cast<std::uint32_t>(next % reload);
        return static_cast<std::uint32_t>(next / reload);
    }
    const auto terminal = reload + (mode >= 4 ? 1 : 0);
    phase = static_cast<std::uint32_t>(std::min<std::uint64_t>(next, terminal));
    return phase == terminal ? 1 : 0;
}

PcAtDevices::PcAtDevices()
{
    pit_[1].program(0x34);
    pit_[1].load(18); // Approximately 15 microseconds between refresh requests.
    pit_[2].gate = false;

    cmos_[0x06] = 1; // Sunday, January 1, 1984.
    cmos_[0x07] = 1;
    cmos_[0x08] = 1;
    cmos_[0x09] = 0x84;
    cmos_[0x0a] = 0x26;
    cmos_[0x0b] = rtc_24hour;
    cmos_[0x0d] = 0x80;
    cmos_[0x15] = 0x80;
    cmos_[0x16] = 2;
    cmos_[0x32] = 0x19;
    configure_drives(false, false);
}

void PcAtDevices::configure_drives(bool floppy, bool hard_disk)
{
    cmos_[0x10] = floppy ? 0x20 : 0; // A: is type 2, 1.2 MB.
    cmos_[0x12] = hard_disk ? 0x20 : 0; // C: is IBM drive type 2.
    cmos_[0x14] = floppy ? 1 : 0; // EGA and at most one floppy drive.
    unsigned checksum = 0;
    for (unsigned address = 0x10; address <= 0x2d; ++address) {
        checksum += cmos_[address];
    }
    cmos_[0x2e] = static_cast<std::uint8_t>(checksum >> 8);
    cmos_[0x2f] = static_cast<std::uint8_t>(checksum);
}

bool PcAtDevices::handles_port(std::uint16_t port) const
{
    return (port >= 0x40 && port <= 0x43) || port == 0x20 || port == 0x21 ||
           port == 0xa0 || port == 0xa1 || port == 0x61 || port == 0x70 || port == 0x71;
}

std::uint8_t PcAtDevices::read_port(std::uint16_t port)
{
    switch (port) {
    case 0x20: return master_.read_command(cascade_request());
    case 0x21: return master_.mask;
    case 0x40:
    case 0x41:
    case 0x42: return pit_[port - 0x40u].read();
    case 0x61:
        return static_cast<std::uint8_t>(system_control_ | (refresh_ ? 0x10 : 0) |
                                        (pit_[2].output() ? 0x20 : 0));
    case 0x70: return cmos_address_;
    case 0x71: return read_cmos();
    case 0xa0: return slave_.read_command();
    case 0xa1: return slave_.mask;
    default: return 0xff;
    }
}

void PcAtDevices::write_pit_control(std::uint8_t value)
{
    const unsigned selected = value >> 6;
    if (selected < pit_.size()) {
        if ((value & 0x30) == 0) {
            pit_[selected].latch_count();
        } else {
            pit_[selected].program(value);
        }
        return;
    }
    for (unsigned index = 0; index < pit_.size(); ++index) {
        if ((value & (2u << index)) == 0) {
            continue;
        }
        if ((value & 0x20) == 0) {
            pit_[index].latch_count();
        }
        if ((value & 0x10) == 0) {
            pit_[index].latch_status();
        }
    }
}

void PcAtDevices::write_port(std::uint16_t port, std::uint8_t value)
{
    if (port >= 0x40 && port <= 0x43) {
        const bool was_high = pit_[0].output();
        if (port == 0x43) {
            write_pit_control(value);
        } else {
            pit_[port - 0x40u].write(value);
        }
        if (!was_high && pit_[0].output()) {
            pulse_irq(0);
        }
        return;
    }
    switch (port) {
    case 0x20: master_.write_command(value); break;
    case 0x21: master_.write_data(value); break;
    case 0x61:
        system_control_ = value & 0x0f;
        pit_[2].set_gate((value & 1) != 0);
        break;
    case 0x70: cmos_address_ = value; break;
    case 0x71: write_cmos(value); break;
    case 0xa0: slave_.write_command(value); break;
    case 0xa1: slave_.write_data(value); break;
    }
}

void PcAtDevices::pulse_irq(unsigned irq)
{
    if (irq < 8) {
        master_.irr |= line_bit(irq);
    } else if (irq < 16) {
        slave_.irr |= line_bit(irq - 8);
    }
}

std::uint8_t PcAtDevices::cascade_request() const
{
    return slave_.pending() >= 0 ? cascade_bit : 0;
}

bool PcAtDevices::interrupt_pending() const
{
    return master_.pending(cascade_request()) >= 0;
}

bool PcAtDevices::timed_interrupt_possible() const
{
    if (pit_[0].will_rise() && master_.pending(1) == 0) {
        return true;
    }
    const auto enables = cmos_[0x0b];
    const bool periodic = (enables & rtc_periodic) != 0 && (cmos_[0x0a] & 15) != 0;
    const bool calendar = (enables & rtc_set) == 0 && (enables & (rtc_alarm | rtc_update)) != 0;
    return !rtc_irq_ && rtc_running() && (periodic || calendar) && slave_.pending(1) == 0 &&
           master_.pending(cascade_bit) == static_cast<int>(cascade_line);
}

std::uint8_t PcAtDevices::acknowledge_interrupt()
{
    const int line = master_.pending(cascade_request());
    if (line < 0) {
        return static_cast<std::uint8_t>(master_.vector_base + 7);
    }
    master_.acknowledge(static_cast<unsigned>(line));
    if (line == static_cast<int>(cascade_line)) {
        const int subordinate = slave_.pending();
        if (subordinate >= 0) {
            slave_.acknowledge(static_cast<unsigned>(subordinate));
            return static_cast<std::uint8_t>(slave_.vector_base + subordinate);
        }
    }
    return static_cast<std::uint8_t>(master_.vector_base + line);
}

bool PcAtDevices::rtc_running() const
{
    return (cmos_[0x0a] & 0x70) == 0x20;
}

std::uint8_t PcAtDevices::rtc_register(std::uint8_t index) const
{
    return cmos_[index & 0x3f];
}

std::uint8_t PcAtDevices::read_cmos()
{
    const unsigned address = cmos_address_ & 0x3f;
    const auto value = cmos_[address];
    if (address == 0x0a && rtc_running() && (cmos_[0x0b] & rtc_set) == 0 &&
        rtc_phase_ >= rtc_hz - 8) {
        return value | 0x80;
    }
    if (address == 0x0c) {
        cmos_[address] = 0;
        update_rtc_irq();
    }
    return value;
}

void PcAtDevices::write_cmos(std::uint8_t value)
{
    const unsigned address = cmos_address_ & 0x3f;
    switch (address) {
    case 0x0a:
        if (((cmos_[address] ^ value) & 0x70) != 0) {
            rtc_phase_ = rtc_fraction_ = 0;
        }
        cmos_[address] = value & 0x7f;
        break;
    case 0x0b:
        cmos_[address] = value;
        update_rtc_irq();
        break;
    case 0x0c:
    case 0x0d:
        break;
    default:
        cmos_[address] = value;
        break;
    }
}

void PcAtDevices::update_rtc_irq()
{
    auto &status = cmos_[0x0c];
    const bool pending = (status & cmos_[0x0b] & 0x70) != 0;
    status = static_cast<std::uint8_t>((status & 0x70) | (pending ? 0x80 : 0));
    if (pending && !rtc_irq_) {
        pulse_irq(8);
    }
    rtc_irq_ = pending;
}

void PcAtDevices::tick_calendar()
{
    const bool binary = (cmos_[0x0b] & rtc_binary) != 0;
    const bool hour24 = (cmos_[0x0b] & rtc_24hour) != 0;
    const auto decode = [binary](std::uint8_t value) { return binary ? unsigned(value) : from_bcd(value); };
    const auto encode = [binary](unsigned value) {
        return static_cast<std::uint8_t>(binary ? value : to_bcd(value));
    };

    unsigned second = std::min(decode(cmos_[0x00]), 59u) + 1;
    unsigned minute = std::min(decode(cmos_[0x02]), 59u) + second / 60;
    unsigned hour = decode(cmos_[0x04] & 0x7f);
    if (!hour24) {
        hour = hour % 12 + ((cmos_[0x04] & 0x80) != 0 ? 12 : 0);
    }
    hour = std::min(hour, 23u) + minute / 60;
    cmos_[0x00] = encode(second % 60);
    cmos_[0x02] = encode(minute % 60);
    if (hour24) {
        cmos_[0x04] = encode(hour % 24);
    } else {
        const unsigned hour12 = (hour + 11) % 12 + 1;
        cmos_[0x04] = static_cast<std::uint8_t>(encode(hour12) | (hour % 24 >= 12 ? 0x80 : 0));
    }

    if (hour == 24) {
        constexpr unsigned month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        unsigned month = std::clamp(decode(cmos_[0x08]), 1u, 12u);
        unsigned year = decode(cmos_[0x09]) % 100;
        unsigned century = from_bcd(cmos_[0x32]);
        const bool leap = year % 4 == 0 && (year != 0 || century % 4 == 0);
        const unsigned last = month_days[month - 1] + (month == 2 && leap ? 1 : 0);
        unsigned day = std::clamp(decode(cmos_[0x07]), 1u, last) + 1;
        if (day > last) {
            day = 1;
            if (++month == 13) {
                month = 1;
                if (++year == 100) {
                    year = 0;
                    century = (century + 1) % 100;
                }
            }
        }
        cmos_[0x06] = encode(std::clamp(decode(cmos_[0x06]), 1u, 7u) % 7 + 1);
        cmos_[0x07] = encode(day);
        cmos_[0x08] = encode(month);
        cmos_[0x09] = encode(year);
        cmos_[0x32] = static_cast<std::uint8_t>(to_bcd(century));
    }

    cmos_[0x0c] |= rtc_update;
    const auto alarm_matches = [this](unsigned reg) {
        const auto alarm = cmos_[reg + 1];
        return (alarm & 0xc0) == 0xc0 || alarm == cmos_[reg];
    };
    if (alarm_matches(0x00) && alarm_matches(0x02) && alarm_matches(0x04)) {
        cmos_[0x0c] |= rtc_alarm;
    }
}

void PcAtDevices::advance_rtc(std::uint32_t ticks)
{
    const auto next = rtc_phase_ + ticks;
    const auto period = period_ticks(cmos_[0x0a] & 15);
    if (period != 0 && next / period != rtc_phase_ / period) {
        cmos_[0x0c] |= rtc_periodic;
    }
    if ((cmos_[0x0b] & rtc_set) == 0) {
        for (auto second = next / rtc_hz; second != 0; --second) {
            tick_calendar();
        }
    }
    rtc_phase_ = next % rtc_hz;
    update_rtc_irq();
}

void PcAtDevices::advance_cycles(std::uint32_t cycles)
{
    // PIT crystal is 315/22 MHz divided by 12, or 35/176 of the guest CPU clock.
    const auto pit_total = std::uint64_t(cycles) * 35 + pit_fraction_;
    pit_fraction_ = static_cast<std::uint32_t>(pit_total % 176);
    const auto pit_ticks = static_cast<std::uint32_t>(pit_total / 176);
    if (pit_ticks != 0) {
        if (pit_[0].advance(pit_ticks) != 0) {
            pulse_irq(0);
        }
        if ((pit_[1].advance(pit_ticks) & 1) != 0) {
            refresh_ = !refresh_;
        }
        pit_[2].advance(pit_ticks);
    }

    if (rtc_running()) {
        const auto rtc_total = std::uint64_t(cycles) * rtc_hz + rtc_fraction_;
        rtc_fraction_ = static_cast<std::uint32_t>(rtc_total % cpu_hz);
        const auto rtc_ticks = static_cast<std::uint32_t>(rtc_total / cpu_hz);
        if (rtc_ticks != 0) {
            advance_rtc(rtc_ticks);
        }
    }
}
