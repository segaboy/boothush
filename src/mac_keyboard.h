#ifndef BOOTHUSH_MAC_KEYBOARD_H
#define BOOTHUSH_MAC_KEYBOARD_H

#include <array>
#include <cstddef>
#include <cstdint>

// A byte-level M0110 and its connection to the VIA shift register.
class MacKeyboard {
public:
    static constexpr uint8_t max_scan_code = 0x39;
    static constexpr std::size_t queue_capacity = 64;
    enum TraceKind : uint8_t { queued, rejected, mode, shift_write, command_complete, reply_begin, reply_complete, key_delivered, shift_read,
                               fault_start=10, disconnected, reconnected, reply_suppressed };
    using Trace = void (*)(void *, uint64_t, TraceKind, uint8_t, std::size_t);
    void set_trace(Trace trace, void *context) { trace_=trace; context_=context; }
    std::size_t queue_size() const { return key_count_; }
    bool configure_fault(uint8_t command,uint32_t occurrence,uint8_t kind,uint32_t argument);
    bool fault_triggered() const { return fault_triggered_; }
    uint64_t fault_cycle() const { return fault_cycle_; }

    void set_auxiliary_control(uint8_t value, uint64_t cycle);
    void write_shift_register(uint8_t value, uint64_t cycle);
    uint8_t shift_register() const { return shift_register_; }
    uint8_t read_shift_register(uint64_t cycle) { record(cycle,shift_read,shift_register_); return shift_register_; }
    bool queue_key(uint8_t scan_code, bool pressed, uint64_t cycle);

    // A true result sets the VIA shift register's IFR bit. The caller clears
    // that bit on guest accesses to the shift register or writes to the IFR.
    bool advance(uint64_t cycle);

private:
    enum class Phase { idle, sending, waiting, receiving };

    void begin_send(uint64_t cycle);
    void begin_reply(uint64_t cycle, uint8_t value, bool consumes_key = false);
    void prepare_reply(uint64_t cycle);
    void record(uint64_t cycle,TraceKind kind,uint8_t value) { if(trace_) trace_(context_,cycle,kind,value,key_count_); }

    std::array<uint8_t, queue_capacity> keys_{};
    std::size_t head_ = 0;
    std::size_t key_count_ = 0;
    Phase phase_ = Phase::idle;
    uint64_t transfer_end_ = 0;
    uint64_t inquiry_expiry_ = 0;
    uint8_t shift_mode_ = 0;
    uint8_t shift_register_ = 0;
    uint8_t command_ = 0;
    uint8_t reply_ = 0;
    bool consumes_key_ = false;
    bool preloaded_ = false;
    Trace trace_ = nullptr;
    void *context_ = nullptr;
    uint8_t fault_command_=0,fault_kind_=0;
    uint32_t fault_occurrence_=0,fault_seen_=0,fault_argument_=0;
    bool fault_triggered_=false,fault_selected_=false,stall_command_=false,disconnect_=false,reply_suppressed_=false;
    uint64_t fault_cycle_=0,reconnect_cycle_=0;
};

#endif
