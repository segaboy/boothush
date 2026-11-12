#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#include <windows.h>
#include <bcrypt.h>

#include "mac128k.h"
#include "mac_live_window.h"
#include "mac_mouse_api.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t window_class[] = L"BootHushMac128KLiveWindow";
constexpr wchar_t fixture_variable[] = L"BOOTHUSH_CLASSICK_LIVE_ROM";
constexpr auto fixture_hash =
    "faf37c6f96f8eb2ee6cf7a1e58a8b1b9538055d2fc89493c7419eeea816f6c8c";
constexpr uint32_t diagnostic_address = 0xb00;
constexpr uint32_t candidate_vbl_guard_cycles = 4096;
constexpr uint32_t settle_cycles = 8'000'000;
constexpr uint64_t cycle_limit = 500'000'000;

using Rom = std::array<uint8_t, MAC128K_ROM_SIZE>;
using DiagnosticMemory = std::array<uint8_t, 1024>;

struct Point {
    int x, y;
    bool operator==(const Point &) const = default;
};

void require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string show_point(Point point)
{
    return "(" + std::to_string(point.x) + "," + std::to_string(point.y) + ")";
}

std::string sha256(std::span<const uint8_t> bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            "cannot open SHA-256 provider");
    std::array<uint8_t, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0,
                                  const_cast<PUCHAR>(bytes.data()),
                                  static_cast<ULONG>(bytes.size()),
                                  digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    require(status >= 0, "cannot hash test bytes");

    constexpr std::string_view hex = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (uint8_t value : digest) {
        result.push_back(hex[value >> 4]);
        result.push_back(hex[value & 0x0f]);
    }
    return result;
}

Rom make_via_probe()
{
    // Own 68000 ROM. Remove the overlay, mirror VIA PB to RAM and repeat.
    constexpr std::array<uint8_t, 8> vectors{0, 0, 0x40, 0, 0, 0x40, 1, 4};
    constexpr std::array<uint8_t, 28> program{
        0x13, 0xfc, 0, 0, 0, 0xef, 0xff, 0xfe,        // move.b #0,VIA ORA
        0x13, 0xfc, 0, 0x10, 0, 0xef, 0xe7, 0xfe,    // move.b #$10,VIA DDRA
        0x13, 0xf9, 0, 0xef, 0xe1, 0xfe, 0, 0, 0x0b, 0, // move.b VIA PB,$B00
        0x60, 0xf4,                                  // bra.s to preceding move
    };
    Rom image{};
    std::copy(vectors.begin(), vectors.end(), image.begin());
    std::copy(program.begin(), program.end(), image.begin() + 0x104);
    return image;
}

BOOL CALLBACK find_window(HWND hwnd, LPARAM result)
{
    std::array<wchar_t, 64> name{};
    if (GetClassNameW(hwnd, name.data(), static_cast<int>(name.size())) != 0 &&
        std::wstring_view(name.data()) == window_class) {
        *reinterpret_cast<HWND *>(result) = hwnd;
        return FALSE;
    }
    return TRUE;
}

class LiveMac {
public:
    explicit LiveMac(const Rom &rom, uint8_t dcd_invert, unsigned scale = 1,
                     uint32_t vbl_guard = MAC_MOUSE_DEFAULT_VBL_GUARD_CYCLES)
        : machine_(mac128k_create(rom.data()), mac128k_destroy),
          mouse_(mac_mouse_create_timed(MAC_MOUSE_DEFAULT_EDGE_CYCLES, dcd_invert, vbl_guard),
                 mac_mouse_destroy),
          window_(mac_live_window_create(scale, false), mac_live_window_destroy),
          scale_(static_cast<int>(scale))
    {
        require(machine_ != nullptr && mouse_ != nullptr, "cannot create Mac test devices");
        require(window_ != nullptr, "cannot create an invisible Mac test window");
        EnumThreadWindows(GetCurrentThreadId(), find_window, reinterpret_cast<LPARAM>(&hwnd_));
        require(hwnd_ != nullptr && !IsWindowVisible(hwnd_), "test window is missing or visible");
        mac_live_window_present(window_.get(), mac128k_frame(machine_.get()));
    }

    void run_to(uint64_t target)
    {
        require(target <= cycle_limit, "guest exceeded the test cycle budget");
        auto state = mac128k_get_state(machine_.get());
        while (state.cycles < target) {
            const auto slice = static_cast<uint32_t>(std::min<uint64_t>(target - state.cycles, 65536));
            mac128k_run(machine_.get(), slice);
            const auto next = mac128k_get_state(machine_.get());
            if (next.fault_operation) {
                throw std::runtime_error(
                    std::string(next.fault_operation) + " guest fault at " +
                    std::to_string(next.fault_address));
            }
            require(next.cycles > state.cycles, "guest made no progress");
            state = next;
        }
    }

