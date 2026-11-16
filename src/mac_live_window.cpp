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
constexpr wchar_t pc_class_name[] = L"BootHushPC286LiveWindow";
constexpr wchar_t title[] = L"BootHush \u2014 Macintosh 128K | Click to focus; close to finish";
constexpr wchar_t pc_title[] = L"BootHush \u2014 PC/AT 286 | Keyboard; close to finish";
constexpr DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr int frame_width = MAC128K_FRAME_WIDTH;
constexpr int frame_height = MAC128K_FRAME_HEIGHT;
constexpr unsigned max_scale = 4;
constexpr unsigned message_budget = 128;

struct Viewport {
    int left, top, scale, width, height;

    int right() const { return left + width * scale; }
    int bottom() const { return top + height * scale; }

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

RECT outer_size(unsigned scale, UINT dpi, int width=frame_width, int height=frame_height)
{
    RECT rect{0, 0, width * static_cast<LONG>(scale), height * static_cast<LONG>(scale)};
    if (!AdjustWindowRectExForDpi(&rect, window_style, FALSE, 0, dpi)) {
        rect = {0, 0, width * static_cast<LONG>(scale), height * static_cast<LONG>(scale)};
        AdjustWindowRectEx(&rect, window_style, FALSE, 0);
    }
    return rect;
}

} // namespace

struct mac_live_window {
    mac_live_window(bool show, bool pc) : visible(show), is_pc(pc), width(pc?640:512), height(pc?350:342) {}

    HWND hwnd = nullptr;
    std::array<uint8_t, MAC128K_FRAME_SIZE> frame{};
    std::array<uint32_t,640*350> rgb{};
    std::array<bh_live_key,512> keys{};
    std::array<bool,256> held_keys{};
    std::array<bh_live_key,256> held_bindings{};
    std::size_t next_key=0,key_count=0;
    unsigned focus_losses=0;
    std::array<mac_live_event, 512> events{};
    std::size_t next_event = 0;
    std::size_t event_count = 0;
    const bool visible;
    const bool is_pc;
    const int width,height;
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

