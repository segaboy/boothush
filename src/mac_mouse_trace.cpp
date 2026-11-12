#include "mac_mouse_trace.h"
#include "mac_mouse_api.h"

#include <charconv>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

struct mac_mouse_trace {
    uint32_t edge_cycles;
    uint8_t dcd_invert;
    uint32_t vbl_guard_cycles;
    uint64_t stop_cycle = 0;
    std::vector<mac_mouse_sample> samples;
    std::string serialized;
};

namespace {

constexpr std::string_view signature = "BHMOUSE1";

std::string_view take_line(std::string_view &rest)
{
    const auto end = rest.find('\n');
    auto line = rest.substr(0, end);
    rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return line;
}

template<class Number>
bool read_number(std::string_view &rest, Number &number, bool last = false)
{
    const auto split = rest.find(' ');
    if (last != (split == std::string_view::npos) || rest.empty()) {
        return false;
    }
    const auto token = rest.substr(0, split);
    const auto result = std::from_chars(token.data(), token.data() + token.size(), number);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
        return false;
    }
    rest.remove_prefix(last ? rest.size() : split + 1);
    return true;
}

template<class Number>
void append_number(std::string &text, Number value, char delimiter)
{
    char field[32];
    const auto result = std::to_chars(field, field + sizeof(field), value);
    text.append(field, result.ptr);
    text.push_back(delimiter);
}

} // namespace

mac_mouse_trace *mac_mouse_trace_create(uint32_t edge_cycles, uint8_t dcd_invert,
                                       uint32_t vbl_guard_cycles)
{
    if (edge_cycles == 0 || edge_cycles > 1000000 || dcd_invert > 3 ||
        vbl_guard_cycles > MAC_MOUSE_MAX_VBL_GUARD_CYCLES) {
        return nullptr;
    }
    return new (std::nothrow) mac_mouse_trace{edge_cycles, dcd_invert, vbl_guard_cycles, 0, {}, {}};
}

mac_mouse_trace *mac_mouse_trace_parse(const uint8_t *bytes, size_t size)
{
    if (bytes == nullptr || size == 0 || size > MAC_MOUSE_TRACE_MAX_BYTES) {
        return nullptr;
    }
    auto rest = std::string_view(reinterpret_cast<const char *>(bytes), size);
    auto header = take_line(rest);
    if (!header.starts_with(signature) || header.size() <= signature.size() ||
        header[signature.size()] != ' ') {
        return nullptr;
    }
    header.remove_prefix(signature.size() + 1);

    uint32_t edge_cycles, invert, guard;
    uint64_t stop_cycle;
    if (!read_number(header, edge_cycles) || !read_number(header, invert) ||
        !read_number(header, stop_cycle) || !read_number(header, guard, true) || invert > 3) {
        return nullptr;
    }
    std::unique_ptr<mac_mouse_trace> trace(mac_mouse_trace_create(edge_cycles, uint8_t(invert), guard));
    if (!trace) {
        return nullptr;
    }
    trace->stop_cycle = stop_cycle;

    while (!rest.empty()) {
        auto line = take_line(rest);
        mac_mouse_sample sample{};
        unsigned button;
        if (!read_number(line, sample.cycle) || !read_number(line, sample.dx) ||
            !read_number(line, sample.dy) || !read_number(line, button, true) ||
            button > 2 || sample.cycle > stop_cycle) {
            return nullptr;
        }
        sample.button_changed = button != 0;
        sample.pressed = button == 2;
        if (!mac_mouse_trace_append(trace.get(), sample)) {
            return nullptr;
        }
    }
    return trace.release();
}

void mac_mouse_trace_destroy(mac_mouse_trace *trace)
{
    delete trace;
}

uint32_t mac_mouse_trace_edge_cycles(const mac_mouse_trace *trace)
{
    return trace->edge_cycles;
}

uint8_t mac_mouse_trace_dcd_invert(const mac_mouse_trace *trace)
{
    return trace->dcd_invert;
}

uint32_t mac_mouse_trace_vbl_guard_cycles(const mac_mouse_trace *trace)
{
    return trace->vbl_guard_cycles;
}

uint64_t mac_mouse_trace_stop_cycle(const mac_mouse_trace *trace)
{
    return trace->stop_cycle;
}

size_t mac_mouse_trace_count(const mac_mouse_trace *trace)
{
    return trace->samples.size();
}

const mac_mouse_sample *mac_mouse_trace_samples(const mac_mouse_trace *trace)
{
    return trace->samples.data();
}

bool mac_mouse_trace_append(mac_mouse_trace *trace, mac_mouse_sample sample)
{
    if (trace->samples.size() >= MAC_MOUSE_TRACE_MAX_SAMPLES ||
        (!trace->samples.empty() && sample.cycle < trace->samples.back().cycle)) {
        return false;
    }
    try {
        trace->samples.push_back(sample);
        return true;
    } catch (const std::bad_alloc &) {
        return false;
    }
}

void mac_mouse_trace_pop(mac_mouse_trace *trace)
{
    trace->samples.pop_back();
}

const uint8_t *mac_mouse_trace_serialize(mac_mouse_trace *trace, uint64_t stop_cycle, size_t *size)
{
    if (!trace->samples.empty() && stop_cycle < trace->samples.back().cycle) {
        return nullptr;
    }
    try {
        auto &text = trace->serialized;
        text.assign(signature);
        text.push_back(' ');
        append_number(text, trace->edge_cycles, ' ');
        append_number(text, trace->dcd_invert, ' ');
        append_number(text, stop_cycle, ' ');
        append_number(text, trace->vbl_guard_cycles, '\n');
        for (const auto &sample : trace->samples) {
            append_number(text, sample.cycle, ' ');
            append_number(text, sample.dx, ' ');
            append_number(text, sample.dy, ' ');
            append_number(text, sample.button_changed ? (sample.pressed ? 2 : 1) : 0, '\n');
        }
        *size = text.size();
        return reinterpret_cast<const uint8_t *>(text.data());
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}
