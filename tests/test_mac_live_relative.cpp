#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mac_live_window.h"
#include <iostream>
#include <stdexcept>
#include <string_view>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static BOOL CALLBACK find(HWND h,LPARAM data)
{
    wchar_t name[80]{};GetClassNameW(h,name,80);
    if(std::wstring_view(name)==L"BootHushMac128KLiveWindow"){*reinterpret_cast<HWND*>(data)=h;return FALSE;}return TRUE;
}
int main()
{
    try {
        RECT clip_before{},clip_after{};check(GetClipCursor(&clip_before),"initial clip");
        auto *w=mac_live_window_create(2,false);check(w,"window");HWND h=nullptr;
        EnumThreadWindows(GetCurrentThreadId(),find,reinterpret_cast<LPARAM>(&h));check(h && !IsWindowVisible(h),"hidden window");
        check(!mac_live_window_relative_gain(w,24) && !mac_live_window_relative_gain(w,801),"invalid gain accepted");
        check(mac_live_window_relative_gain(w,100),"initial gain");
        check(mac_live_window_relative_mode(w,true),"enable");
        check(!mac_live_window_relative_gain(w,200),"configuration changed while enabled");
        check(!mac_live_window_inject_relative(w,20,20),"uncaptured motion accepted");
        SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100));SendMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(100,100));
        mac_live_event e{};check(!mac_live_window_next_event(w,&e),"capture click reached guest");
        check(mac_live_window_pointer_state(w).captured,"click did not capture");
        check(mac_live_window_inject_relative(w,1,-1),"fraction packet");check(!mac_live_window_next_event(w,&e),"fraction rounded early");
        check(mac_live_window_inject_relative(w,1,-1),"second fraction packet");
        check(mac_live_window_next_event(w,&e) && e.dx==1 && e.dy==-1,"fraction accumulated incorrectly");
        // Legacy cursor reaches its edge but cannot cap raw relative travel.
        SendMessageW(h,WM_MOUSEMOVE,0,MAKELPARAM(1023,683));check(!mac_live_window_next_event(w,&e),"duplicate legacy motion");
        check(mac_live_window_inject_relative(w,4096,-4096),"unbounded delta");
        check(mac_live_window_next_event(w,&e) && e.dx==2048 && e.dy==-2048,"host edge capped delta");
        SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,0);
        check(mac_live_window_next_event(w,&e) && e.button_changed && e.pressed,"primary press missing");
        SendMessageW(h,WM_KILLFOCUS,0,0);
        check(mac_live_window_next_event(w,&e) && e.button_changed && !e.pressed,"focus release missing");
        check(!mac_live_window_pointer_state(w).captured && !mac_live_window_inject_relative(w,1,1),"focus did not release capture");
        SendMessageW(h,WM_KEYDOWN,VK_F12,1);check(mac_live_window_pointer_state(w).captured,"F12 capture");
        SendMessageW(h,WM_KEYDOWN,VK_F12,LPARAM(1)|(LPARAM(1)<<30));check(mac_live_window_pointer_state(w).captured,"F12 repeat toggled");
        SendMessageW(h,WM_KEYDOWN,VK_F12,1);check(!mac_live_window_pointer_state(w).captured,"F12 release");
        const auto s=mac_live_window_pointer_state(w);check(s.packets==3 && s.captures==2 && s.releases==2,"capture accounting");
        SendMessageW(h,WM_KEYDOWN,VK_F10,1);
        SendMessageW(h,WM_KEYDOWN,VK_F10,LPARAM(1)|(LPARAM(1)<<30));
        SendMessageW(h,WM_KEYUP,VK_F10,1);
        check(mac_live_window_pointer_state(w).gain_percent==200,"repeat/key-up changed gain");
        SendMessageW(h,WM_KEYDOWN,VK_F10,1);SendMessageW(h,WM_KEYDOWN,VK_F12,1);
        check(mac_live_window_inject_relative(w,3,-3),"400 percent motion");
        check(mac_live_window_next_event(w,&e) && e.dx==6 && e.dy==-6,"400 percent scaling");
        for(int i=0;i<8;++i)SendMessageW(h,WM_KEYDOWN,VK_F9,1);
        check(mac_live_window_pointer_state(w).gain_percent==25,"lower gain bound");
        for(int i=0;i<7;++i){check(mac_live_window_inject_relative(w,1,-1),"low gain input");check(!mac_live_window_next_event(w,&e),"low gain rounded early");}
        check(mac_live_window_inject_relative(w,1,-1),"low gain eighth input");
        check(mac_live_window_next_event(w,&e) && e.dx==1 && e.dy==-1,"low gain fractional conservation");
        check(mac_live_window_inject_relative(w,3,-3),"partial fraction");
        SendMessageW(h,WM_KEYDOWN,VK_F10,1); // Reset a partial fraction on sensitivity change.
        for(int i=0;i<3;++i){check(mac_live_window_inject_relative(w,1,-1),"new gain input");check(!mac_live_window_next_event(w,&e),"old fraction leaked into new gain");}
        check(mac_live_window_inject_relative(w,1,-1),"new gain fourth input");
        check(mac_live_window_next_event(w,&e) && e.dx==1 && e.dy==-1,"new gain fraction");
        for(int i=0;i<8;++i)SendMessageW(h,WM_KEYDOWN,VK_F10,1);
        const auto gain=mac_live_window_pointer_state(w);
        check(gain.initial_gain_percent==100 && gain.gain_percent==800 && gain.gain_changes==11,"gain history or upper bound");
        bh_live_key key{};check(!bh_live_window_next_key(w,&key),"host sensitivity key reached guest");
        SendMessageW(h,WM_CLOSE,0,0);
        check(mac_live_window_pump(w)==MAC_LIVE_CLOSED && !mac_live_window_pointer_state(w).captured,"close capture cleanup");
        mac_live_window_destroy(w);
        w=mac_live_window_create(1,false);h=nullptr;EnumThreadWindows(GetCurrentThreadId(),find,reinterpret_cast<LPARAM>(&h));
        check(w && h && mac_live_window_relative_gain(w,800) && mac_live_window_relative_options(w,true,false),"overflow fixture");
        SendMessageW(h,WM_KEYDOWN,VK_F12,1);
        check(!mac_live_window_inject_relative(w,INT32_MAX,0),"scaled signed overflow accepted");
        check(mac_live_window_pump(w)==MAC_LIVE_INPUT_ERROR && mac_live_window_pointer_state(w).failure,"overflow diagnostic missing");
        mac_live_window_destroy(w);
        check(GetClipCursor(&clip_after) && EqualRect(&clip_before,&clip_after) && !GetCapture(),"hidden test changed desktop input");
        std::cout<<"Hidden relative mouse, sensitivity/fraction bounds, overflow, focus/F12/close cleanup checks passed.\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
