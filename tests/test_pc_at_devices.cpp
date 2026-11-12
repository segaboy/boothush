#include "pc_at_devices.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void init_pics(PcAtDevices &at, std::uint8_t master_mask, std::uint8_t slave_mask)
{
    at.write_port(0x20, 0x11);
    at.write_port(0x21, 0x08);
    at.write_port(0x21, 0x04);
    at.write_port(0x21, 0x01);
    at.write_port(0x21, master_mask);
    at.write_port(0xa0, 0x11);
    at.write_port(0xa1, 0x70);
    at.write_port(0xa1, 0x02);
    at.write_port(0xa1, 0x01);
    at.write_port(0xa1, slave_mask);
}

void test_pic()
{
    PcAtDevices at;
    init_pics(at, 0xba, 0xfe); // Master IRQ0, IRQ2 and IRQ6; slave IRQ8.
    at.pulse_irq(1);
    require(!at.interrupt_pending(), "masked master IRQ was presented to CPU");
    require((at.read_port(0x20) & 2) != 0, "masked IRQ did not latch in IRR");
    at.pulse_irq(6);
    at.pulse_irq(8);
    require(at.acknowledge_interrupt() == 0x70, "master IRQ2 did not prioritize cascaded RTC");
    require(!at.interrupt_pending(), "lower-priority IRQ bypassed in-service cascade");

    at.write_port(0x20, 0x0b);
    at.write_port(0xa0, 0x0b);
    require(at.read_port(0x20) == 4 && at.read_port(0xa0) == 1, "cascade did not set both ISRs");
    at.pulse_irq(0);
    require(at.acknowledge_interrupt() == 0x08, "higher-priority IRQ could not interrupt IRQ8");
    at.write_port(0x20, 0x60); // Specific EOI, IRQ0.
    require(!at.interrupt_pending(), "specific EOI cleared an unrelated ISR bit");
    at.write_port(0xa0, 0x20);
    at.write_port(0x20, 0x20);
    require(at.acknowledge_interrupt() == 0x0e, "both cascade EOIs did not unblock IRQ6");
    at.write_port(0x20, 0x20);
    require(at.acknowledge_interrupt() == 0x0f, "empty acknowledgement did not report spurious IRQ7");

    at.write_port(0x21, 0xb8); // Unmask the pending IRQ1.
    at.write_port(0x20, 0x0c); // Poll command.
    require(at.read_port(0x20) == 0x81, "PIC poll did not acknowledge IRQ1");
    require(at.read_port(0x20) == 2, "poll did not enter IRQ1 in service");
}

void test_pit()
{
    PcAtDevices at;
    init_pics(at, 0xfe, 0xff);
    require(!at.timed_interrupt_possible(), "unprogrammed PIT had a future IRQ");
    at.write_port(0x43, 0x34);
    at.write_port(0x40, 10);
    at.write_port(0x40, 0);
    require(at.timed_interrupt_possible(), "running PIT0 could not wake CPU");

    at.write_port(0x43, 0);
    at.advance_cycles(25);
    require(at.read_port(0x40) == 10 && at.read_port(0x40) == 0, "counter latch changed with time");
    at.write_port(0x43, 0xc2); // Latch status and count for channel 0.
    require(at.read_port(0x40) == 0xb4, "8254 read-back status differs from programmed mode");
    require(at.read_port(0x40) == 6 && at.read_port(0x40) == 0, "8254 read-back count was wrong");

    at.advance_cycles(25);
    require(!at.interrupt_pending(), "PIT interrupted before the rising output edge");
    at.write_port(0x43, 0xe2); // Status only.
    require(at.read_port(0x40) == 0x34, "mode 2 output was not low on the last tick");
    at.advance_cycles(1);
    require(at.acknowledge_interrupt() == 0x08, "PIT terminal count did not generate IRQ0");
    require(!at.timed_interrupt_possible(), "in-service IRQ0 could interrupt itself");
    at.write_port(0x20, 0x20);
    require(at.timed_interrupt_possible(), "EOI did not permit a future timer IRQ");

    PcAtDevices speaker;
    speaker.write_port(0x43, 0xb6);
    speaker.write_port(0x42, 4);
    speaker.write_port(0x42, 0);
    speaker.advance_cycles(11);
    require((speaker.read_port(0x61) & 0x20) != 0, "disabled speaker gate did not hold output high");
    speaker.write_port(0x61, 3);
    speaker.advance_cycles(10);
    require((speaker.read_port(0x61) & 0x23) == 3, "speaker gate did not start the square wave");
    speaker.write_port(0x61, 0);
    require((speaker.read_port(0x61) & 0x20) != 0, "dropping speaker gate did not restore high output");
    speaker.advance_cycles(70);
    require((speaker.read_port(0x61) & 0x10) != 0, "refresh request did not toggle port 61h bit 4");
}

