#include "pcat_keyboard.h"

#include <cstdlib>
#include <iostream>

namespace {

constexpr unsigned data = 0x60;
constexpr unsigned control = 0x64;

void require(bool result, const char *message)
{
    if (!result) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void configure(PcatKeyboard &keyboard, std::uint8_t configuration)
{
    keyboard.write_port(control, 0x60);
    keyboard.write_port(data, configuration);
}

void expect_output(PcatKeyboard &keyboard, std::uint8_t expected, const char *message)
{
    require((keyboard.read_port(control) & 1) != 0, "expected a buffered keyboard byte");
    require(keyboard.read_port(data) == expected, message);
}

void test_controller_startup()
{
    PcatKeyboard keyboard;
    require((keyboard.read_port(control) & 0x03) == 0, "unexpected power-on output or busy input");
    require(keyboard.read_port(data) == 0xff, "empty data port did not return open bus");
    require(keyboard.a20_enabled(), "A20 must preserve the 286 reset vector at power-on");

    keyboard.write_port(control, 0xad);
    keyboard.write_port(control, 0xa7);
    keyboard.write_port(control, 0x20);
    require((keyboard.read_port(control) & 0x09) == 0x09, "status did not report command and output");
    expect_output(keyboard, 0x30, "controller readback did not report disabled interfaces");
    require(!keyboard.inject_scancode(0x1c), "disabled keyboard accepted a scan byte");

    configure(keyboard, 0x30);
    require((keyboard.read_port(control) & 0x08) == 0, "status did not identify data write");
    keyboard.write_port(control, 0xd0);
    const auto before_test = keyboard.read_port(data);
    keyboard.write_port(control, 0xaa);
    expect_output(keyboard, 0x55, "controller self-test failed");
    require((keyboard.read_port(control) & 0x04) != 0, "self-test did not set system flag");
    keyboard.write_port(control, 0xd0);
    require((keyboard.read_port(data) & 3) == (before_test & 3), "self-test changed reset or A20");
    keyboard.write_port(control, 0xab);
    expect_output(keyboard, 0x00, "keyboard interface test failed");
    keyboard.write_port(control, 0xae);

    keyboard.write_port(data, 0xff);
    expect_output(keyboard, 0xfa, "keyboard reset did not acknowledge");
    expect_output(keyboard, 0xaa, "keyboard reset did not complete BAT");
    require(!keyboard.take_irq1(), "keyboard raised disabled IRQ1");
    keyboard.write_port(data, 0xf2);
    expect_output(keyboard, 0xfa, "keyboard identify did not acknowledge");
    expect_output(keyboard, 0xab, "keyboard identify prefix changed");
    expect_output(keyboard, 0x83, "keyboard identify returned the wrong keyboard");
}

void test_a20_and_reset()
{
    PcatKeyboard keyboard;
    keyboard.write_port(control, 0xd1);
    keyboard.write_port(data, 1);
    require(!keyboard.a20_enabled(), "controller failed to disable A20");
    require(!keyboard.take_reset_request(), "an output port with reset high reset the CPU");

    keyboard.write_port(control, 0xd1);
    keyboard.write_port(data, 2);
    require(keyboard.a20_enabled(), "controller failed to enable A20");
    require(keyboard.take_reset_request(), "an output port with reset low did not request reset");
    require(!keyboard.take_reset_request(), "reset request did not clear after polling");
    keyboard.write_port(control, 0xd0);
    require((keyboard.read_port(data) & 3) == 3, "reset pulse did not release the reset line");

    keyboard.write_port(control, 0xff);
    require(!keyboard.take_reset_request(), "command FF should not pulse any output");
    keyboard.write_port(control, 0xfe);
    require(keyboard.take_reset_request(), "command FE did not pulse CPU reset");
    require(keyboard.a20_enabled(), "reset pulse altered A20");
}

void test_scan_commands()
{
    PcatKeyboard keyboard;
    keyboard.write_port(data, 0xf5);
    expect_output(keyboard, 0xfa, "disable scanning did not acknowledge");
    require(!keyboard.inject_scancode(0x1c), "disabled scanning accepted a scan byte");

    keyboard.write_port(data, 0xf0);
    expect_output(keyboard, 0xfa, "scan set command did not acknowledge");
    keyboard.write_port(data, 3);
    expect_output(keyboard, 0xfe, "unsupported scan set did not request resend");
    keyboard.write_port(data, 2);
    expect_output(keyboard, 0xfa, "scan set two was not accepted");
    keyboard.write_port(data, 0xf0);
    expect_output(keyboard, 0xfa, "scan set query did not acknowledge");
    keyboard.write_port(data, 0);
    expect_output(keyboard, 0xfa, "scan set query parameter did not acknowledge");
    expect_output(keyboard, 2, "scan set query returned the wrong mode");

    keyboard.write_port(data, 0xed);
    expect_output(keyboard, 0xfa, "LED command did not acknowledge");
    keyboard.write_port(data, 7);
    expect_output(keyboard, 0xfa, "LED argument did not acknowledge");
    keyboard.write_port(data, 0xf3);
    expect_output(keyboard, 0xfa, "typematic command did not acknowledge");
    keyboard.write_port(data, 0x20);
    expect_output(keyboard, 0xfa, "typematic argument did not acknowledge");
    keyboard.write_port(data, 0xee);
    expect_output(keyboard, 0xee, "echo was wrong");
    keyboard.write_port(data, 0xfe);
    expect_output(keyboard, 0xee, "resend did not repeat the previous keyboard reply");

    keyboard.write_port(data, 0xf4);
    expect_output(keyboard, 0xfa, "enable scanning did not acknowledge");
    require(keyboard.inject_scancode(0xf0), "set-2 release prefix was refused");
    require(keyboard.inject_scancode(0x1c), "set-2 release byte was refused");
    expect_output(keyboard, 0xf0, "untranslated release prefix changed");
    expect_output(keyboard, 0x1c, "untranslated release byte changed");
    keyboard.write_port(data, 0xfe);
    expect_output(keyboard, 0x1c, "resend did not repeat the previous scan byte");
}

void test_irq_and_translation()
{
    PcatKeyboard keyboard;
    require(keyboard.inject_scancode(0x1c), "scan byte was refused at power-on");
    require(!keyboard.take_irq1(), "IRQ1 was enabled before configuration");
    configure(keyboard, 0x41);
    require(keyboard.take_irq1(), "enabling IRQ1 did not signal existing data");
    require(!keyboard.take_irq1(), "the same buffered byte signalled twice");
    expect_output(keyboard, 0x1c, "changing translation retroactively modified queued data");

    for (const std::uint8_t byte : {0x1c, 0xf0, 0x1c, 0xe0, 0xf0, 0x14, 0x83}) {
        require(keyboard.inject_scancode(byte), "translated scan input was refused");
    }
    for (const std::uint8_t byte : {0x1e, 0x9e, 0xe0, 0x9d, 0x41}) {
        require(keyboard.take_irq1(), "output latch did not signal the next translated byte");
        require(!keyboard.take_irq1(), "the same translated byte signalled twice");
        expect_output(keyboard, byte, "set-1 translation mismatch");
    }
    require(!keyboard.take_irq1(), "empty output latch signalled IRQ1");

    for (const std::uint8_t byte : {0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77}) {
        require(keyboard.inject_scancode(byte), "Pause input was refused");
    }
    for (const std::uint8_t byte : {0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5}) {
        expect_output(keyboard, byte, "Pause translation mismatch");
    }
}

void test_bounded_fifo()
{
    PcatKeyboard keyboard;
    for (unsigned index = 0; index < 31; ++index) {
        require(keyboard.inject_scancode(std::uint8_t(index)), "FIFO rejected input with capacity");
    }
    keyboard.write_port(data, 0xff); // No room for both ACK and BAT.
    require(keyboard.inject_scancode(0x7f), "partial command reply consumed the last slot");
    require(!keyboard.inject_scancode(0x1c), "FIFO accepted more bytes than it can hold");
    for (unsigned index = 0; index < 31; ++index) {
        expect_output(keyboard, std::uint8_t(index), "overflow changed the queued scan order");
    }
    expect_output(keyboard, 0x7f, "overflow changed the final byte");
    require((keyboard.read_port(control) & 1) == 0, "FIFO did not empty");
}

} // namespace

int main()
{
    test_controller_startup();
    test_a20_and_reset();
    test_scan_commands();
    test_irq_and_translation();
    test_bounded_fifo();
}
