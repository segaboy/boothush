// GCR encoding and Disk Copy geometry adapted from the BSD-3-Clause MAME code.
// See docs/mac-floppy.md for attribution and the pinned upstream revision.

#include "iwm.h"

#include "mac128k.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace boothush {
namespace {

constexpr unsigned sector_bytes = 512;
constexpr unsigned tag_bytes = 12;
constexpr unsigned tag_image_bytes = MAC128K_FLOPPY_SIZE / sector_bytes * tag_bytes;
constexpr unsigned dc42_header_bytes = 84;
constexpr unsigned tracks_per_zone = 16;
constexpr uint64_t cpu_hz = 7833600;
constexpr uint64_t cycles_per_nibble = 124;
constexpr uint64_t disable_delay = 8388608;
constexpr std::array<unsigned, 5> rpm{394, 429, 472, 525, 590};

constexpr uint8_t drive_enable = 0x10;
constexpr uint8_t external_drive = 0x20;
constexpr uint8_t drive_strobe = 0x08;
constexpr uint8_t q6 = 0x40;
constexpr uint8_t q7 = 0x80;
constexpr uint8_t immediate_disable = 0x04;

constexpr std::array<uint8_t, 64> gcr{
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

struct Payload {
    std::span<const uint8_t> sectors;
    std::span<const uint8_t> tags;
};

uint32_t read_be32(std::span<const uint8_t> data, size_t offset)
{
    uint32_t result = 0;
    for (size_t i = 0; i < 4; ++i) {
        result = (result << 8) | data[offset + i];
    }
    return result;
}

uint32_t dc42_checksum(std::span<const uint8_t> data)
{
    uint32_t checksum = 0;
    for (size_t i = 0; i < data.size(); i += 2) {
        checksum += (uint32_t{data[i]} << 8) | data[i + 1];
        checksum = std::rotr(checksum, 1);
    }
    return checksum;
}

Payload image_payload(std::span<const uint8_t> image)
{
    if (image.size() == MAC128K_FLOPPY_SIZE) {
        return {image, {}};
    }
    if (image.size() < dc42_header_bytes || image[0] > 63 ||
        read_be32(image, 0x40) != MAC128K_FLOPPY_SIZE ||
        image[0x50] != 0 || image[0x51] != 2 || image[0x52] != 1 || image[0x53] != 0) {
        return {};
    }
    const uint32_t tag_size = read_be32(image, 0x44);
    if ((tag_size != 0 && tag_size != tag_image_bytes) ||
        image.size() != dc42_header_bytes + MAC128K_FLOPPY_SIZE + tag_size) {
        return {};
    }
    const auto sectors = image.subspan(dc42_header_bytes, MAC128K_FLOPPY_SIZE);
    const auto tags = image.last(tag_size);
    const uint32_t tag_checksum = tags.empty() ? 0 : dc42_checksum(tags.subspan(tag_bytes));
    if (read_be32(image, 0x48) != dc42_checksum(sectors) || read_be32(image, 0x4c) != tag_checksum) {
        return {};
    }
    return {sectors, tags};
}

void append_gcr(std::vector<uint8_t> &track, uint8_t a, uint8_t b, uint8_t c, bool last = false)
{
    track.push_back(gcr[((a >> 2) & 0x30) | ((b >> 4) & 0x0c) | (c >> 6)]);
    track.push_back(gcr[a & 0x3f]);
    track.push_back(gcr[b & 0x3f]);
    if (!last) {
        track.push_back(gcr[c & 0x3f]);
    }
}

uint32_t append_sector(std::vector<uint8_t> &track, unsigned cylinder, unsigned sector,
                       std::span<const uint8_t> data, std::span<const uint8_t> tags)
{
    const unsigned track_low = cylinder & 63;
    const unsigned track_high = cylinder >> 6;
    constexpr unsigned format = 2;
    track.insert(track.end(), 36, 0xff);
    track.insert(track.end(), {0xd5, 0xaa, 0x96, gcr[track_low], gcr[sector],
                               gcr[track_high], gcr[format],
                               gcr[track_low ^ sector ^ track_high ^ format], 0xde, 0xaa, 0xff});
    track.insert(track.end(), 5, 0xff);
    track.insert(track.end(), {0xd5, 0xaa, 0xad, gcr[sector]});

    std::array<uint8_t, tag_bytes + sector_bytes> payload{};
    if (!tags.empty()) {
        std::copy(tags.begin(), tags.end(), payload.begin());
    }
    std::copy(data.begin(), data.end(), payload.begin() + tag_bytes);
    uint8_t checksum_a = 0;
    uint8_t checksum_b = 0;
    uint8_t checksum_c = 0;
    for (size_t offset = 0; offset < payload.size(); offset += 3) {
        const bool last = offset + 2 == payload.size();
        const uint8_t a = payload[offset];
        const uint8_t b = payload[offset + 1];
        const uint8_t c = last ? 0 : payload[offset + 2];

        const uint8_t rotated_c = std::rotl(checksum_c, 1);
        const unsigned sum_a = unsigned{checksum_a} + a + (rotated_c & 1);
        checksum_a = static_cast<uint8_t>(sum_a);
        const unsigned sum_b = unsigned{checksum_b} + b + (sum_a >> 8);
        checksum_b = static_cast<uint8_t>(sum_b);
        checksum_c = last ? rotated_c : static_cast<uint8_t>(rotated_c + c + (sum_b >> 8));
        append_gcr(track, a ^ rotated_c, b ^ checksum_a, c ^ checksum_b, last);
    }
    // The second checksum nibble contains checksum A's low six bits.
    const auto checksum_offset = static_cast<uint32_t>(track.size() + 1);
    append_gcr(track, checksum_a, checksum_b, checksum_c);
    track.insert(track.end(), {0xde, 0xaa, 0xff, 0xff});
    return checksum_offset;
}

std::vector<uint8_t> encode_track(const Payload &payload, unsigned cylinder, unsigned first_sector,
                                  std::array<uint32_t, 12> &checksum_offsets)
{
    const unsigned zone = cylinder / tracks_per_zone;
    const unsigned count = 12 - zone;
    const size_t slots = cpu_hz * 60 / (rpm[zone] * cycles_per_nibble);
    std::vector<uint8_t> encoded;
    encoded.reserve(slots);
    std::array<unsigned, 12> physical_order{};
    for (unsigned sector = 0, slot = 0; sector < count; ++sector) {
        physical_order[slot] = sector;
        slot = (slot + 2) % count;
        if (slot == 0) {
            ++slot;
        }
    }
    for (unsigned slot = 0; slot < count; ++slot) {
        const unsigned sector = physical_order[slot];
        const unsigned logical = first_sector + sector;
        const auto data = payload.sectors.subspan(logical * sector_bytes, sector_bytes);
        const auto tags = payload.tags.empty() ? payload.tags :
            payload.tags.subspan(logical * tag_bytes, tag_bytes);
        checksum_offsets[sector] = append_sector(encoded, cylinder, sector, data, tags);
    }
    if (encoded.size() < slots) {
        encoded.resize(slots, 0xff);
    }
    return encoded;
}

} // namespace

bool Iwm::insert(std::span<const uint8_t> image)
{
    const Payload payload = image_payload(image);
    if (payload.sectors.empty()) {
        return false;
    }
    decltype(tracks_) tracks;
    decltype(checksum_offsets_) checksums{};
    for (unsigned cylinder = 0, sector = 0; cylinder < track_count; ++cylinder) {
        tracks[cylinder] = encode_track(payload, cylinder, sector, checksums[cylinder]);
        sector += 12 - cylinder / tracks_per_zone;
    }
    std::vector<uint8_t> original(image.begin(), image.end());
    tracks_ = std::move(tracks);
    image_ = std::move(original);
    data_start_ = image.size() == MAC128K_FLOPPY_SIZE ? 0 : dc42_header_bytes;
    tag_start_ = data_start_ + MAC128K_FLOPPY_SIZE;
    tagged_ = image.size() > tag_start_;
    writable_ = write_error_ = false;
    write_used_ = 0;
    session_bytes_ = session_records_ = 0;
    accepted_bytes_ = committed_sectors_ = rejected_records_ = 0;
    checksum_offsets_ = checksums;
    checksum_fault_ = {};
    inserted_ = true;
    disk_changed_ = true;
    last_slot_ = UINT64_MAX;
    return true;
}

bool Iwm::enable_writes()
{
    if (!inserted_) return false;
    writable_ = true;
    return true;
}

Iwm::WriteState Iwm::write_state() const
{
    return {writable_, write_error_ || rejected_records_ != 0, (control_ & q7) != 0,
            static_cast<uint32_t>(write_used_), accepted_bytes_, committed_sectors_, rejected_records_};
}

std::span<const uint8_t> Iwm::export_image()
{
    if (!writable_ || image_.empty() || rejected_records_ || write_used_ ||
        ((control_ & q7) && session_bytes_)) return {};
    if (data_start_) {
        const auto bytes = std::span<const uint8_t>(image_);
        const uint32_t data_sum = dc42_checksum(bytes.subspan(data_start_, MAC128K_FLOPPY_SIZE));
        const uint32_t tag_sum = tagged_ ? dc42_checksum(bytes.subspan(tag_start_ + tag_bytes)) : 0;
        for (unsigned i = 0; i < 4; ++i) {
            image_[0x48 + i] = static_cast<uint8_t>(data_sum >> (24 - 8 * i));
            image_[0x4c + i] = static_cast<uint8_t>(tag_sum >> (24 - 8 * i));
        }
    }
    return image_;
}

bool Iwm::write_ready(uint64_t cycle) const
{
    return writable_ && !write_error_ && inserted_ && motor_on_ && internal_drive(cycle) &&
           track_ == write_track_ && cycle >= next_write_cycle_;
}

void Iwm::reject_write()
{
    if (!write_error_) ++rejected_records_;
    write_error_ = true;
}

void Iwm::finish_write()
{
    if (writable_ && (write_used_ || (session_bytes_ && !session_records_))) reject_write();
    write_used_ = 0;
}

bool Iwm::commit_record()
{
    // Decode one complete record into a fixed private buffer before touching media.
    // This is the inverse of the existing data-record encoding, not a flux writer.
    auto symbol = [](uint8_t byte) -> int {
        const auto found = std::find(gcr.begin(), gcr.end(), byte);
        return found == gcr.end() ? -1 : static_cast<int>(found - gcr.begin());
    };
    const int sector = symbol(write_record_[3]);
    if (sector < 0 || static_cast<unsigned>(sector) >= 12 - track_ / tracks_per_zone ||
        write_record_[707] != 0xde || write_record_[708] != 0xaa) return false;
    std::array<uint8_t, tag_bytes + sector_bytes> payload{};
    size_t at = 4;
    auto unpack = [&](uint8_t &a, uint8_t &b, uint8_t &c, bool last) {
        const int high = symbol(write_record_[at++]);
        const int low_a = symbol(write_record_[at++]);
        const int low_b = symbol(write_record_[at++]);
        const int low_c = last ? 0 : symbol(write_record_[at++]);
        if (high < 0 || low_a < 0 || low_b < 0 || low_c < 0) return false;
        a = static_cast<uint8_t>(((high & 0x30) << 2) | low_a);
        b = static_cast<uint8_t>(((high & 0x0c) << 4) | low_b);
        c = static_cast<uint8_t>(((high & 3) << 6) | low_c);
        return true;
    };
    uint8_t check_a = 0, check_b = 0, check_c = 0;
    for (size_t offset = 0; offset < payload.size(); offset += 3) {
        const bool last = offset + 2 == payload.size();
        uint8_t a, b, c;
        if (!unpack(a, b, c, last)) return false;
        const uint8_t rotated = std::rotl(check_c, 1);
        a ^= rotated;
        const unsigned sum_a = unsigned{check_a} + a + (rotated & 1);
        check_a = static_cast<uint8_t>(sum_a);
        b ^= check_a;
        const unsigned sum_b = unsigned{check_b} + b + (sum_a >> 8);
        check_b = static_cast<uint8_t>(sum_b);
        payload[offset] = a;
        payload[offset + 1] = b;
        if (last) {
            if (c != (check_b & 0xc0)) return false;
            check_c = rotated;
        } else {
            c ^= check_b;
            check_c = static_cast<uint8_t>(rotated + c + (sum_b >> 8));
            payload[offset + 2] = c;
        }
    }
    uint8_t a, b, c;
    if (!unpack(a, b, c, false) || a != check_a || b != check_b || c != check_c) return false;
    // An untagged source cannot represent nonzero tags in a same-format snapshot.
    if (!tagged_ && std::any_of(payload.begin(), payload.begin() + tag_bytes,
                              [](uint8_t byte) { return byte != 0; })) return false;
    unsigned logical = static_cast<unsigned>(sector);
    for (unsigned track = 0; track < track_; ++track) logical += 12 - track / tracks_per_zone;
    std::copy(payload.begin() + tag_bytes, payload.end(), image_.begin() + data_start_ + logical * sector_bytes);
    if (tagged_) std::copy_n(payload.begin(), tag_bytes, image_.begin() + tag_start_ + logical * tag_bytes);
    const auto checksum = checksum_offsets_[track_][sector];
    std::copy(write_record_.begin(), write_record_.end(), tracks_[track_].begin() + checksum - 704);
    if (checksum_fault_.enabled && checksum_fault_.track == track_ && checksum_fault_.sector == sector)
        bad_checksum_nibble_ = gcr[static_cast<unsigned>(symbol(write_record_[704])) ^ 1];
    ++committed_sectors_;
    ++session_records_;
    return true;
}

void Iwm::write_data(uint64_t cycle, uint8_t value)
{
    if (!write_ready(cycle)) {
        ++ignored_data_writes_;
        reject_write();
        return;
    }
    ++accepted_bytes_;
    ++session_bytes_;
    next_write_cycle_ = cycle + cycles_per_nibble;
    if (write_used_ < 3) {
        constexpr std::array<uint8_t, 3> prologue{0xd5, 0xaa, 0xad};
        if (value == prologue[write_used_]) write_record_[write_used_++] = value;
        else { write_used_ = value == prologue[0] ? 1 : 0; write_record_[0] = value; }
        return;
    }
    write_record_[write_used_++] = value;
    if (write_used_ == write_record_.size()) {
        if (!commit_record()) reject_write();
        write_used_ = 0;
    }
}

bool Iwm::inject_bad_checksum(unsigned track, unsigned sector, bool once)
{
    if (!inserted_ || track >= track_count || sector >= 12 - track / tracks_per_zone) {
        return false;
    }
    const uint8_t original = tracks_[track][checksum_offsets_[track][sector]];
    const auto code = std::find(gcr.begin(), gcr.end(), original);
    if (code == gcr.end()) return false;
    bad_checksum_nibble_ = gcr[static_cast<size_t>(code - gcr.begin()) ^ 1];
    checksum_fault_ = {true, static_cast<uint8_t>(track), static_cast<uint8_t>(sector), 0, once};
    return true;
}

bool Iwm::enabled(uint64_t cycle) const
{
    return (control_ & drive_enable) != 0 || cycle < disable_at_;
}

Iwm::State Iwm::state(uint64_t cycle) const
{
    return {inserted_, inserted_ && !writable_, (control_ & external_drive) != 0, enabled(cycle),
            motor_on_, disk_changed_, outward_, static_cast<uint8_t>(track_), control_, mode_,
            step_commands_, nibbles_delivered_, disabled_data_reads_, not_ready_data_reads_,
            ignored_data_writes_, eject_commands_};
}

bool Iwm::internal_drive(uint64_t cycle) const
{
    return enabled(cycle) && (control_ & external_drive) == 0;
}

void Iwm::switch_line(unsigned reg, bool select, uint64_t cycle)
{
    const uint8_t mask = static_cast<uint8_t>(1u << (reg / 2));
    const uint8_t previous = control_;
    control_ = (reg & 1) != 0 ? control_ | mask : control_ & ~mask;

    if (writable_ && !(previous & q7) && (control_ & q7)) {
        write_used_ = 0;
        write_track_ = track_;
        write_error_ = false;
        session_bytes_ = session_records_ = 0;
        next_write_cycle_ = cycle;
    } else if ((previous & q7) && !(control_ & q7)) {
        finish_write();
    }

    if ((control_ & drive_enable) != 0) {
        disable_at_ = 0;
    } else if ((previous & drive_enable) != 0 && (mode_ & immediate_disable) == 0) {
        disable_at_ = cycle + disable_delay;
    }
    if ((control_ & drive_strobe) != 0 && (previous & drive_strobe) == 0 && internal_drive(cycle)) {
        drive_command((control_ & 7) | (select ? 8 : 0), cycle);
    }
}

void Iwm::drive_command(unsigned command, uint64_t cycle)
{
    switch (command) {
    case 0: outward_ = false; break;
    case 1:
        ++step_commands_;
        if (outward_ && track_ != 0) {
            --track_;
        } else if (!outward_ && track_ + 1 < track_count) {
            ++track_;
        }
        last_slot_ = UINT64_MAX;
        break;
    case 2:
        if (!motor_on_) {
            motor_on_ = true;
            motor_start_ = cycle;
            last_slot_ = UINT64_MAX;
        }
        break;
    case 4: outward_ = true; break;
    case 6: motor_on_ = false; break;
    case 7:
        ++eject_commands_;
        inserted_ = false;
        disk_changed_ = true;
        motor_on_ = false;
        break;
    case 12: disk_changed_ = false; break;
    default: break;
    }
}

bool Iwm::drive_sense(bool select, uint64_t cycle) const
{
    if (!internal_drive(cycle)) {
        return true;
    }
    switch ((control_ & 7) | (select ? 8 : 0)) {
    case 0: return outward_;
    case 1: return true; // Seek completes at the strobe boundary.
    case 2: return !motor_on_;
    case 3: return disk_changed_;
    case 8: return !inserted_;
    case 9: return !inserted_ || writable_; // High means writable, or no disk.
    case 10: return track_ != 0;
    case 11:
        return motor_on_ && inserted_ &&
            ((cycle - motor_start_) / (cpu_hz * 60 / rpm[track_ / tracks_per_zone] / 120) & 1) == 0;
    case 14: return !motor_on_ || !inserted_;
    default: return false; // Single-sided GCR drive; no external drive or MFM support.
    }
}

uint8_t Iwm::read_data(uint64_t cycle)
{
    if (!enabled(cycle)) {
        ++disabled_data_reads_;
        return 0xff;
    }
    if (!internal_drive(cycle) || !motor_on_ || !inserted_) {
        ++not_ready_data_reads_;
        return 0;
    }
    const uint64_t slot = (cycle - motor_start_) / cycles_per_nibble;
    if (slot == last_slot_) {
        ++not_ready_data_reads_;
        return 0;
    }
    last_slot_ = slot;
    const auto &track = tracks_[track_];
    ++nibbles_delivered_;
    const size_t offset = slot % track.size();
    if (checksum_fault_.enabled && (!checksum_fault_.once || checksum_fault_.altered_nibbles == 0) &&
        track_ == checksum_fault_.track &&
        offset == checksum_offsets_[track_][checksum_fault_.sector]) {
        ++checksum_fault_.altered_nibbles;
        return bad_checksum_nibble_;
    }
    return track[offset];
}

uint8_t Iwm::read(unsigned reg, bool select, uint64_t cycle)
{
    switch_line(reg, select, cycle);
    switch (control_ & (q6 | q7)) {
    case 0: return read_data(cycle);
    case q6:
        return (mode_ & 0x1f) | (enabled(cycle) ? 0x20 : 0) | (drive_sense(select, cycle) ? 0x80 : 0);
    case q7:
        if (!writable_) return 0xbf;
        return static_cast<uint8_t>(0x3f | (write_error_ ? 0 : 0x40) | (write_ready(cycle) ? 0x80 : 0));
    default: return 0xff;
    }
}

void Iwm::write(unsigned reg, bool select, uint64_t cycle, uint8_t value)
{
    switch_line(reg, select, cycle);
    if ((control_ & (q6 | q7)) == (q6 | q7) && (reg & 1) != 0) {
        if (!enabled(cycle)) {
            mode_ = value;
        } else if (writable_) {
            write_data(cycle, value);
        } else {
            ++ignored_data_writes_;
        }
    }
    // Default insertion remains protected; only explicitly enabled copies accept records.
}

} // namespace boothush
