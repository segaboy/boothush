#ifndef BOOTHUSH_IWM_H
#define BOOTHUSH_IWM_H

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace boothush {

class Iwm {
public:
    struct State {
        bool media_present, write_protected, external_selected, controller_enabled;
        bool motor_on, disk_changed, step_outward;
        uint8_t track, control, mode;
        uint64_t step_commands, nibbles_delivered, disabled_data_reads, not_ready_data_reads;
        uint64_t ignored_data_writes, eject_commands;
    };
    struct ChecksumFault {
        bool enabled = false;
        uint8_t track = 0, sector = 0;
        uint64_t altered_nibbles = 0;
    };

    bool insert(std::span<const uint8_t> image);
    uint8_t read(unsigned reg, bool select, uint64_t cycle);
    void write(unsigned reg, bool select, uint64_t cycle, uint8_t value);
    // Copies modeled state only; never touches soft switches or the data latch.
    State state(uint64_t cycle) const;
    // Configures a persistent synthetic checksum error on inserted media.
    bool inject_bad_checksum(unsigned track, unsigned sector);
    ChecksumFault checksum_fault() const { return checksum_fault_; }

private:
    static constexpr unsigned track_count = 80;
    std::array<std::vector<uint8_t>, track_count> tracks_{};
    std::array<std::array<uint32_t, 12>, track_count> checksum_offsets_{};
    ChecksumFault checksum_fault_{};
    uint8_t bad_checksum_nibble_ = 0;
    uint8_t control_ = 0;
    uint8_t mode_ = 0;
    unsigned track_ = 0;
    bool inserted_ = false;
    bool disk_changed_ = false;
    bool outward_ = false;
    bool motor_on_ = false;
    uint64_t motor_start_ = 0;
    uint64_t disable_at_ = 0;
    uint64_t last_slot_ = UINT64_MAX;
    uint64_t step_commands_ = 0, nibbles_delivered_ = 0;
    uint64_t disabled_data_reads_ = 0, not_ready_data_reads_ = 0;
    uint64_t ignored_data_writes_ = 0, eject_commands_ = 0;

    bool enabled(uint64_t cycle) const;
    bool internal_drive(uint64_t cycle) const;
    void switch_line(unsigned reg, bool select, uint64_t cycle);
    void drive_command(unsigned command, uint64_t cycle);
    bool drive_sense(bool select, uint64_t cycle) const;
    uint8_t read_data(uint64_t cycle);
};

} // namespace boothush

#endif
