#ifndef BOOTHUSH_PC_AT_DEVICES_H
#define BOOTHUSH_PC_AT_DEVICES_H

#include <array>
#include <cstdint>
#include <optional>

// PC/AT 5170 edge-triggered PIC pair, 8254 PIT and MC146818 RTC/CMOS.
// Time advances only when the 6 MHz guest CPU or an interrupt wait advances.
class PcAtDevices {
public:
    PcAtDevices();

    // Set the initial CMOS equipment and checksum for the attached media.
    void configure_drives(bool floppy, bool hard_disk);
    bool handles_port(std::uint16_t port) const;
    std::uint8_t read_port(std::uint16_t port);
    void write_port(std::uint16_t port, std::uint8_t value);
    void advance_cycles(std::uint32_t cycles);
    std::uint8_t rtc_register(std::uint8_t index) const;

    void pulse_irq(unsigned irq);
    bool interrupt_pending() const;
    // True when a clocked source can deliver an interrupt without guest writes.
    bool timed_interrupt_possible() const;
    std::uint8_t acknowledge_interrupt();

private:
    struct Pic {
        explicit Pic(std::uint8_t base) : vector_base(base) {}

        std::uint8_t vector_base;
        std::uint8_t irr = 0;
        std::uint8_t isr = 0;
        std::uint8_t mask = 0xff;
        unsigned lowest_priority = 7;
        enum class Init { ready, vector, cascade, mode } init = Init::ready;
        bool single = false;
        bool needs_icw4 = false;
        bool auto_eoi = false;
        bool auto_rotate = false;
        bool read_isr = false;
        bool poll = false;

        int highest(std::uint8_t bits) const;
        int pending(std::uint8_t external = 0) const;
        std::uint8_t read_command(std::uint8_t external = 0);
        void write_command(std::uint8_t value);
        void write_data(std::uint8_t value);
        void acknowledge(unsigned line);
    };

    struct Counter {
        std::uint32_t reload = 65536;
        std::uint32_t phase = 0;
        std::uint16_t partial = 0;
        std::optional<std::uint16_t> count_latch;
        std::optional<std::uint8_t> status_latch;
        std::uint8_t access = 3;
        std::uint8_t mode = 3;
        bool gate = true;
        bool loaded = false;
        bool triggered = false;
        bool bcd = false;
        bool write_high = false;
        bool read_high = false;
        bool latch_high = false;

        void program(std::uint8_t control);
        void load(std::uint16_t value);
        void write(std::uint8_t value);
        std::uint8_t read();
        void latch_count();
        void latch_status();
        void set_gate(bool enabled);
        bool output() const;
        bool will_rise() const;
        std::uint16_t count() const;
        std::uint32_t advance(std::uint32_t ticks);
    };

    Pic master_{0x08};
    Pic slave_{0x70};
    std::array<Counter, 3> pit_{};
    std::uint32_t pit_fraction_ = 0;
    std::uint8_t system_control_ = 0;
    bool refresh_ = false;

    std::array<std::uint8_t, 64> cmos_{};
    std::uint8_t cmos_address_ = 0;
    std::uint32_t rtc_fraction_ = 0;
    std::uint32_t rtc_phase_ = 0;
    bool rtc_irq_ = false;

    std::uint8_t cascade_request() const;
    void write_pit_control(std::uint8_t value);
    bool rtc_running() const;
    std::uint8_t read_cmos();
    void write_cmos(std::uint8_t value);
    void advance_rtc(std::uint32_t ticks);
    void tick_calendar();
    void update_rtc_irq();
};

#endif