    void post(UINT message, Point position, WPARAM keys = 0)
    {
        RECT client{};
        require(GetClientRect(hwnd_, &client) != 0, "cannot read the owned client viewport");
        const int left = (client.right - MAC128K_FRAME_WIDTH * scale_) / 2;
        const int top = (client.bottom - MAC128K_FRAME_HEIGHT * scale_) / 2;
        const LPARAM packed = MAKELPARAM(left + position.x * scale_, top + position.y * scale_);
        require(PostMessageW(hwnd_, message, keys, packed) != 0, "cannot post mouse message");
    }

    void post(UINT message)
    {
        require(PostMessageW(hwnd_, message, 0, 0) != 0, "cannot post owned-window message");
    }

    std::vector<mac_live_event> pump_events()
    {
        require(mac_live_window_pump(window_.get()) == MAC_LIVE_RUNNING, "window input pump failed");
        std::vector<mac_live_event> events;
        mac_live_event event{};
        while (mac_live_window_next_event(window_.get(), &event)) {
            events.push_back(event);
        }
        require(GetCapture() != hwnd_, "the hidden test window captured system mouse input");
        return events;
    }

    void pump()
    {
        const auto events = pump_events();
        const auto cycle = current_cycle();
        for (const auto &event : events) {
            require(mac_mouse_queue_motion(mouse_.get(), cycle, event.dx, event.dy),
                    "motion queue rejected a window event");
            if (event.button_changed) {
                require(mac_mouse_queue_button(mouse_.get(), cycle, event.pressed),
                        "button queue rejected a window event");
            }
        }
    }

    // Execute at most one pending pin transition, at the same instruction
    // boundary used by the live run. This lets the probe check short edges.
    bool apply_next()
    {
        uint64_t cycle = 0;
        while (mac_mouse_next_cycle(mouse_.get(), &cycle)) {
            run_to(cycle);
            mac_mouse_state state{};
            if (mac_mouse_advance(mouse_.get(), current_cycle(), &state)) {
                mac128k_set_mouse_phase(machine_.get(), state.x_phase, state.y_phase);
                mac128k_set_mouse_button(machine_.get(), state.pressed);
                return true;
            }
            require(mac_mouse_next_cycle(mouse_.get(), &cycle) && cycle > current_cycle(),
                    "mouse did not advance or schedule a future transition");
        }
        require(mac_mouse_pending(mouse_.get()) == 0, "mouse queue has no usable deadline");
        return false;
    }

    void settle()
    {
        while (apply_next()) {
        }
        run_to(current_cycle() + settle_cycles);
    }

    void move(Point position, bool held = false)
    {
        post(WM_MOUSEMOVE, position, held ? MK_LBUTTON : 0);
        pump();
        settle();
    }

    void close()
    {
        post(WM_CLOSE);
        require(mac_live_window_pump(window_.get()) == MAC_LIVE_CLOSED,
                "owned window did not process close");
        require(!IsWindow(hwnd_), "closed test window still exists");
    }

    uint64_t current_cycle() const { return mac128k_get_state(machine_.get()).cycles; }

    uint8_t via_sample()
    {
        run_to(current_cycle() + 256);
        uint8_t sample = 0;
        require(mac128k_read_ram(machine_.get(), diagnostic_address, &sample, 1),
                "VIA probe result is unavailable");
        return sample & 0x38;
    }

    DiagnosticMemory diagnostics() const
    {
        DiagnosticMemory memory{};
        require(mac128k_read_ram(machine_.get(), diagnostic_address, memory.data(), memory.size()),
                "ClasSICK guest diagnostics are unavailable");
        return memory;
    }

    std::string frame_hash() const
    {
        return sha256(std::span(mac128k_frame(machine_.get()), MAC128K_FRAME_SIZE));
    }

private:
    std::unique_ptr<mac128k, decltype(&mac128k_destroy)> machine_;
    std::unique_ptr<mac_mouse, decltype(&mac_mouse_destroy)> mouse_;
    std::unique_ptr<mac_live_window, decltype(&mac_live_window_destroy)> window_;
    HWND hwnd_ = nullptr;
    int scale_;
};

