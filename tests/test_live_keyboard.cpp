#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mac_live_window.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Find {const wchar_t *name;HWND hwnd;};
static BOOL CALLBACK find(HWND hwnd,LPARAM param)
{
    auto &f=*reinterpret_cast<Find*>(param);wchar_t name[80]{};
    if(GetClassNameW(hwnd,name,80) && std::wstring_view(name)==f.name){f.hwnd=hwnd;return FALSE;}return TRUE;
}
static HWND handle(bool pc)
{
    Find f{pc?L"BootHushPC286LiveWindow":L"BootHushMac128KLiveWindow",nullptr};
    EnumThreadWindows(GetCurrentThreadId(),find,reinterpret_cast<LPARAM>(&f));
    check(f.hwnd && !IsWindowVisible(f.hwnd),"test window must remain hidden");return f.hwnd;
}
static bh_live_key next(bh_live_window *w,unsigned code,bool down,bool extended=false,bool repeated=false,bool released=false)
{
    bh_live_key k{};check(bh_live_window_next_key(w,&k),"missing key");
    check(k.code==code && k.pressed==down && k.extended==extended && k.repeated==repeated && k.focus_release==released,"key mismatch");return k;
}
int main()
{
    try {
        check(!bh_live_window_create(BH_LIVE_PC286,0,false),"scale zero accepted");
        check(!bh_live_window_create(static_cast<bh_live_profile>(99),1,false),"invalid profile accepted");
        for(bool pc:{false,true}) {
            auto *w=bh_live_window_create(pc?BH_LIVE_PC286:BH_LIVE_MAC128K,1,false);check(w,"create");HWND h=handle(pc);
            RECT r{};GetClientRect(h,&r);check(r.right==(pc?640:512) && r.bottom==(pc?350:342),"viewport size");
            // A key already held when focus arrived must not become a fresh press.
            SendMessageW(h,WM_KEYDOWN,'A',LPARAM(1)|(LPARAM(1)<<30));check(!bh_live_window_pending_keys(w),"startup repeat accepted");
            SendMessageW(h,WM_KEYUP,'A',LPARAM(1)|(LPARAM(1)<<31));check(!bh_live_window_pending_keys(w),"unmatched release accepted");
            SendMessageW(h,WM_KEYDOWN,'A',1);next(w,pc?0x1c:0,true);
            SendMessageW(h,WM_KEYDOWN,'A',LPARAM(1)|(LPARAM(1)<<30));
            if(pc)next(w,0x1c,true,false,true);else check(!bh_live_window_pending_keys(w),"Mac repeat copied");
            SendMessageW(h,WM_KEYDOWN,'F',1);next(w,pc?0x2b:3,true);
            SendMessageW(h,WM_KILLFOCUS,0,0);next(w,pc?0x1c:0,false,false,false,true);next(w,pc?0x2b:3,false,false,false,true);
            SendMessageW(h,WM_CANCELMODE,0,0);check(!bh_live_window_pending_keys(w),"duplicate focus releases");
            SendMessageW(h,WM_KEYDOWN,VK_SPACE,1);next(w,pc?0x29:0x31,true);
            SendMessageW(h,WM_KEYUP,VK_SPACE,1);next(w,pc?0x29:0x31,false);
            SendMessageW(h,WM_KEYDOWN,VK_LEFT,1);
            if(pc){next(w,0x6b,true,true);SendMessageW(h,WM_KEYUP,VK_LEFT,1);next(w,0x6b,false,true);}
            else check(!bh_live_window_pending_keys(w),"unsupported Mac arrow accepted");
            // A frame is accepted without exposing a window or taking foreground.
            std::array<uint8_t,640*350*3> rgb{};rgb[0]=255;rgb[4]=255;rgb.back()=255;
            bh_live_window_present_rgb(w,rgb.data());
            if(pc) {
                // Exercise the same DIB painting path offscreen, including channel
                // order, row orientation and the last pixel; no desktop capture.
                BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),640,-350,1,32,BI_RGB,0,0,0,0,0};
                HDC dc=CreateCompatibleDC(nullptr);void *pixels=nullptr;
                HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
                check(dc && bitmap,"offscreen DIB creation");HGDIOBJ old=SelectObject(dc,bitmap);
                SendMessageW(h,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
                check(GetPixel(dc,0,0)==RGB(255,0,0) && GetPixel(dc,1,0)==RGB(0,255,0) &&
                      GetPixel(dc,639,349)==RGB(0,0,255) && GetPixel(dc,639,0)==RGB(0,0,0),"RGB paint mismatch");
                SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
            }
            SendMessageW(h,WM_KEYDOWN,'W',1);next(w,pc?0x1d:13,true);
            SendMessageW(h,WM_CLOSE,0,0);check(mac_live_window_pump(w)==MAC_LIVE_CLOSED,"close did not terminate");
            next(w,pc?0x1d:13,false,false,false,true);mac_live_window_destroy(w);
        }
        auto *w=bh_live_window_create(BH_LIVE_PC286,1,false);HWND h=handle(true);
        for(unsigned i=0;i<513;++i)SendMessageW(h,WM_KEYDOWN,VK_SPACE,i?LPARAM(1)|(LPARAM(1)<<30):1);
        check(mac_live_window_pump(w)==MAC_LIVE_INPUT_OVERFLOW,"queue saturation hidden");mac_live_window_destroy(w);
        std::cout<<"Hidden Mac/286 viewport, mapping, repeat, startup, focus, close and overflow checks passed.\n";
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
