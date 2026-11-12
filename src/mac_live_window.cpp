#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#include <windows.h>
#include <windowsx.h>

#include "mac_live_window.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <new>

namespace {

constexpr wchar_t class_name[] = L"BootHushMac128KLiveWindow";
constexpr wchar_t title[] = L"BootHush \u2014 Macintosh 128K";
constexpr DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr int frame_width = MAC128K_FRAME_WIDTH;
constexpr int frame_height = MAC128K_FRAME_HEIGHT;
constexpr unsigned max_scale = 4;
constexpr unsigned message_budget = 128;

struct Viewport {
    int left, top, scale;

    int right() const { return left + frame_width * scale; }
    int bottom() const { return top + frame_height * scale; }

    bool contains(int x, int y) const
    {
        return x >= left && x < right() && y >= top && y < bottom();
    }
};

struct BitmapInfo {
    BITMAPINFOHEADER header;
    RGBQUAD colors[2];
};

constexpr BitmapInfo bitmap = {
    {sizeof(BITMAPINFOHEADER), frame_width, -frame_height, 1, 1, BI_RGB,
     MAC128K_FRAME_SIZE, 0, 0, 2, 2},
    {{255, 255, 255, 0}, {0, 0, 0, 0}},
};

RECT outer_size(unsigned scale, UINT dpi)
{
    RECT rect{0, 0, frame_width * static_cast<LONG>(scale), frame_height * static_cast<LONG>(scale)};
    if (!AdjustWindowRectExForDpi(&rect, window_style, FALSE, 0, dpi)) {
        rect = {0, 0, frame_width * static_cast<LONG>(scale), frame_height * static_cast<LONG>(scale)};
        AdjustWindowRectEx(&rect, window_style, FALSE, 0);
    }
    return rect;
}

} // namespace

struct mac_live_window {
    explicit mac_live_window(bool show) : visible(show) {}

    HWND hwnd = nullptr;
    std::array<uint8_t, MAC128K_FRAME_SIZE> frame{};
    std::array<mac_live_event, 512> events{};
    std::size_t next_event = 0;
    std::size_t event_count = 0;
    const bool visible;
    bool input_overflow = false;
    bool active = false;
    bool anchored = false;
    bool tracking_leave = false;
    bool pressed = false;
    int guest_x = 0;
    int guest_y = 0;

    void enqueue(mac_live_event event)
    {
        if (event_count == events.size()) {
            input_overflow = true;
            return;
        }
        events[(next_event + event_count) % events.size()] = event;
        ++event_count;
    }

    bool accepts_input() const { return active || !visible; }
    bool captured() const { return pressed && (!visible || GetCapture() == hwnd); }

    void change_button(bool down)
    {
        if (pressed != down) {
            pressed = down;
            enqueue({0, 0, true, down});
        }
    }

    void abandon_input(bool release_capture)
    {
        anchored = false;
        change_button(false);
        if (release_capture && GetCapture() == hwnd) {
            ReleaseCapture();
        }
    }

    Viewport viewport(const RECT &client) const
    {
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int scale = std::max(1, std::min(width / frame_width, height / frame_height));
        return {(width - frame_width * scale) / 2, (height - frame_height * scale) / 2, scale};
    }

    bool pointer_position(LPARAM coordinates, bool allow_captured)
    {
        RECT client{};
        if (!GetClientRect(hwnd, &client) || client.right <= 0 || client.bottom <= 0) {
            anchored = false;
            return false;
        }
        const auto view = viewport(client);
        const int x = GET_X_LPARAM(coordinates);
        const int y = GET_Y_LPARAM(coordinates);
        const bool in_client = x >= 0 && x < client.right && y >= 0 && y < client.bottom;
        if (visible && in_client && !tracking_leave) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            tracking_leave = TrackMouseEvent(&track) != FALSE;
        }
        if ((!in_client || !view.contains(x, y)) && !allow_captured) {
            anchored = false;
            return false;
        }

