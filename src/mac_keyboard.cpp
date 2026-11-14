#include "mac_keyboard.h"

namespace {

// SY6522 ACR bits 2-4 select the shift mode. The M0110 supplies CB1 clocks.
constexpr uint8_t shift_mask = 0x1c;
constexpr uint8_t shift_in = 0x0c;
constexpr uint8_t shift_out = 0x1c;

constexpr uint8_t inquiry = 0x10;
constexpr uint8_t instant = 0x14;
constexpr uint8_t model_number = 0x16;
constexpr uint8_t test = 0x36;
constexpr uint8_t no_transition = 0x7b;
constexpr uint8_t model_m0110 = 0x03;
constexpr uint8_t test_ack = 0x7d;

constexpr uint64_t cpu_hz = 7'833'600;

constexpr uint64_t microseconds_to_cycles(uint64_t microseconds)
{
    return (cpu_hz * microseconds + 999'999) / 1'000'000;
}

// Inside Macintosh III documents eight 400 us and eight 330 us clock
// periods in the two directions, and a quarter-second empty Inquiry.
constexpr uint64_t command_cycles = microseconds_to_cycles(8 * 400);
constexpr uint64_t reply_cycles = microseconds_to_cycles(8 * 330);
constexpr uint64_t inquiry_cycles = microseconds_to_cycles(250'000);

} // namespace

bool MacKeyboard::configure_fault(uint8_t command,uint32_t occurrence,uint8_t kind,uint32_t argument)
{
    if(!occurrence || kind<1 || kind>5 || ((kind==1 || kind==2) && argument) ||
       ((kind==3 || kind==5) && !argument) || (kind==4 && argument>255)) return false;
    fault_command_=command;fault_occurrence_=occurrence;fault_kind_=kind;fault_argument_=argument;
    fault_seen_=0;fault_triggered_=false;fault_selected_=false;stall_command_=false;disconnect_=false;reply_suppressed_=false;return true;
}

void MacKeyboard::set_auxiliary_control(uint8_t value, uint64_t cycle)
{
    record(cycle,mode,value);
    const uint8_t mode = value & shift_mask;
    if (mode == shift_mode_) {
        return;
    }
    shift_mode_ = mode;
    if (phase_ == Phase::sending && mode != shift_out) {
        phase_ = Phase::idle;
    } else if (phase_ == Phase::receiving && mode != shift_in) {
        phase_ = Phase::waiting;
    }

    if (mode == shift_out && preloaded_) {
        begin_send(cycle);
    } else if (mode == shift_in && phase_ == Phase::waiting) {
        prepare_reply(cycle);
    }
    preloaded_ = false;
}

void MacKeyboard::write_shift_register(uint8_t value, uint64_t cycle)
{
    record(cycle,shift_write,value);
    shift_register_ = value;
    preloaded_ = shift_mode_ == 0;
    if (shift_mode_ == shift_out) {
        begin_send(cycle);
    }
}

void MacKeyboard::begin_send(uint64_t cycle)
{
    command_ = shift_register_;
    fault_selected_=false;stall_command_=disconnect_;reply_suppressed_=false;
    if(fault_kind_ && !fault_triggered_ && command_==fault_command_ && ++fault_seen_==fault_occurrence_) {
        fault_triggered_=true;fault_selected_=true;fault_cycle_=cycle;record(cycle,fault_start,fault_kind_);
        if(fault_kind_==1) stall_command_=true;
        if(fault_kind_==5) {disconnect_=true;stall_command_=true;reconnect_cycle_=cycle+fault_argument_;record(cycle,disconnected,0);}
    }
    transfer_end_ = cycle + command_cycles;
    phase_ = Phase::sending;
}

void MacKeyboard::begin_reply(uint64_t cycle, uint8_t value, bool consumes_key)
{
    if(fault_selected_ && fault_kind_==4) {value=static_cast<uint8_t>(fault_argument_);consumes_key=false;}
    reply_ = value;
    consumes_key_ = consumes_key;
    transfer_end_ = cycle + reply_cycles + (fault_selected_ && fault_kind_==3?fault_argument_:0);
    phase_ = Phase::receiving;
    record(cycle,reply_begin,value);
}

void MacKeyboard::prepare_reply(uint64_t cycle)
{
    if(fault_selected_ && fault_kind_==2) {
        if(!reply_suppressed_) {record(cycle,reply_suppressed,command_);reply_suppressed_=true;}
        return;
    }
    switch (command_) {
    case inquiry:
        if (key_count_ == 0 && cycle < inquiry_expiry_) {
            return;
        }
        [[fallthrough]];
    case instant:
        begin_reply(cycle, key_count_ == 0 ? no_transition : keys_[head_], key_count_ != 0);
        break;
    case model_number:
        begin_reply(cycle, model_m0110);
        break;
    case test:
        begin_reply(cycle, test_ack);
        break;
    default:
        phase_ = Phase::idle;
        break;
    }
}

bool MacKeyboard::queue_key(uint8_t scan_code, bool pressed, uint64_t cycle)
{
    const auto transition = static_cast<uint8_t>((scan_code << 1) | 1 | (pressed ? 0 : 0x80));
    if (scan_code > max_scan_code || key_count_ == keys_.size()) {
        record(cycle,rejected,transition);
        return false;
    }
    keys_[(head_ + key_count_) % keys_.size()] = transition;
    ++key_count_;
    record(cycle,queued,transition);

    if (phase_ == Phase::waiting && !(fault_selected_ && fault_kind_==2) && command_ == inquiry &&
        shift_mode_ == shift_in && cycle < inquiry_expiry_) {
        begin_reply(cycle, keys_[head_], true);
    }
    return true;
}

bool MacKeyboard::advance(uint64_t cycle)
{
    if(disconnect_ && cycle>=reconnect_cycle_) {disconnect_=false;record(cycle,reconnected,0);}
    if (phase_ == Phase::sending && !stall_command_ && cycle >= transfer_end_) {
        inquiry_expiry_ = transfer_end_ + inquiry_cycles;
        phase_ = Phase::waiting;
        record(cycle,command_complete,command_);
        return true;
    }
    if (phase_ == Phase::waiting && !(fault_selected_ && fault_kind_==2) && command_ == inquiry &&
        shift_mode_ == shift_in && cycle >= inquiry_expiry_) {
        begin_reply(inquiry_expiry_, no_transition);
    }
    if (phase_ == Phase::receiving && cycle >= transfer_end_) {
        shift_register_ = reply_;
        record(cycle,reply_complete,reply_);
        if (consumes_key_) {
            head_ = (head_ + 1) % keys_.size();
            --key_count_;
            record(cycle,key_delivered,reply_);
        }
        phase_ = Phase::idle;
        return true;
    }
    return false;
}
