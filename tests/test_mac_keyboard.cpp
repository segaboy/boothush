#include "mac_keyboard.h"

#include <cstdlib>
#include <iostream>

namespace {

constexpr uint8_t shift_in = 0x0c;
constexpr uint8_t shift_out = 0x1c;
constexpr uint64_t command_cycles = 25'068;
constexpr uint64_t reply_cycles = 20'681;
constexpr uint64_t inquiry_cycles = 1'958'400;

void require(bool result, const char *message)
{
    if (!result) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

uint64_t send(MacKeyboard &keyboard, uint8_t command, uint64_t cycle)
{
    keyboard.set_auxiliary_control(shift_out, cycle);
    keyboard.write_shift_register(command, cycle);
    const uint64_t sent = cycle + command_cycles;
    require(!keyboard.advance(sent - 1), "command finished before eight clock periods");
    require(keyboard.advance(sent), "command did not raise shift-complete");
    require(!keyboard.advance(sent), "command raised shift-complete twice");
    keyboard.set_auxiliary_control(shift_in, sent);
    return sent;
}

void receive(MacKeyboard &keyboard, uint64_t start, uint8_t expected)
{
    const uint64_t received = start + reply_cycles;
    require(!keyboard.advance(received - 1), "reply finished before eight clock periods");
    require(keyboard.advance(received), "reply did not raise shift-complete");
    require(keyboard.shift_register() == expected, "shift register contained the wrong response");
    require(!keyboard.advance(received), "reply raised shift-complete twice");
}

void test_commands()
{
    MacKeyboard keyboard;
    require(!keyboard.advance(0), "idle keyboard raised an interrupt");

    auto cycle = send(keyboard, 0x16, 0);
    receive(keyboard, cycle, 0x03);
    cycle = send(keyboard, 0x36, cycle + reply_cycles);
    receive(keyboard, cycle, 0x7d);
    cycle = send(keyboard, 0x14, cycle + reply_cycles);
    receive(keyboard, cycle, 0x7b);

    cycle = send(keyboard, 0x10, cycle + reply_cycles);
    receive(keyboard, cycle + inquiry_cycles, 0x7b);

    cycle = send(keyboard, 0x12, cycle + inquiry_cycles + reply_cycles);
    require(!keyboard.advance(cycle + inquiry_cycles + reply_cycles),
            "unsupported command produced a reply");
}

void test_transitions()
{
    MacKeyboard keyboard;
    const auto sent = send(keyboard, 0x10, 0);
    const auto pressed = sent + 10'000;
    require(!keyboard.advance(pressed), "empty Inquiry produced an early response");
    require(keyboard.queue_key(0x24, true, pressed), "Return press rejected");
    require(keyboard.queue_key(0x24, false, pressed + 1), "Return release rejected");
    receive(keyboard, pressed, 0x49);

    const auto next = send(keyboard, 0x14, pressed + reply_cycles);
    receive(keyboard, next, 0xc9);
    const auto empty = send(keyboard, 0x14, next + reply_cycles);
    receive(keyboard, empty, 0x7b);

    const auto expiry = send(keyboard, 0x10, empty + reply_cycles) + inquiry_cycles;
    require(keyboard.queue_key(0x00, true, expiry), "A press at Inquiry expiry rejected");
    receive(keyboard, expiry, 0x7b);
    const auto queued = send(keyboard, 0x10, expiry + reply_cycles);
    receive(keyboard, queued, 0x01);
}

void test_shift_mode_changes()
{
    MacKeyboard keyboard;
    keyboard.write_shift_register(0x14, 0);
    require(keyboard.queue_key(0x39, true, 1), "Caps Lock press rejected");
    require(!keyboard.advance(command_cycles), "disabled shift register transmitted");

    const auto enabled = command_cycles + 1;
    keyboard.set_auxiliary_control(shift_out | 0x40, enabled);
    require(!keyboard.advance(enabled + command_cycles - 1), "preloaded command finished early");
    require(keyboard.advance(enabled + command_cycles), "preloaded command failed to shift");

    const auto receive_mode = enabled + command_cycles;
    keyboard.set_auxiliary_control(shift_in | 0x40, receive_mode);
    keyboard.set_auxiliary_control(0, receive_mode + 1);
    require(!keyboard.advance(receive_mode + reply_cycles),
            "disabled shift register completed an interrupted reply");

    const auto retry = receive_mode + reply_cycles + 1;
    keyboard.set_auxiliary_control(shift_in, retry);
    receive(keyboard, retry, 0x73);
}

void test_queue_bounds()
{
    MacKeyboard keyboard;
    require(!keyboard.queue_key(0x3a, true, 0), "out-of-range scan code accepted");
    for (std::size_t index = 0; index < MacKeyboard::queue_capacity; ++index) {
        require(keyboard.queue_key(0, index % 2 == 0, 0), "queue filled early");
    }
    require(!keyboard.queue_key(0x24, true, 0), "queue accepted a transition after becoming full");

    uint64_t cycle = 0;
    for (std::size_t index = 0; index < MacKeyboard::queue_capacity; ++index) {
        const auto sent = send(keyboard, 0x14, cycle);
        receive(keyboard, sent, index % 2 == 0 ? 0x01 : 0x81);
        cycle = sent + reply_cycles;
        if (index == 0) {
            require(keyboard.queue_key(0x24, true, cycle), "queue slot was not reusable");
        }
    }
    const auto sent = send(keyboard, 0x14, cycle);
    receive(keyboard, sent, 0x49);
}

void test_fault_injection()
{
    for(uint8_t kind=1;kind<=5;++kind) {
        MacKeyboard k;
        uint32_t arg=kind==3?5000:kind==4?0:kind==5?1000:0;
        require(k.configure_fault(0x14,1,kind,arg),"fault configuration failed");
        require(k.queue_key(0,true,0),"fault fixture queue failed");
        k.set_auxiliary_control(shift_out,0);k.write_shift_register(0x14,0);
        require(k.fault_triggered() && k.fault_cycle()==0,"fault did not activate");
        if(kind==1 || kind==5) {
            require(!k.advance(command_cycles+10000),"stalled command completed");
            k.set_auxiliary_control(0,command_cycles+10001);
            auto next=send(k,0x14,command_cycles+10002);receive(k,next,0x01);
        } else {
            require(k.advance(command_cycles),"fault fixture command did not finish");
            k.set_auxiliary_control(shift_in,command_cycles);
            if(kind==2) {
                require(!k.advance(command_cycles+inquiry_cycles+reply_cycles),"suppressed reply completed");
                k.set_auxiliary_control(0,command_cycles+inquiry_cycles+reply_cycles+1);
                auto next=send(k,0x14,command_cycles+inquiry_cycles+reply_cycles+2);receive(k,next,0x01);
            } else if(kind==3) {
                require(!k.advance(command_cycles+reply_cycles+arg-1),"delayed reply was early");
                require(k.advance(command_cycles+reply_cycles+arg),"delayed reply failed");
                require(k.shift_register()==0x01,"delayed reply changed data");
            } else {
                receive(k,command_cycles,0x00);
                auto next=send(k,0x14,command_cycles+reply_cycles);receive(k,next,0x01);
            }
        }
    }
    MacKeyboard occurrence;
    require(occurrence.configure_fault(0x14,2,4,0xff),"occurrence configuration failed");
    auto first=send(occurrence,0x14,0);receive(occurrence,first,0x7b);
    require(!occurrence.fault_triggered(),"fault triggered too soon");
    auto second=send(occurrence,0x14,first+reply_cycles);receive(occurrence,second,0xff);
    require(occurrence.fault_triggered(),"second occurrence not selected");
}

} // namespace

int main()
{
    test_commands();
    test_transitions();
    test_shift_mode_changes();
    test_queue_bounds();
    test_fault_injection();
}
