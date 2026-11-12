#include "mac_rtc.h"

#include <cstdlib>
#include <iostream>

namespace {

constexpr uint8_t data = 0x01;
constexpr uint8_t clock = 0x02;
constexpr uint8_t disable = 0x04;

void require(bool result, const char *message)
{
    if (!result) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct SerialBus {
    MacRtc rtc;

    void select() { rtc.set_port_b(clock, disable | clock | data); }
    void deselect() { rtc.set_port_b(disable | clock, disable | clock | data); }

    void send(uint8_t value, unsigned bit_count = 8)
    {
        for (unsigned bit = 0; bit < bit_count; ++bit) {
            rtc.set_port_b(0, disable | clock | data);
            const uint8_t level = (value >> (7 - bit)) & data;
            rtc.set_port_b(level, disable | clock | data);
            rtc.set_port_b(level | clock, disable | clock | data);
        }
    }

    void write(uint8_t command, uint8_t value)
    {
        select();
        send(command);
        send(value);
        deselect();
    }

    uint8_t read(uint8_t command)
    {
        select();
        send(command | 0x80);
        rtc.set_port_b(clock, disable | clock);
        require(rtc.data_high(), "RTC drove data before the first read edge");

        uint8_t value = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            rtc.set_port_b(0, disable | clock);
            value = static_cast<uint8_t>((value << 1) | rtc.data_high());
            rtc.set_port_b(clock, disable | clock);
        }
        require(rtc.data_high(), "RTC did not release data after the byte");
        deselect();
        return value;
    }
};

uint8_t pram_command(unsigned address)
{
    return static_cast<uint8_t>(address < 16 ? 0x41 + address * 4 : 0x21 + (address - 16) * 4);
}

void test_pram_and_write_protection()
{
    SerialBus bus;
    for (unsigned address = 0; address < 20; ++address) {
        require(bus.read(pram_command(address)) == 0, "PRAM did not start cleared");
        bus.write(pram_command(address), static_cast<uint8_t>(0x91 + address));
    }
    for (unsigned address = 0; address < 20; ++address) {
        require(bus.read(pram_command(address)) == 0x91 + address, "PRAM addresses overlapped");
    }

    bus.write(0x35, 0x80);
    bus.write(0x41, 0);
    bus.write(0x01, 0x55);
    require(bus.read(0x41) == 0x91, "protected PRAM was overwritten");
    require(bus.read(0x01) == 0, "protected seconds byte was overwritten");
    require(bus.read(0x35) == 0xff, "write protection register was readable");
    bus.write(0x35, 0x7f);
    bus.write(0x41, 0x18);
    require(bus.read(0x41) == 0x18, "RTC did not clear write protection");
}

void test_clock()
{
    SerialBus bus;
    const uint64_t period = MacRtc::cycles_per_second;
    require(!bus.rtc.advance(period - 1), "early RTC second");
    require(bus.read(0x01) == 0 && bus.read(0x0d) == 0, "RTC did not start at the Mac epoch");
    require(bus.rtc.advance(period), "RTC second did not elapse");
    require(!bus.rtc.advance(period), "RTC repeated the same second");
    require(bus.read(0x01) == 1, "RTC low byte did not increment");
    require(bus.rtc.advance(period * 4 + 1), "RTC did not catch up across seconds");
    require(bus.read(0x01) == 4, "RTC lost elapsed seconds");

    bus.write(0x01, 0xfe);
    bus.write(0x05, 0xff);
    bus.write(0x09, 0xff);
    bus.write(0x0d, 0xff);
    require(bus.read(0x11) == 0xfe, "RTC seconds alias differed");
    bus.write(0x35, 0x80);
    require(bus.rtc.advance(period * 5), "write protection stopped the oscillator");
    require(bus.read(0x01) == 0xff, "RTC did not apply a guest seconds write");
    require(bus.rtc.advance(period * 6), "RTC did not tick at counter rollover");
    for (uint8_t command : {0x01, 0x05, 0x09, 0x0d}) {
        require(bus.read(command) == 0, "RTC did not wrap its 32-bit counter");
    }
}

void test_incomplete_and_unsupported_commands()
{
    SerialBus bus;
    require(bus.rtc.data_high(), "inactive RTC data line should be released");
    bus.select();
    bus.send(0x41, 5);
    bus.deselect();

    bus.select();
    bus.send(0x41);
    bus.send(0xb6, 7);
    // Making the enable pin an input pulls it high and aborts this write.
    bus.rtc.set_port_b(clock, clock | data);
    require(bus.read(0x41) == 0, "incomplete PRAM write was committed");

    bus.write(0x40, 0xb6);
    require(bus.read(0x41) == 0, "RTC accepted a command without trailing 01");
    require(bus.read(0x39) == 0xff, "original RTC accepted an extended PRAM command");
    bus.write(0x41, 0xb6);
    require(bus.read(0x41) == 0xb6, "RTC failed to recover after an aborted transaction");
}

} // namespace

int main()
{
    test_pram_and_write_protection();
    test_clock();
    test_incomplete_and_unsupported_commands();
}