    void enqueue_key(bh_live_key key)
    {
        if(key_count==keys.size()){input_overflow=true;return;}
        keys[(next_key+key_count)%keys.size()]=key;++key_count;
    }
    void release_keys()
    {
        for(unsigned i=0;i<held_keys.size();++i) if(held_keys[i]) {
            auto key=held_bindings[i];key.pressed=false;key.repeated=false;key.focus_release=true;
            enqueue_key(key);held_keys[i]=false;
        }
    }
    void keyboard(UINT message,WPARAM vk,LPARAM detail)
    {
        if(!accepts_input() || vk>=256)return;
        const bool down=message==WM_KEYDOWN;
        unsigned index=static_cast<unsigned>(vk);
        if(vk==VK_SHIFT)index=((detail>>16)&255)==0x36?VK_RSHIFT:VK_LSHIFT;
        if(vk==VK_CONTROL)index=(detail&(1L<<24))?VK_RCONTROL:VK_LCONTROL;
        const bool repeat=down && held_keys[index];
        if(!down && !held_keys[index])return;
        // Do not claim a key held before activation. It must be released and pressed anew.
        if(down && (detail&(1L<<30)) && !held_keys[index])return;
        bh_live_key key{};key.pressed=down;key.repeated=repeat;
        if(!down){key=held_bindings[index];key.pressed=false;}
        else if(!binding(index,key))return;
        if(!is_pc && repeat)return; // M0110 transitions, not host typematic copies.
        key.repeated=repeat;key.focus_release=false;
        held_keys[index]=down;if(down)held_bindings[index]=key;
        enqueue_key(key);
    }
    bool binding(unsigned vk,bh_live_key &key) const
    {
        static constexpr uint8_t mac_letters[26]={0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
        static constexpr uint8_t pc_letters[26]={0x1c,0x32,0x21,0x23,0x24,0x2b,0x34,0x33,0x43,0x3b,0x42,0x4b,0x3a,0x31,0x44,0x4d,0x15,0x2d,0x1b,0x2c,0x3c,0x2a,0x1d,0x22,0x35,0x1a};
        static constexpr uint8_t mac_digits[10]={29,18,19,20,21,23,22,26,28,25};
        static constexpr uint8_t pc_digits[10]={0x45,0x16,0x1e,0x26,0x25,0x2e,0x36,0x3d,0x3e,0x46};
        if(vk>='A' && vk<='Z'){key.code=(is_pc?pc_letters:mac_letters)[vk-'A'];return true;}
        if(vk>='0' && vk<='9'){key.code=(is_pc?pc_digits:mac_digits)[vk-'0'];return true;}
        switch(vk) {
        case VK_SPACE:key.code=is_pc?0x29:0x31;return true;
        case VK_RETURN:key.code=is_pc?0x5a:0x24;return true;
        case VK_BACK:key.code=is_pc?0x66:0x33;return true;
        case VK_TAB:key.code=is_pc?0x0d:0x30;return true;
        case VK_OEM_MINUS:key.code=is_pc?0x4e:0x1b;return true;
        case VK_OEM_PLUS:key.code=is_pc?0x55:0x18;return true;
        case VK_OEM_4:key.code=is_pc?0x54:0x21;return true;
        case VK_OEM_6:key.code=is_pc?0x5b:0x1e;return true;
        case VK_OEM_1:key.code=is_pc?0x4c:0x29;return true;
        case VK_OEM_7:key.code=is_pc?0x52:0x27;return true;
        case VK_OEM_COMMA:key.code=is_pc?0x41:0x2b;return true;
        case VK_OEM_PERIOD:key.code=is_pc?0x49:0x2f;return true;
        case VK_OEM_2:key.code=is_pc?0x4a:0x2c;return true;
        case VK_OEM_5:key.code=is_pc?0x5d:0x2a;return true;
        default:break;
        }
        if(!is_pc)return false; // Unsupported M0110 modifiers/navigation are explicit.
        switch(vk) {
        case VK_LEFT:key.code=0x6b;key.extended=true;return true;
        case VK_RIGHT:key.code=0x74;key.extended=true;return true;
        case VK_UP:key.code=0x75;key.extended=true;return true;
        case VK_DOWN:key.code=0x72;key.extended=true;return true;
        case VK_ESCAPE:key.code=0x76;return true;
        case VK_LSHIFT:key.code=0x12;return true;
        case VK_RSHIFT:key.code=0x59;return true;
        case VK_LCONTROL:key.code=0x14;return true;
        case VK_RCONTROL:key.code=0x14;key.extended=true;return true;
        default:return false;
        }
    }

    Viewport viewport(const RECT &client) const
    {
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int scale = std::max(1, std::min(width / this->width, height / this->height));
        return {(width - this->width * scale) / 2, (height - this->height * scale) / 2, scale,this->width,this->height};
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

    void draw(HDC dc) const
    {
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
        if(is_pc) {
            BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,640*350*4,0,0,0,0};
            StretchDIBits(dc,view.left,view.top,width*view.scale,height*view.scale,0,0,width,height,
                          rgb.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        } else StretchDIBits(dc, view.left, view.top, frame_width * view.scale, frame_height * view.scale,
                      0, 0, frame_width, frame_height, frame.data(),
                      reinterpret_cast<const BITMAPINFO *>(&bitmap), DIB_RGB_COLORS, SRCCOPY);
    }
    void paint() const
    {
        PAINTSTRUCT paint{};
        const HDC dc=BeginPaint(hwnd,&paint);
        draw(dc);
        EndPaint(hwnd,&paint);
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
        if(window->active && LOWORD(wparam)==WA_INACTIVE)++window->focus_losses;
        window->active = LOWORD(wparam) != WA_INACTIVE && HIWORD(wparam) == 0;
        if (!window->active) {
            window->abandon_input(true);
            window->release_keys();
        }
        break;
    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        window->abandon_input(true);
        window->release_keys();
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
        window->keyboard(message,wparam,lparam);
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lparam) != hwnd && window->pressed) {
            window->abandon_input(false);
        }
        break;
    case WM_MOUSEMOVE:
        if(window->is_pc)break;
        if (window->accepts_input()) {
            if (window->pressed && (wparam & MK_LBUTTON) == 0) {
                window->abandon_input(true);
            }
            window->pointer_position(lparam, window->captured());
        }
        return 0;
    case WM_LBUTTONDOWN:
        if(window->is_pc)break;
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
        if(window->is_pc)break;
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
        if (!window->is_pc && window->active && LOWORD(lparam) == HTCLIENT) {
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
        const auto minimum = outer_size(1, GetDpiForWindow(hwnd),window->width,window->height);
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
            window->release_keys();
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
    case WM_PRINTCLIENT:
        window->draw(reinterpret_cast<HDC>(wparam));
        return 0;
    case WM_CLOSE:
        window->abandon_input(true);
        window->release_keys();
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        window->abandon_input(true);
        window->release_keys();
        window->hwnd = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

mac_live_window *mac_live_window_create(unsigned scale, bool visible)
{
    return bh_live_window_create(BH_LIVE_MAC128K,scale,visible);
}

bh_live_window *bh_live_window_create(bh_live_profile profile,unsigned scale,bool visible)
{
    if (scale < 1 || scale > max_scale || (profile!=BH_LIVE_MAC128K && profile!=BH_LIVE_PC286)) {
        return nullptr;
    }
    auto *window = new (std::nothrow) mac_live_window(visible,profile==BH_LIVE_PC286);
    if (window == nullptr) {
        return nullptr;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.lpszClassName = window->is_pc?pc_class_name:class_name;
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        delete window;
        return nullptr;
    }

    const UINT dpi = GetDpiForSystem();
    RECT bounds = outer_size(scale, dpi,window->width,window->height);
    RECT work_area{};
    if (visible && SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        while (scale > 1 && (bounds.right - bounds.left > work_area.right - work_area.left ||
                             bounds.bottom - bounds.top > work_area.bottom - work_area.top)) {
            bounds = outer_size(--scale, dpi,window->width,window->height);
        }
    }
    const HWND hwnd = CreateWindowExW(0, window_class.lpszClassName, window->is_pc?pc_title:title, window_style, CW_USEDEFAULT, CW_USEDEFAULT,
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
         !window->input_overflow && window->events.size() - window->event_count >= 4 &&
         window->keys.size()-window->key_count>=128; ++message) {
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
    if (!window || window->is_pc || window->visible || !window->hwnd || x >= frame_width || y >= frame_height || action > 3) {
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

void bh_live_window_present_rgb(bh_live_window *window,const uint8_t *rgb)
{
    if(!window || !window->is_pc || !window->hwnd || !rgb)return;
    bool changed=false;
    for(size_t i=0;i<window->rgb.size();++i) {
        const uint32_t pixel=(uint32_t(rgb[i*3])<<16)|(uint32_t(rgb[i*3+1])<<8)|rgb[i*3+2];
        changed|=window->rgb[i]!=pixel;window->rgb[i]=pixel;
    }
    if(changed)InvalidateRect(window->hwnd,nullptr,FALSE);
}
bool bh_live_window_next_key(bh_live_window *window,bh_live_key *key)
{
    if(!window || !key || !window->key_count)return false;
    *key=window->keys[window->next_key];window->next_key=(window->next_key+1)%window->keys.size();--window->key_count;return true;
}
size_t bh_live_window_pending_keys(const bh_live_window *window){return window?window->key_count:0;}
unsigned bh_live_window_focus_losses(const bh_live_window *window){return window?window->focus_losses:0;}