        // Keep captured drag coordinates at the nearest visible viewport pixel.
        // The first in-bounds reversal should not have to unwind off-window motion.
        const int next_x = (std::clamp(x, view.left, view.right() - 1) - view.left) / view.scale;
        const int next_y = (std::clamp(y, view.top, view.bottom() - 1) - view.top) / view.scale;
        if (anchored && (next_x != guest_x || next_y != guest_y)) {
            enqueue({next_x - guest_x, next_y - guest_y, false, pressed});
        }
        guest_x = next_x;
        guest_y = next_y;
        anchored = true;
        return true;
    }

    void paint() const
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        const auto view = viewport(client);
        const RECT bars[] = {
            {client.left, client.top, client.right, view.top},
            {client.left, view.bottom(), client.right, client.bottom},
            {client.left, view.top, view.left, view.bottom()},
            {view.right(), view.top, client.right, view.bottom()},
        };
        const HBRUSH brush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        for (const auto &bar : bars) {
            if (bar.left < bar.right && bar.top < bar.bottom) {
                FillRect(dc, &bar, brush);
            }
        }
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, view.left, view.top, frame_width * view.scale, frame_height * view.scale,
                      0, 0, frame_width, frame_height, frame.data(),
                      reinterpret_cast<const BITMAPINFO *>(&bitmap), DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &paint);
    }
};

