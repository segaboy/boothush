#ifndef BOOTHUSH_PCAT_HARD_DISK_H
#define BOOTHUSH_PCAT_HARD_DISK_H

#include <array>
#include <cstdint>
#include <span>

class PcatHardDisk {
public:
    static constexpr unsigned cylinders = 615;
    static constexpr unsigned heads = 4;
    static constexpr unsigned sectors_per_track = 17;
    static constexpr unsigned sector_size = 512;
    static constexpr unsigned sector_count = cylinders * heads * sectors_per_track;
    static constexpr unsigned image_size = sector_count * sector_size;

    void mount(std::span<uint8_t> image);
    bool handles_port(uint16_t port) const;
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    uint16_t read_data_word();
    void write_data_word(uint16_t value);
    bool take_irq();

private:
    enum class Transfer { idle, read, write };

    void reset();
    void command(uint8_t opcode);
    void fail(uint8_t error);
    void complete();
    void finish_sector();
    void raise_irq();
    bool selected() const;
    unsigned sector_index() const;
    uint8_t read_data_byte();
    void write_data_byte(uint8_t value);

    std::span<uint8_t> media_{};
    std::array<uint8_t, sector_size> buffer_{};
    Transfer transfer_ = Transfer::idle;
    unsigned offset_ = 0;
    unsigned remaining_ = 0;
    unsigned lba_ = 0;
    uint8_t error_ = 1;
    uint8_t count_ = 1;
    uint8_t sector_ = 1;
    uint16_t cylinder_ = 0;
    uint8_t drive_head_ = 0xa0;
    uint8_t control_ = 0;
    uint8_t status_ = 0;
    bool irq_ = false;
};

#endif