std::uint8_t read_cmos(PcAtDevices &at, std::uint8_t address)
{
    at.write_port(0x70, address);
    return at.read_port(0x71);
}

void write_cmos(PcAtDevices &at, std::uint8_t address, std::uint8_t data)
{
    at.write_port(0x70, address);
    at.write_port(0x71, data);
}

void test_rtc()
{
    PcAtDevices at;
    at.configure_drives(true, true);
    require(read_cmos(at, 0x10) == 0x20 && read_cmos(at, 0x12) == 0x20, "CMOS disk types were wrong");
    require(read_cmos(at, 0x15) == 0x80 && read_cmos(at, 0x16) == 2, "CMOS did not report 640 KiB");
    require(read_cmos(at, 0x2e) == 0 && read_cmos(at, 0x2f) == 0xc3, "CMOS checksum was wrong");
    require(read_cmos(at, 0x8d) == 0x80 && at.read_port(0x70) == 0x8d, "NMI masking changed CMOS address");

    init_pics(at, 0xfb, 0xfe);
    write_cmos(at, 0x0b, 0x42);
    require(at.timed_interrupt_possible(), "RTC periodic IRQ could not wake CPU");
    at.advance_cycles(5859);
    require(!at.interrupt_pending(), "RTC IRQ fired before the first 1024 Hz period");
    at.advance_cycles(1);
    require(at.acknowledge_interrupt() == 0x70, "RTC did not interrupt through slave IRQ8");
    at.write_port(0xa0, 0x20);
    at.write_port(0x20, 0x20);
    at.advance_cycles(5860);
    require(!at.interrupt_pending(), "RTC generated a second edge without status acknowledgement");
    require(read_cmos(at, 0x0c) == 0xc0 && read_cmos(at, 0x0c) == 0, "RTC status C did not clear on read");
    at.advance_cycles(5860);
    require(at.acknowledge_interrupt() == 0x70, "RTC status acknowledgement did not rearm IRQ8");

    PcAtDevices calendar;
    write_cmos(calendar, 0x0a, 0x20);
    write_cmos(calendar, 0x0b, 0x82); // Freeze BCD calendar writes in 24-hour mode.
    write_cmos(calendar, 0x00, 0x59);
    write_cmos(calendar, 0x02, 0x59);
    write_cmos(calendar, 0x04, 0x23);
    write_cmos(calendar, 0x07, 0x28);
    write_cmos(calendar, 0x08, 2);
    write_cmos(calendar, 0x09, 0);
    write_cmos(calendar, 0x32, 0x20);
    calendar.advance_cycles(6000000);
    require(read_cmos(calendar, 0) == 0x59, "SET did not freeze the calendar");
    write_cmos(calendar, 0x0b, 2);
    calendar.advance_cycles(5999000);
    require((read_cmos(calendar, 0x0a) & 0x80) != 0, "RTC did not report update-in-progress");
    calendar.advance_cycles(1000);
    require(read_cmos(calendar, 0) == 0 && read_cmos(calendar, 4) == 0 &&
            read_cmos(calendar, 7) == 0x29, "BCD calendar did not advance through leap-day midnight");
    require(read_cmos(calendar, 0x0c) == 0x30, "calendar update and midnight alarm flags were wrong");
}

} // namespace

int main()
{
    test_pic();
    test_pit();
    test_rtc();
}
