#pragma once

#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

class MouseHook {
public:
    static void init();
    static void clip_to_viewport();
    static void release_clip();
    static void lock_mouse();
    static void unlock_mouse();
    static bool is_locked();
    static void process_dimousestate(DIMOUSESTATE* state);
    static void process_dimousestate2(DIMOUSESTATE2* state);
    static void process_dideviceobjectdata(DIDEVICEOBJECTDATA* data, DWORD count);
    static void transform_client_pos(POINT* pt);
    static void transform_screen_pos(POINT* pt);
    static void subclass_secondary_windows();
};
