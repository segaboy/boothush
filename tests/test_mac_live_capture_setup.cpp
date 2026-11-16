// Exercise the production visible-setup branches with local API fakes only.
// No window, desktop clipping, physical input or raw-device registration occurs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace fixture {
int fail=0,clips=0,clip_reads=0,maps=0,registrations=0,removals=0,hides=0;
RECT clip{0,0,1920,1080};
LONG_PTR window=0;
bool denied(int operation){if(fail!=operation)return false;SetLastError(ERROR_ACCESS_DENIED);return true;}
BOOL WINAPI client(HWND,LPRECT r){if(denied(1))return FALSE;*r={0,0,1024,684};return TRUE;}
BOOL WINAPI get_clip(LPRECT r){++clip_reads;if(denied(2))return FALSE;*r=clip;return TRUE;}
BOOL WINAPI map(HWND,LPPOINT p){++maps;if(denied(3))return FALSE;p->x+=100;p->y+=100;return TRUE;}
BOOL WINAPI raw(PCRAWINPUTDEVICE d,UINT n,UINT size){
    if(n!=1 || size!=sizeof(*d) || d->usUsagePage!=1 || d->usUsage!=2)throw std::runtime_error("registration arguments");
    if(d->dwFlags==RIDEV_REMOVE){++removals;if(d->hwndTarget)throw std::runtime_error("remove target");return TRUE;}
    ++registrations;if(d->dwFlags || !d->hwndTarget)throw std::runtime_error("foreground registration");
    return denied(4)?FALSE:TRUE;
}
BOOL WINAPI set_clip(const RECT *r){++clips;if(denied(5))return FALSE;clip=*r;return TRUE;}
BOOL WINAPI title(HWND,LPCWSTR){return TRUE;}
HCURSOR WINAPI cursor(HCURSOR c){if(!c)++hides;return nullptr;}
LONG_PTR WINAPI user_data(HWND,int){return window;}
void reset(){fail=clips=clip_reads=maps=registrations=removals=hides=0;clip={0,0,1920,1080};}
}
#define GetClientRect fixture::client
#define GetClipCursor fixture::get_clip
#define ClientToScreen fixture::map
#define RegisterRawInputDevices fixture::raw
#define ClipCursor fixture::set_clip
#define SetWindowTextW fixture::title
#define SetCursor fixture::cursor
#undef GetWindowLongPtrW
#define GetWindowLongPtrW fixture::user_data
#include "../src/mac_live_window.cpp"

static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main()
{
    try {
        const char *operations[]={"", "GetClientRect", "GetClipCursor", "ClientToScreen", "RegisterRawInputDevices", "ClipCursor"};
        for(int fail=0;fail<=5;++fail) {
            fixture::reset();fixture::fail=fail;
            mac_live_window w(true,false);w.hwnd=reinterpret_cast<HWND>(1);w.active=true;w.relative.enabled=true;
            check(w.begin_relative()==(fail==0),"setup result");
            if(fail) {
                check(w.input_error && w.relative.failure==std::string_view(operations[fail]) && w.relative.win32_error==ERROR_ACCESS_DENIED,"specific API error lost");
                check(!w.relative.captured && w.relative.captures==0,"failed setup counted as capture");
                w.relative_failure("later error",123);check(w.relative.failure==std::string_view(operations[fail]),"first error replaced");
                check(fixture::removals==(fail==5?1:0),"failed clipping did not unregister");
            }else {
                check(w.relative.captured && fixture::clips==1 && fixture::hides==1,"confined setup missing");
                w.end_relative();check(fixture::clips==2 && fixture::removals==1,"confined cleanup missing");
            }
            check(fixture::clip.left==0 && fixture::clip.right==1920,"clip not restored or failed mutation");
        }
        fixture::reset();fixture::fail=5;
        mac_live_window free(true,false);free.hwnd=reinterpret_cast<HWND>(1);free.active=true;free.relative.enabled=true;free.relative.unconfined=true;
        check(free.begin_relative(),"explicit free mode failed with denied clipping");
        free.end_relative();
        check(!free.input_error && fixture::registrations==1 && fixture::removals==1,"free registration cleanup");
        check(fixture::clips==0 && fixture::clip_reads==0 && fixture::maps==0 && fixture::hides==0,"free mode touched confinement or hid cursor");
        fixture::reset();fixture::window=reinterpret_cast<LONG_PTR>(&free);free.active=false;
        window_proc(free.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,0);
        window_proc(free.hwnd,WM_KEYDOWN,VK_F12,1);
        window_proc(free.hwnd,WM_KEYDOWN,VK_F10,1);
        check(free.relative.gain_percent==100,"inactive shortcut changed gain");
        check(!free.input_error && !free.relative.captured && fixture::registrations==0,"inactive input treated as capture failure");
        std::cout<<"Capture setup, API denial/rollback, unconfined mode and inactive input checks passed.\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