namespace {

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto *window = reinterpret_cast<mac_live_window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        window = static_cast<mac_live_window *>(create->lpCreateParams);
        window->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    if (window == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    switch (message) {
    case WM_MOUSEACTIVATE:
        if (window->visible && !window->active && LOWORD(lparam) == HTCLIENT) {
            // The activation click has no guest cursor anchor yet.
            return MA_ACTIVATEANDEAT;
        }
        break;
    case WM_ACTIVATE:
        window->active = LOWORD(wparam) != WA_INACTIVE && HIWORD(wparam) == 0;
        if (!window->active) {
            window->abandon_input(true);
        }
        break;
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        window->abandon_input(true);
        break;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lparam) != hwnd && window->pressed) {
            window->abandon_input(false);
        }
        break;
    case WM_MOUSEMOVE:
        if (window->accepts_input()) {
            if (window->pressed && (wparam & MK_LBUTTON) == 0) {
                window->abandon_input(true);
            }
            window->pointer_position(lparam, window->captured());
        }
        return 0;
    case WM_LBUTTONDOWN:
        // Win32 already maps the configured physical primary to left-button messages.
        if (window->accepts_input() && !window->pressed && window->pointer_position(lparam, false)) {
            if (window->visible) {
                SetCapture(hwnd);
            }
            if (!window->visible || GetCapture() == hwnd) {
                window->change_button(true);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (window->pressed) {
            window->pointer_position(lparam, window->captured());
            window->change_button(false);
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            RECT client{};
            GetClientRect(hwnd, &client);
            if (!window->viewport(client).contains(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) {
                window->anchored = false;
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        window->tracking_leave = false;
        if (!window->pressed) {
            window->anchored = false;
        }
        return 0;
    case WM_SETCURSOR:
        if (window->active && LOWORD(lparam) == HTCLIENT) {
            POINT pointer{};
            RECT client{};
            if (GetCursorPos(&pointer) && ScreenToClient(hwnd, &pointer) &&
                GetClientRect(hwnd, &client) && window->viewport(client).contains(pointer.x, pointer.y)) {
                SetCursor(nullptr);
                return TRUE;
            }
        }
        break;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (wparam == HTCLOSE) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else if (wparam == HTMINBUTTON) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
        // Do not enter the native move, caption-button tracking, or system menu loops.
        return 0;
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_CONTEXTMENU:
        return 0;
    case WM_SYSCOMMAND:
        switch (wparam & 0xfff0) {
        case SC_MOVE:
        case SC_SIZE:
        case SC_MAXIMIZE:
        case SC_MOUSEMENU:
        case SC_KEYMENU:
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto *constraints = reinterpret_cast<MINMAXINFO *>(lparam);
        const auto minimum = outer_size(1, GetDpiForWindow(hwnd));
        constraints->ptMinTrackSize.x = std::max(constraints->ptMinTrackSize.x, minimum.right - minimum.left);
        constraints->ptMinTrackSize.y = std::max(constraints->ptMinTrackSize.y, minimum.bottom - minimum.top);
        return 0;
    }
    case WM_MOVE:
        window->anchored = false;
        break;
    case WM_SIZE:
        window->anchored = false;
        if (wparam == SIZE_MINIMIZED) {
            window->abandon_input(true);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        const auto *bounds = reinterpret_cast<const RECT *>(lparam);
        window->anchored = false;
        SetWindowPos(hwnd, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                     bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        window->paint();
        return 0;
    case WM_CLOSE:
        window->abandon_input(true);
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        window->abandon_input(true);
        window->hwnd = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

mac_live_window *mac_live_window_create(unsigned scale, bool visible)
{
    if (scale < 1 || scale > max_scale) {
        return nullptr;
    }
    auto *window = new (std::nothrow) mac_live_window(visible);
    if (window == nullptr) {
        return nullptr;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.lpszClassName = class_name;
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        delete window;
        return nullptr;
    }

    const UINT dpi = GetDpiForSystem();
    RECT bounds = outer_size(scale, dpi);
    RECT work_area{};
    if (visible && SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        while (scale > 1 && (bounds.right - bounds.left > work_area.right - work_area.left ||
                             bounds.bottom - bounds.top > work_area.bottom - work_area.top)) {
            bounds = outer_size(--scale, dpi);
        }
    }
    const HWND hwnd = CreateWindowExW(0, class_name, title, window_style, CW_USEDEFAULT, CW_USEDEFAULT,
                                      bounds.right - bounds.left, bounds.bottom - bounds.top,
                                      nullptr, nullptr, instance, window);
    if (hwnd == nullptr) {
        delete window;
        return nullptr;
    }
    if (visible) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    return window;
}

void mac_live_window_destroy(mac_live_window *window)
{
    if (window == nullptr) {
        return;
    }
    if (window->hwnd != nullptr) {
        window->abandon_input(true);
        DestroyWindow(window->hwnd);
    }
    delete window;
}

mac_live_status mac_live_window_pump(mac_live_window *window)
{
    for (unsigned message = 0; message < message_budget && window->hwnd != nullptr &&
         !window->input_overflow && window->events.size() - window->event_count >= 4; ++message) {
        MSG pending{};
        if (!PeekMessageW(&pending, window->hwnd, 0, 0, PM_REMOVE)) {
            break;
        }
        if (pending.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(pending.wParam));
            return MAC_LIVE_CLOSED;
        }
        DispatchMessageW(&pending);
    }
    if (window->input_overflow) {
        window->abandon_input(true);
        return MAC_LIVE_INPUT_OVERFLOW;
    }
    return window->hwnd != nullptr ? MAC_LIVE_RUNNING : MAC_LIVE_CLOSED;
}

bool mac_live_window_next_event(mac_live_window *window, mac_live_event *event)
{
    if (window->event_count == 0) {
        return false;
    }
    *event = window->events[window->next_event];
    window->next_event = (window->next_event + 1) % window->events.size();
    --window->event_count;
    return true;
}

bool mac_live_window_inject(mac_live_window *window, unsigned x, unsigned y, unsigned action)
{
    if (!window || window->visible || !window->hwnd || x >= frame_width || y >= frame_height || action > 3) {
        return false;
    }
    if ((action == 0 || action == 1) ? window->pressed : !window->pressed) {
        return false;
    }
    RECT client{};
    if (!GetClientRect(window->hwnd, &client)) {
        return false;
    }
    const auto view = window->viewport(client);
    const LPARAM point = MAKELPARAM(view.left + x * view.scale, view.top + y * view.scale);
    const UINT message = action == 1 ? WM_LBUTTONDOWN : action == 3 ? WM_LBUTTONUP : WM_MOUSEMOVE;
    SendMessageW(window->hwnd, message, action == 1 || action == 2 ? MK_LBUTTON : 0, point);
    return !window->input_overflow;
}

void mac_live_window_present(mac_live_window *window, const uint8_t frame[MAC128K_FRAME_SIZE])
{
    if (window->hwnd != nullptr && std::memcmp(window->frame.data(), frame, window->frame.size()) != 0) {
        std::memcpy(window->frame.data(), frame, window->frame.size());
        InvalidateRect(window->hwnd, nullptr, FALSE);
    }
}
