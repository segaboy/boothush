#ifndef BOOTHUSH_IWM_H
#define BOOTHUSH_IWM_H

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace boothush {

class Iwm {
public:
    bool insert(std::span<const uint8_t> image);
    uint8_t read(unsigned reg, bool select, uint64_t cycle);
    void write(unsigned reg, bool select, uint64_t cycle, uint8_t value);

private:
    static constexpr unsigned track_count = 80;
    std::array<std::vector<uint8_t>, track_count> tracks_{};
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

    bool enabled(uint64_t cycle) const;
    bool internal_drive(uint64_t cycle) const;
    void switch_line(unsigned reg, bool select, uint64_t cycle);
    void drive_command(unsigned command, uint64_t cycle);
    bool drive_sense(bool select, uint64_t cycle) const;
    uint8_t read_data(uint64_t cycle);
};

} // namespace boothush

#endif
