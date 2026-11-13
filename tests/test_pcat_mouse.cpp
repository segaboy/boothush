#include "pcat_keyboard.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
void command(PcatKeyboard &controller, unsigned c) { controller.write_port(0x64, c); }
void data(PcatKeyboard &controller, unsigned c) { controller.write_port(0x60, c); }
void expect(PcatKeyboard &controller, bool auxiliary, unsigned value)
{
    const auto status = controller.read_port(0x64);
    require((status & 1) != 0, "expected output");
    require(((status & 0x20) != 0) == auxiliary, "incorrect AUX routing flag");
    require(controller.read_port(0x60) == value, "incorrect controller or device byte");
}
void mouse_command(PcatKeyboard &controller, unsigned value)
{
    command(controller, 0xd4); data(controller, value);
}
void start_mouse(PcatKeyboard &controller)
{
    command(controller, 0xad); command(controller, 0xa7);
    command(controller, 0xa9); expect(controller, false, 0);
    command(controller, 0xa8);
    mouse_command(controller, 0xf5); expect(controller, true, 0xfa);
    mouse_command(controller, 0xff); expect(controller, true, 0xfa);
    expect(controller, true, 0xaa); expect(controller, true, 0x00);
    mouse_command(controller, 0xf6); expect(controller, true, 0xfa);
    command(controller, 0x20); expect(controller, false, 0x10);
    command(controller, 0xae);
    mouse_command(controller, 0xf4); expect(controller, true, 0xfa);
}
void packet(PcatKeyboard &controller, unsigned a, unsigned b, unsigned c)
{
    expect(controller, true, a); expect(controller, true, b); expect(controller, true, c);
}
} // namespace

int main()
{
    PcatKeyboard old;
    command(old, 0xa9); expect(old, false, 1);
    require(!old.inject_mouse(0, 0, false), "286 default must remain keyboard-only");

    PcatKeyboard controller(true);
    require(!controller.inject_mouse(0, 0, false), "streaming enabled before startup");
    start_mouse(controller);
    require(controller.inject_mouse(0, 0, false), "neutral report rejected");
    packet(controller, 0x08, 0, 0);
    require(controller.inject_mouse(32, -12, true), "held drag rejected");
    packet(controller, 0x09, 32, 12);
    require(controller.inject_mouse(-10, 8, false), "release movement rejected");
    packet(controller, 0x38, 246, 248);
    require(controller.inject_mouse(255, -255, false), "maximum movement rejected");
    packet(controller, 0x08, 255, 255);
    require(!controller.inject_mouse(256, 0, false), "out-of-range delta accepted");
    require((controller.read_port(0x64) & 1) == 0, "rejected packet queued partial bytes");

    command(controller, 0x60); data(controller, 3); // Enable both IRQs.
    require(controller.inject_scancode(0x1c), "keyboard byte rejected");
    require(controller.inject_mouse(1, 1, true), "interleaved packet rejected");
    require(controller.take_irq1(), "keyboard byte did not request IRQ1");
    require(!controller.take_irq12(), "IRQ12 fired before keyboard byte was read");
    expect(controller, false, 0x1c);
    require(controller.take_irq12(), "auxiliary byte did not request IRQ12");
    packet(controller, 0x29, 1, 255);

    for (unsigned i = 0; i < 30; ++i) require(controller.inject_scancode(0x1c), "queue fill failed");
    require(!controller.inject_mouse(1, 0, true), "partial packet accepted into full queue");
    for (unsigned i = 0; i < 30; ++i) expect(controller, false, 0x1c);
    require((controller.read_port(0x64) & 1) == 0, "partial packet leaked after failed enqueue");
    for (unsigned i = 0; i < 32; ++i) require(controller.inject_scancode(0x1c), "queue fill failed");
    mouse_command(controller, 0xf5);
    require(controller.dropped_aux_replies() == 1, "dropped auxiliary response not counted");
    for (unsigned i = 0; i < 32; ++i) expect(controller, false, 0x1c);
    require(controller.inject_mouse(0, 0, false), "failed command changed streaming state");
    packet(controller, 0x08, 0, 0);
    mouse_command(controller, 0xf5); expect(controller, true, 0xfa);
    require(!controller.inject_mouse(0, 0, false), "disabled device accepted mouse input");
    return 0;
}