void test_captured_edges()
{
    LiveMac live(make_via_probe(), 0, 2);
    live.post(WM_MOUSEMOVE, {2, 1});
    live.post(WM_LBUTTONDOWN, {2, 1}, MK_LBUTTON);
    live.post(WM_MOUSEMOVE, {-8, -6}, MK_LBUTTON);
    live.post(WM_MOUSEMOVE, {-25, -20}, MK_LBUTTON);
    live.post(WM_MOUSEMOVE, {1, 1}, MK_LBUTTON);
    live.post(WM_MOUSEMOVE, {530, 360}, MK_LBUTTON);
    live.post(WM_LBUTTONUP, {570, 390});
    live.post(WM_MOUSEMOVE, {510, 340});
    live.post(WM_MOUSEMOVE, {509, 340});

    const auto events = live.pump_events();
    constexpr std::array<mac_live_event, 6> expected{{
        {0, 0, true, true},
        {-2, -1, false, true},
        {1, 1, false, true},
        {510, 340, false, true},
        {0, 0, true, false},
        {-1, 0, false, false},
    }};
    require(events.size() == expected.size(), "captured edge motion produced extra or missing events");
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto &got = events[index];
        const auto &want = expected[index];
        require(got.dx == want.dx && got.dy == want.dy && got.button_changed == want.button_changed &&
                    got.pressed == want.pressed,
                "captured drag produced the wrong movement or button state");
    }
    live.close();
}

void test_guest_pins()
{
    LiveMac live(make_via_probe(), 0, 2);
    live.run_to(1000);
    require(live.via_sample() == 0x38, "reset mouse pins did not reflect a released button");
    live.post(WM_MOUSEMOVE, {100, 100});
    live.post(WM_LBUTTONDOWN, {100, 100}, MK_LBUTTON);
    live.post(WM_MOUSEMOVE, {101, 100}, MK_LBUTTON);
    live.post(WM_LBUTTONUP, {101, 100});
    live.pump();

    require(live.apply_next() && live.via_sample() == 0x30, "short press was lost");
    require(live.apply_next(), "first X quadrature edge was lost");
    require(live.apply_next() && live.via_sample() == 0x20, "X motion did not reach VIA PB4");
    require(live.apply_next() && live.via_sample() == 0x28, "short release was lost");
    require(!live.apply_next(), "extra mouse event after the short drag");

    live.post(WM_LBUTTONDOWN, {101, 100}, MK_LBUTTON);
    live.pump();
    require(live.apply_next() && live.via_sample() == 0x20, "second press was lost");
    live.post(WM_CAPTURECHANGED);
    live.pump();
    require(live.apply_next() && live.via_sample() == 0x28, "capture loss failed to release");
    require(!live.apply_next(), "extra mouse event after capture loss");
    live.close();
}

template <std::size_t count>
std::array<uint32_t, count> read_record(const DiagnosticMemory &memory, std::string_view tag)
{
    const auto begin = std::search(memory.begin(), memory.end(), tag.begin(), tag.end());
    require(begin != memory.end() && memory.end() - begin >= static_cast<std::ptrdiff_t>(count * 4),
            "missing or truncated ClasSICK diagnostic " + std::string(tag));
    std::array<uint32_t, count> result{};
    for (std::size_t word = 0; word < count; ++word) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            result[word] = (result[word] << 8) | begin[word * 4 + byte];
        }
    }
    return result;
}

uint32_t expect_scene(const LiveMac &live, Point pointer, Point window, bool pressed,
                      uint32_t pattern, uint32_t presses)
{
    const auto memory = live.diagnostics();
    const auto trace = read_record<32>(memory, "MWP1");
    const auto diag = read_record<7>(memory, "CS09");
    const auto loss = read_record<5>(memory, "MLC1");
    require(trace[1] == 2 && trace[2] == 128 && trace[8] == 1,
            "unexpected candidate diagnostic profile");
    require(diag[1] == 16 && diag[4] == 1 && diag[6] == 0, "candidate did not reach stage 16");
    const auto expected = show_point(pointer);
    require(trace[9] == 0 &&
                std::all_of(loss.begin() + 1, loss.end(), [](auto value) { return value == 0; }),
            "ClasSICK recorded a hard mouse loss approaching " + expected);
    const auto omissions = read_record<2>(memory, "MMD1")[1];
    const auto merges = read_record<2>(memory, "MCM1")[1];
    require(omissions == 0, "ClasSICK recorded " + std::to_string(omissions) +
                                " omissions approaching " + expected);

    const Point actual_pointer{static_cast<int>(trace[13]), static_cast<int>(trace[14])};
    const Point actual_window{static_cast<int>(trace[15]), static_cast<int>(trace[16])};
    require(actual_pointer == pointer,
            "pointer " + show_point(actual_pointer) + " expected " + expected);
    require(actual_window == window,
            "window " + show_point(actual_window) + " expected " + show_point(window));
    require(trace[22] == (pressed ? 1u : 0u), "ClasSICK observed the wrong primary button state");
    require(trace[17] == pattern, "ClasSICK content pattern did not match");
    require(diag[3] == presses, "ClasSICK press count did not match");
    return merges;
}

