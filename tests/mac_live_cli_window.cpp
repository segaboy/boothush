#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "mac_live_window.h"

#include <array>
#include <string_view>

namespace {

constexpr std::wstring_view window_class = L"BootHushMac128KLiveWindow";
constexpr UINT_PTR close_timer = 1;

BOOL CALLBACK find_window(HWND hwnd, LPARAM parameter)
{
    std::array<wchar_t, 64> name{};
    if (GetClassNameW(hwnd, name.data(), static_cast<int>(name.size())) &&
        std::wstring_view(name.data()) == window_class) {
        *reinterpret_cast<HWND *>(parameter) = hwnd;
        return FALSE;
    }
    return TRUE;
}

void CALLBACK close_after_input(HWND hwnd, UINT, UINT_PTR timer, DWORD)
{
    KillTimer(hwnd, timer);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int left = (client.right - MAC128K_FRAME_WIDTH) / 2;
    const int top = (client.bottom - MAC128K_FRAME_HEIGHT) / 2;
    PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(left + 103, top + 99));
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

} // namespace

// Linked only by the test executable. Its main.c runs the normal live CLI.
extern "C" mac_live_window *boothush_test_window_create(unsigned scale, bool visible)
{
    if (scale != 1 || !visible) {
        return nullptr;
    }
    auto *window = mac_live_window_create(scale, false);
    if (!window) {
        return nullptr;
    }

    HWND hwnd = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), find_window, reinterpret_cast<LPARAM>(&hwnd));
    RECT client{};
    if (!hwnd || IsWindowVisible(hwnd) || !GetClientRect(hwnd, &client)) {
        mac_live_window_destroy(window);
        return nullptr;
    }
    const int left = (client.right - MAC128K_FRAME_WIDTH) / 2;
    const int top = (client.bottom - MAC128K_FRAME_HEIGHT) / 2;
    const LPARAM start = MAKELPARAM(left + 100, top + 100);
    const LPARAM end = MAKELPARAM(left + 101, top + 99);

    if (!PostMessageW(hwnd, WM_MOUSEMOVE, 0, start) ||
        !PostMessageW(hwnd, WM_MOUSEMOVE, 0, end) ||
        !PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, end) ||
        !PostMessageW(hwnd, WM_LBUTTONUP, 0, end) ||
        !SetTimer(hwnd, close_timer, 150, close_after_input)) {
        mac_live_window_destroy(window);
        return nullptr;
    }
    return window;
}
