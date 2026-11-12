#ifndef BOOTHUSH_PCAT_FLOPPY_H
#define BOOTHUSH_PCAT_FLOPPY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

class PcatFloppy {
public:
    static constexpr std::size_t image_size = 80 * 2 * 15 * 512;

    // The board owns the two spans for the life of the attached controller.
    explicit PcatFloppy(std::span<std::uint8_t> ram);
    void set_media(std::span<const std::uint8_t> media);

    bool handles_port(std::uint16_t port) const;
    std::uint8_t read_port(std::uint16_t port);
    void write_port(std::uint16_t port, std::uint8_t value);
    bool take_irq();

private:
    struct DmaChannel {
        std::uint16_t address = 0;
        std::uint16_t count = 0;
        std::uint16_t base_address = 0;
        std::uint16_t base_count = 0;
        std::uint8_t mode = 0;
    };

    struct Dma8237 {
        std::array<DmaChannel, 4> channels{};
        std::uint8_t command = 0;
        std::uint8_t mask = 0x0f;
        std::uint8_t terminal_count = 0;
        std::uint8_t request = 0;
        bool high_byte = false;

        std::uint8_t read(std::uint8_t reg);
        void write(std::uint8_t reg, std::uint8_t value);
        std::size_t write_device_data(unsigned channel_index, std::uint8_t page,
                                      std::span<const std::uint8_t> data,
                                      std::span<std::uint8_t> ram, bool &reached_terminal_count);
    };

    void write_dor(std::uint8_t value);
    void reset_controller();
    void write_fifo(std::uint8_t value);
    std::uint8_t read_fifo();
    std::uint8_t main_status() const;
    void execute_command();
    void complete_seek(std::uint8_t unit, std::uint8_t cylinder);
    void sense_interrupt();
    void read_data();
    void read_id();
    void data_result(std::uint8_t st0, std::uint8_t st1, std::uint8_t st2,
                     std::uint8_t cylinder, std::uint8_t head, std::uint8_t sector, std::uint8_t size);
    void result(std::initializer_list<std::uint8_t> values);
    bool motor_enabled(std::uint8_t unit) const;
    void signal_irq();

    std::span<std::uint8_t> ram_;
    std::span<const std::uint8_t> media_;
    Dma8237 dma1_{};
    Dma8237 dma2_{};
    std::array<std::uint8_t, 16> dma_pages_{};

    std::uint8_t dor_ = 0;
    std::uint8_t data_rate_ = 0;
    bool disk_changed_ = false;
    bool non_dma_ = false;
    bool irq_pending_ = false;
    std::array<std::uint8_t, 4> cylinders_{};
    std::array<std::uint8_t, 4> seek_status_{};
    std::uint8_t seek_pending_ = 0;
    std::array<std::uint8_t, 9> command_{};
    std::size_t command_size_ = 0;
    std::size_t command_length_ = 0;
    std::array<std::uint8_t, 7> result_{};
    std::size_t result_size_ = 0;
    std::size_t result_position_ = 0;
    std::size_t pio_position_ = 0;
    std::size_t pio_end_ = 0;
};

#endif