uint32_t test_candidate(const Rom &rom)
{
    LiveMac live(rom, 3, 1, candidate_vbl_guard_cycles);
    constexpr Point original_window{80, 70};
    live.run_to(100'000'000);
    expect_scene(live, {256, 171}, original_window, false, 0, 0);
    live.move({256, 171});
    expect_scene(live, {256, 171}, original_window, false, 0, 0);

    struct Corner {
        Point pointer;
        std::string_view frame;
    };
    // Exact packed frame hashes from the ClasSICK 2026-11-12 scene oracle.
    constexpr std::array<Corner, 5> corners{{
        {{0, 0}, "51a509f42d840ef39c363a49baa8ededa524b8a89a8759e8508f237e7ec6dd96"},
        {{511, 0}, "2b389d83ce0ed315de9f3cb052418a5df839267cfe70450593b4c842ba875272"},
        {{511, 341}, "1d69b666ae3a62b50723809b99b6344e4e7ebef237afe4130dce24ab2bde778f"},
        {{0, 341}, "2bc2462f0e2281cfd610fc730bf940a7b2006cc7624ec9ed2a36f68aaeaac60c"},
        {{256, 170}, "1c8f2437718c1a00b73c0e47383af75680588c3d3bd74f3afaba35832439b63f"},
    }};
    for (const auto &corner : corners) {
        live.move(corner.pointer);
        expect_scene(live, corner.pointer, original_window, false, 0, 0);
        require(live.frame_hash() == corner.frame,
                "corner frame differs from the ClasSICK scene oracle at " + show_point(corner.pointer));
    }

    live.move({100, 80});
    live.post(WM_LBUTTONDOWN, {100, 80}, MK_LBUTTON);
    live.pump();
    live.settle();
    expect_scene(live, {100, 80}, original_window, true, 0, 1);
    live.move({132, 80}, true);
    expect_scene(live, {132, 80}, {112, 70}, true, 0, 1);
    live.post(WM_LBUTTONUP, {132, 80});
    live.pump();
    live.settle();
    expect_scene(live, {132, 80}, {112, 70}, false, 0, 1);
    live.move({148, 80});
    expect_scene(live, {148, 80}, {112, 70}, false, 0, 1);

    live.post(WM_LBUTTONDOWN, {148, 80}, MK_LBUTTON);
    live.pump();
    live.settle();
    expect_scene(live, {148, 80}, {112, 70}, true, 0, 2);
    live.move({148, 56}, true);
    expect_scene(live, {148, 56}, {112, 46}, true, 0, 2);
    live.post(WM_LBUTTONUP, {148, 56});
    live.pump();
    live.settle();
    live.move({148, 64});
    expect_scene(live, {148, 64}, {112, 46}, false, 0, 2);

    live.move({150, 110});
    live.post(WM_LBUTTONDOWN, {150, 110}, MK_LBUTTON);
    live.post(WM_LBUTTONUP, {150, 110});
    live.pump();
    live.settle();
    // MCM1 counts accepted tail coalescing. The waypoint and frame checks
    // verify the resulting scene, including transitions across button states.
    const auto merged = expect_scene(live, {150, 110}, {112, 46}, false, 1, 3);
    live.close();
    return merged;
}

} // namespace

int main()
{
    try {
        test_captured_edges();
        test_guest_pins();
        const DWORD length = GetEnvironmentVariableW(fixture_variable, nullptr, 0);
        if (length == 0) {
            std::cout << "Optional ClasSICK live ROM fixture not supplied.\n";
            return 0;
        }
        std::wstring path(length, L'\0');
        const DWORD copied = GetEnvironmentVariableW(fixture_variable, path.data(), length);
        require(copied == length - 1, "cannot read the ClasSICK live ROM path");
        path.resize(copied);
        std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        Rom rom{};
        require(file && file.tellg() == static_cast<std::streamoff>(rom.size()),
                "ClasSICK live ROM must be exactly 65536 bytes");
        file.seekg(0);
        require(static_cast<bool>(file.read(reinterpret_cast<char *>(rom.data()), rom.size())),
                "cannot read ClasSICK live ROM");
        require(sha256(rom) == fixture_hash, "ClasSICK live ROM SHA-256 differs from the fixture");
        const auto merged = test_candidate(rom);
        std::cout << "ClasSICK live ROM accepted corners, drags and a click. "
                  << "Zero hard losses, zero omissions and " << merged << " accepted merges.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
