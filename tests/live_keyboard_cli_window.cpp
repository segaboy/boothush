#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mac_live_window.h"
#include <string_view>
#include <cstdlib>

namespace {
bh_live_window *test_window=nullptr;
struct Find {const wchar_t *name;HWND result;};
BOOL CALLBACK find(HWND h,LPARAM p)
{
    auto &f=*reinterpret_cast<Find*>(p);wchar_t name[80]{};
    if(GetClassNameW(h,name,80) && std::wstring_view(name)==f.name){f.result=h;return FALSE;}return TRUE;
}
void CALLBACK input(HWND h,UINT,UINT_PTR timer,DWORD)
{
    KillTimer(h,timer);
    wchar_t relative[2]{};
    if(GetEnvironmentVariableW(L"BOOTHUSH_TEST_RELATIVE_ERROR",relative,2)==1 && relative[0]==L'1') {
        SendMessageW(h,WM_KEYDOWN,VK_F12,1);
        SendMessageW(h,WM_INPUT,RIM_INPUT,0); // Invalid handle, no physical input.
        return;
    }
    if(GetEnvironmentVariableW(L"BOOTHUSH_TEST_RELATIVE",relative,2)==1 && relative[0]==L'1') {
        SendMessageW(h,WM_KEYDOWN,VK_F12,1);
        if(!mac_live_window_inject_relative(test_window,64,0))PostMessageW(h,WM_CLOSE,0,0);
        SendMessageW(h,WM_KEYDOWN,VK_F12,1);
    }
    // A then F down; focus loss must deliver both releases. A later close
    // lets the guest observe all transitions before final inspection.
    PostMessageW(h,WM_KEYDOWN,'A',1);PostMessageW(h,WM_KEYDOWN,'F',1);
    PostMessageW(h,WM_KILLFOCUS,0,0);
    SetTimer(h,2,200,[](HWND w,UINT,UINT_PTR id,DWORD){KillTimer(w,id);PostMessageW(w,WM_CLOSE,0,0);});
}
bh_live_window *create(bh_live_profile profile,unsigned scale,bool visible)
{
    if(!visible)return nullptr;
    auto *w=bh_live_window_create(profile,scale,false);if(!w)return nullptr;
    test_window=w;
    Find f{profile==BH_LIVE_PC286?L"BootHushPC286LiveWindow":L"BootHushMac128KLiveWindow",nullptr};
    EnumThreadWindows(GetCurrentThreadId(),find,reinterpret_cast<LPARAM>(&f));
    // Optional bounded delay for slower original integration guests. This is
    // a test-only binary: every window remains hidden regardless of the delay.
    unsigned delay=150;wchar_t value[32]{};
    const DWORD count=GetEnvironmentVariableW(L"BOOTHUSH_TEST_INPUT_DELAY_MS",value,32);
    if(count) {
        wchar_t *end=nullptr;const auto parsed=std::wcstoul(value,&end,10);
        if(count>=32 || *end || parsed<1 || parsed>20000){mac_live_window_destroy(w);return nullptr;}
        delay=static_cast<unsigned>(parsed);
    }
    if(!f.result || IsWindowVisible(f.result) || !SetTimer(f.result,1,delay,input)){mac_live_window_destroy(w);return nullptr;}
    return w;
}
}
extern "C" mac_live_window *boothush_key_test_mac_create(unsigned scale,bool visible){return create(BH_LIVE_MAC128K,scale,visible);}
extern "C" bh_live_window *boothush_key_test_create(bh_live_profile profile,unsigned scale,bool visible){return create(profile,scale,visible);}
