// SPDX-License-Identifier: CC0-1.0
// CQ1-FPS for the official Windows release of Costume Quest: Cq.exe 1.0.0.11 (SHA-256
// 9d5acea0...addef8) and the current Steam build (d9e1ee3e...b46cf). Built as dinput8.dll and placed
// next to Cq.exe, which imports DINPUT8.dll and so loads this first; DirectInput8Create is passed on
// to the system's dinput8.dll.
//
// Costume Quest's world advances in fixed simulation ticks of 1/30 s and the game draws one frame
// per tick, so it runs at 30 fps however fast the machine is. This runs it at 60 or 120 Hz (as
// Costume Quest 2's "60 FPS" option does) and adds "60 FPS", "120 FPS" and "Show FPS" checkboxes
// to Help & Options > Settings > Advanced. Settings: cq1fps-settings.txt; log: cq1fps.log (both in
// the game folder).
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

// ---- addresses in Cq.exe ---------------------------------------------------------------------------
// Two builds are known: the retail 1.0.0.11 and the current Steam build (Jan 2026), where code moved
// by +0x420 (Lua by +0x440) and globals by -0x1900.
enum { L_TYPE, L_TONUMBER, L_TOBOOLEAN, L_PUSHNUMBER, L_PUSHCCLOSURE, L_GETFIELD, L_SETFIELD, L_SETTOP,
       L_TOLSTRING, L_LOADBUFFER, L_PCALL, L_GETTOP, L_PUSHBOOLEAN, L_COUNT };
static const struct build {
    const char *name;
    DWORD console;  // Console *g_pConsole (its lua_State at +0x18)
    struct {
        DWORD addr;
        unsigned char bytes[8];
        int n;
    } check[3];
    // The simulation period variable (1/30, read by GameApp::Init, GameApp::_RequestRender, the
    // SimWorld constructor and SimWorld::_UpdateTimeScale), the two "movss xmm0, [30.0]" loads of
    // the frequency (constructor, _UpdateTimeScale) and that constant, GameApp* (+0xe0 =
    // powf(0.1, period)) and SimulationManager* (+0x10: the 4 world slots).
    DWORD period, freq_load[2], freq_const, game_app, sim_manager;
    // The Advanced page's movie: FlashFileOpener's vtable slot for OpenFile and what it holds,
    // GSysFile::GSysFile(path, flags, mode) and the GFx allocator.
    DWORD opener_slot, opener_open, sysfile_ctor, gfx_alloc;
    DWORD lua[L_COUNT];  // Lua 5.1 (lua_Number is float), in the order of the enum above
} kBuilds[] = {
    {"Cq.exe 1.0.0.11", 0xa915b8,
     {{0x565150, {0x83, 0xec, 0x14, 0x53, 0x8b, 0x5c, 0x24, 0x1c}, 8},  // Console::ExecuteCommand
      {0x564f00, {0x55, 0x8b, 0x2d, 0xb8, 0x15, 0xa9, 0x00}, 7},        // Console::QueueCommand
      {0x565b50, {0xa1, 0xb8, 0x15, 0xa9, 0x00}, 5}},                   // luaapp_ToggleConsoleSpam
     0xa30c44, {0x5d1f04, 0x5d35ed}, 0x96e8a4, 0xa915b0, 0xa915c4,
     0x9a8a94, 0x6ea3b0, 0x7832e0, 0x7687a0,
     {0x754c00, 0x754d60, 0x754de0, 0x755000, 0x755150, 0x755280, 0x7554a0, 0x754a50, 0x754e10, 0x7564b0,
      0x755760, 0x754a40, 0x7551e0}},
    {"Cq.exe (Steam, 2026)", 0xa8fcb8,
     {{0x565570, {0x83, 0xec, 0x14, 0x53, 0x8b, 0x5c, 0x24, 0x1c}, 8},
      {0x565320, {0x55, 0x8b, 0x2d, 0xb8, 0xfc, 0xa8, 0x00}, 7},
      {0x565f70, {0xa1, 0xb8, 0xfc, 0xa8, 0x00}, 5}},
     0xa2f864, {0x5d1ad4, 0x5d31bd}, 0x96db14, 0xa8fcb0, 0xa8fcc4,
     0x9a7a54, 0x6ea570, 0x783730, 0x768bf0,
     {0x755040, 0x7551a0, 0x755220, 0x755440, 0x755590, 0x7556c0, 0x7558e0, 0x754e90, 0x755250, 0x7568f0,
      0x755ba0, 0x754e80, 0x755620}},
};
static struct build g_b;  // the running build, its addresses moved to where Windows loaded Cq.exe
static int g_ok;

typedef int (*LuaFn)(void *L);
#define LUA_GLOBALSINDEX (-10002)
#define LUA(i, type) ((type)g_b.lua[i])
#define lua_type LUA(L_TYPE, int (*)(void *, int))
#define lua_tonumber LUA(L_TONUMBER, float (*)(void *, int))
#define lua_toboolean LUA(L_TOBOOLEAN, int (*)(void *, int))
#define lua_pushnumber LUA(L_PUSHNUMBER, void (*)(void *, float))
#define lua_pushcclosure LUA(L_PUSHCCLOSURE, void (*)(void *, LuaFn, int))
#define lua_getfield LUA(L_GETFIELD, void (*)(void *, int, const char *))
#define lua_setfield LUA(L_SETFIELD, void (*)(void *, int, const char *))
#define lua_settop LUA(L_SETTOP, void (*)(void *, int))
#define lua_tolstring LUA(L_TOLSTRING, const char *(*)(void *, int, size_t *))
#define luaL_loadbuffer LUA(L_LOADBUFFER, int (*)(void *, const char *, size_t, const char *))
#define lua_pcall LUA(L_PCALL, int (*)(void *, int, int, int))
#define lua_gettop LUA(L_GETTOP, int (*)(void *))
#define lua_pushboolean LUA(L_PUSHBOOLEAN, void (*)(void *, int))
#define LUA_TBOOLEAN 1
#define LUA_TNUMBER 3

// ---- log and settings ------------------------------------------------------------------------------
static FILE *g_log;
static char g_dir[MAX_PATH];
static void logf(const char *fmt, ...) {
    if (!g_log) return;
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
}

#include "pausepatch.h"

#define SM_WORLDS 0x10    // SimulationManager: the 4 world slots
#define W_DIRTY 0xa58     // SimWorld: recompute period/frequency on the next tick
#define APP_SMOOTHING 0xe0
static float g_sim_freq = 30;  // the frequency loads read this instead of the 30.0 constant
static int g_sim_hz = 30, g_show_fps, g_vsync_off;  // g_vsync_off: 120 FPS switched V-Sync off
static volatile LONG g_want_hz;  // cq64.SetSim60 / SetSim120's wish, applied on the next timer tick
static volatile float g_frame_ms;  // average frame time (Present)
static char g_pause_path[MAX_PATH];

static void poke(DWORD addr, const void *src, size_t n) {
    DWORD prot;
    VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &prot);
    memcpy((void *)addr, src, n);
    VirtualProtect((void *)addr, n, prot, &prot);
}

// cq1fps-settings.txt: "simrate 30|60|120", "showfps 0|1", "vsyncoff 0|1" (120 FPS switched V-Sync off,
// so it comes back on after; the game saves V-Sync in its own options meanwhile).
static void save_settings(void) {
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\cq1fps-settings.txt", g_dir);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "simrate %d\nshowfps %d\nvsyncoff %d\n", g_sim_hz, g_show_fps, g_vsync_off);
    fclose(f);
}

static int load_settings(void) {  // the saved rate
    char p[MAX_PATH], l[64];
    int v, hz = 30;
    snprintf(p, sizeof p, "%s\\cq1fps-settings.txt", g_dir);
    FILE *f = fopen(p, "r");
    if (!f) return hz;
    while (fgets(l, sizeof l, f)) {
        if (sscanf(l, "simrate %d", &v) == 1 && (v == 30 || v == 60 || v == 120)) hz = v;
        if (sscanf(l, "showfps %d", &v) == 1) g_show_fps = v != 0;
        if (sscanf(l, "vsyncoff %d", &v) == 1) g_vsync_off = v != 0;
    }
    fclose(f);
    return hz;
}

// ---- the simulation rate -----------------------------------------------------------------------------
// Switches the simulation to 30, 60 or 120 Hz, at start or while playing (on the game thread).
static void set_sim_rate(int hz) {
    *(float *)g_b.period = 1.0f / hz;  // a variable in .data
    g_sim_freq = (float)hz;
    char *app = *(char **)g_b.game_app, *sim = *(char **)g_b.sim_manager;
    if (app) *(float *)(app + APP_SMOOTHING) = hz == 120 ? 0.980994713f : hz == 60 ? 0.962350607f : 0.926118731f;
    char **worlds = sim ? *(char ***)(sim + SM_WORLDS) : NULL;
    for (int i = 0; worlds && i < 4; i++)
        if (worlds[i]) worlds[i][W_DIRTY] = 1;  // existing worlds recompute on their next tick
    g_sim_hz = hz;
    logf("simulation at %d Hz", hz);
}

// FlashFileOpener::OpenFile: the Advanced page's movie comes from our copy, the rest as usual.
typedef void *(__thiscall *OpenFileFn)(void *self, const char *path);
typedef void *(__thiscall *SysFileCtor)(void *self, const char *path, int flags, int mode);
typedef void *(__stdcall *GfxAlloc)(unsigned size);
static void *__thiscall open_file(void *self, const char *p) {
    size_t n = strlen(p);
    if (*g_pause_path && n >= 19 && !_stricmp(p + n - 19, "pause/opt/pause.gfx")) {
        char *f = (char *)((GfxAlloc)g_b.gfx_alloc)(0x20);  // a GSysFile is 0x14 bytes
        if (f) {
            *(DWORD *)(f + 4) = 0x56471e89, *(DWORD *)(f + 8) = 0x9fe1234a;  // as the game does
            return ((SysFileCtor)g_b.sysfile_ctor)(f, g_pause_path, 1, 0x1b6);
        }
    }
    return ((OpenFileFn)g_b.opener_open)(self, p);
}

static void install(void) {
    static const unsigned char kLoad[4] = {0xf3, 0x0f, 0x10, 0x05};  // movss xmm0, [addr]
    int ok = *(float *)g_b.period == 1.0f / 30 && *(float *)g_b.freq_const == 30.0f;
    for (int i = 0; i < 2; i++)
        ok = ok && !memcmp((void *)g_b.freq_load[i], kLoad, 4) && *(DWORD *)(g_b.freq_load[i] + 4) == g_b.freq_const;
    if (!ok) {
        logf("simulation code not as expected: CQ1-FPS off");
        return;
    }
    DWORD a = (DWORD)&g_sim_freq;
    for (int i = 0; i < 2; i++) poke(g_b.freq_load[i] + 4, &a, 4);
    g_ok = 1;
    int hz = load_settings();
    if (hz != 30) set_sim_rate(hz);
    // The Advanced page's additions (ui/advanced.as), in a copy of the player's own movie.
    snprintf(g_pause_path, sizeof g_pause_path, "%s\\cq1fps-pause.gfx", g_dir);
    char pack[MAX_PATH];
    snprintf(pack, sizeof pack, "%s\\Win\\Packs\\Man_Gfx.~h", g_dir);
    DWORD fn = (DWORD)open_file;
    if (*(DWORD *)g_b.opener_slot == g_b.opener_open && pp_build_pause(pack, g_pause_path))
        poke(g_b.opener_slot, &fn, 4);
    else *g_pause_path = 0;
}

// ---- Lua ------------------------------------------------------------------------------------------------
// cq1fps_rate([hz]): the simulation rate (the one asked for, if not applied yet); 30, 60 or 120 asks
// for it (applied on the next timer tick)
static int l_rate(void *L) {
    if (lua_type(L, 1) == LUA_TNUMBER) {
        int hz = (int)lua_tonumber(L, 1);
        if (hz == 30 || hz == 60 || hz == 120) InterlockedExchange(&g_want_hz, hz);
    }
    lua_pushnumber(L, (float)(g_want_hz ? g_want_hz : g_sim_hz));
    return 1;
}
// cq1fps_showfps([on]): the "Show FPS" setting
static int l_show_fps(void *L) {
    if (lua_type(L, 1) == LUA_TBOOLEAN) {
        int on = lua_toboolean(L, 1);
        if (on != g_show_fps) g_show_fps = on, save_settings();
    }
    lua_pushboolean(L, g_show_fps);
    return 1;
}
// cq1fps_vsyncoff([on]): whether 120 FPS switched V-Sync off (kept in the settings file)
static int l_vsync_off(void *L) {
    if (lua_type(L, 1) == LUA_TBOOLEAN) {
        int on = lua_toboolean(L, 1);
        if (on != g_vsync_off) g_vsync_off = on, save_settings();
    }
    lua_pushboolean(L, g_vsync_off);
    return 1;
}
// cq1fps_fps(): frame time (ms) and frames per second, as presented
static int l_fps(void *L) {
    float ms = g_frame_ms;
    lua_pushnumber(L, ms);
    lua_pushnumber(L, ms > 0 ? 1000 / ms : 0);
    return 2;
}

// The functions the Advanced page calls (ui/advanced.as; values 0 / 1, numbers or booleans). The game
// draws one frame per simulation tick, and with V-Sync on no faster than the screen refreshes (60 Hz
// on most), so 120 FPS turns V-Sync off while it is on, and back on after.
static const char kLua[] =
    "cq64 = cq64 or {}\n"
    "local function truthy(v) return v == true or (type(v) == 'number' and v ~= 0) end\n"
    "local function vsync_for(hz)\n"
    "  if hz == 120 and app.GetCurrentVSync() ~= 0 then app.SetCurrentVSync(0) ; cq1fps_vsyncoff(true)\n"
    "  elseif hz ~= 120 and cq1fps_vsyncoff() then app.SetCurrentVSync(1) ; cq1fps_vsyncoff(false) end\n"
    "end\n"
    "local function rate(hz) return function() return cq1fps_rate() == hz and 1 or 0 end end\n"
    "local function setrate(hz) return function(on)\n"
    "  local to = truthy(on) and hz or (cq1fps_rate() == hz and 30) or cq1fps_rate()\n"
    "  cq1fps_rate(to)\n"
    "  vsync_for(to)\n"
    "end end\n"
    "cq64.GetSim60, cq64.SetSim60 = rate(60), setrate(60)\n"
    "cq64.GetSim120, cq64.SetSim120 = rate(120), setrate(120)\n"
    "cq64.GetShowFPS = function() return cq1fps_showfps() and 1 or 0 end\n"
    "cq64.SetShowFPS = function(on) cq1fps_showfps(truthy(on)) end\n"
    "cq64.GetFPS = function() local _, fps = cq1fps_fps() return fps end\n";
// (Unlike on Linux, V-Sync isn't re-applied at start for a saved 120: these functions are registered
// from a window timer message, and app.SetCurrentVSync from there hung the game (Direct3D 9). The game
// keeps the V-Sync the page set in its own options anyway.)

#ifdef CQ1FPS_TEST
// Test builds only (-DCQ1FPS_TEST): cq1fps-test.txt in the game folder, read and deleted every tick:
// "lua <chunk>" runs it and logs what it returns; "shot" saves the next frame as cq1fps-shot.bmp;
// "input <virtual-key code>" presses and releases a key (SendInput); "pad <XInput button mask> [ms]"
// holds buttons on a virtual controller 0 (0x1 up, 0x2 down, 0x4 left, 0x8 right, 0x10 Start,
// 0x1000 A, 0x2000 B), 150 ms by default; "stick <x> <y> [ms]" holds the left stick (-1..1, y up).
static volatile LONG g_shot, g_pad, g_pad_until, g_virtual, g_stick_x, g_stick_y, g_stick_until;  // g_virtual: controller 0 stays plugged in
static void test_tick(void *L) {
    char p[MAX_PATH], l[1024];
    snprintf(p, sizeof p, "%s\\cq1fps-test.txt", g_dir);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(l, sizeof l, f)) {
        l[strcspn(l, "\r\n")] = 0;
        int vk;
        if (!strcmp(l, "shot")) InterlockedExchange(&g_shot, 1);
        else if (!strncmp(l, "pad ", 4)) {
            int mask = 0, ms = 150;
            sscanf(l + 4, "%i %i", &mask, &ms);
            InterlockedExchange(&g_pad_until, (LONG)(GetTickCount() + ms));
            InterlockedExchange(&g_pad, mask);
            InterlockedExchange(&g_virtual, 1);
        } else if (!strncmp(l, "stick ", 6)) {
            float x = 0, y = 0;
            int ms = 250;
            sscanf(l + 6, "%f %f %i", &x, &y, &ms);
            InterlockedExchange(&g_stick_x, (LONG)(x * 32767));
            InterlockedExchange(&g_stick_y, (LONG)(y * 32767));
            InterlockedExchange(&g_stick_until, (LONG)(GetTickCount() + ms));
            InterlockedExchange(&g_virtual, 1);
        } else if (sscanf(l, "input %i", &vk) == 1) {  // a key press (virtual-key code), as from the keyboard
            INPUT in[2] = {{0}};
            in[0].type = in[1].type = INPUT_KEYBOARD;
            in[0].ki.wVk = in[1].ki.wVk = (WORD)vk;
            in[0].ki.wScan = in[1].ki.wScan = (WORD)MapVirtualKeyA(vk, 0);
            in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &in[0], sizeof in[0]);
            Sleep(80);
            SendInput(1, &in[1], sizeof in[1]);
        }
        else if (!strncmp(l, "lua ", 4) && L) {
            int top = lua_gettop(L), r = luaL_loadbuffer(L, l + 4, strlen(l + 4), "=test");
            if (!r) r = lua_pcall(L, 0, 1, 0);
            const char *v = lua_tolstring(L, -1, NULL);
            logf("test: %s -> %s%s", l + 4, r ? "error: " : "", v ? v : "(nil)");
            lua_settop(L, top);
        }
    }
    fclose(f);
    remove(p);
}
typedef DWORD(WINAPI *XGetState)(DWORD, void *);
static XGetState g_xinput_get_state;
static DWORD WINAPI my_xinput_get_state(DWORD user, void *state) {
    DWORD r = g_xinput_get_state(user, state);
    if (user || !state || !g_virtual) return r;
    static DWORD packet;
    if (r) memset(state, 0, 16), r = ERROR_SUCCESS;  // no real controller: a virtual one
    if ((LONG)(GetTickCount() - g_pad_until) < 0) {
        *(DWORD *)state = ++packet;
        *(WORD *)((char *)state + 4) |= (WORD)g_pad;  // XINPUT_STATE: dwPacketNumber, then wButtons
    }
    if ((LONG)(GetTickCount() - g_stick_until) < 0) {  // sThumbLX, sThumbLY at +8, +10
        *(DWORD *)state = ++packet;
        *(SHORT *)((char *)state + 8) = (SHORT)g_stick_x;
        *(SHORT *)((char *)state + 10) = (SHORT)g_stick_y;
    }
    return r;
}
#endif

// On the game thread, every timer tick: register the functions once the game's Lua is up (and again
// if it is ever made anew), and apply a rate change.
static void tick(void) {
    char *c = *(char **)g_b.console;
    void *L = c ? *(void **)(c + 0x18) : NULL;  // the console's lua_State
    if (L) {
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "cq1fps_rate");
        int have = lua_type(L, -1) == 6;  // LUA_TFUNCTION
        lua_getfield(L, LUA_GLOBALSINDEX, "app");
        int app = lua_type(L, -1) == 5;  // LUA_TTABLE: the game's bindings are in
        lua_settop(L, top);
        if (!have && app) {
            static const struct { const char *name; LuaFn fn; } kFns[] = {
                {"cq1fps_rate", l_rate}, {"cq1fps_showfps", l_show_fps}, {"cq1fps_fps", l_fps},
                {"cq1fps_vsyncoff", l_vsync_off}};
            for (size_t i = 0; i < sizeof kFns / sizeof kFns[0]; i++) {
                lua_pushcclosure(L, kFns[i].fn, 0);
                lua_setfield(L, LUA_GLOBALSINDEX, kFns[i].name);
            }
            int r = luaL_loadbuffer(L, kLua, sizeof kLua - 1, "=cq1fps");
            if (!r) r = lua_pcall(L, 0, 0, 0);
            if (r) {
                const char *e = lua_tolstring(L, -1, NULL);
                logf("Lua: %s", e ? e : "error");
            } else logf("Lua: cq64.GetSim60/SetSim60/GetSim120/SetSim120/GetShowFPS/SetShowFPS/GetFPS registered");
            lua_settop(L, top);
        }
    }
    int hz = InterlockedExchange(&g_want_hz, 0);
    if (hz && hz != g_sim_hz) set_sim_rate(hz), save_settings();
#ifdef CQ1FPS_TEST
    test_tick(L);
#endif
}

// ---- the window: a timer on the game's main thread ----------------------------------------------------
#define TIMER_ID 0xc0f5
static HWND g_wnd;
static WNDPROC g_old_proc;
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_TIMER && wp == TIMER_ID) {
        tick();
        return 0;
    }
    return CallWindowProcA(g_old_proc, w, m, wp, lp);
}

static BOOL CALLBACK find_window(HWND w, LPARAM unused) {
    DWORD pid;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) return TRUE;
    g_wnd = w;
    return FALSE;
}

// The game creates its window after startup; wait for it, then subclass it. Cq.exe 1.0.0.11 destroys
// its first window during startup and makes a new one, so attach again whenever the window is gone.
static DWORD WINAPI attach(void *unused) {
    for (;; Sleep(100)) {
        if (g_wnd && IsWindow(g_wnd)) continue;
        g_wnd = NULL;
        EnumWindows(find_window, 0);
        if (!g_wnd) continue;
        g_old_proc = (WNDPROC)SetWindowLongA(g_wnd, GWL_WNDPROC, (LONG)proc);
        SetTimer(g_wnd, TIMER_ID, 200, NULL);
        logf("attached to window %p", (void *)g_wnd);
    }
    return 0;
}

// ---- Show FPS (Direct3D 9 Present) ----------------------------------------------------------------------
typedef HRESULT(STDMETHODCALLTYPE *Present)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(STDMETHODCALLTYPE *CreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD,
                                                 D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT(STDMETHODCALLTYPE *Reset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT(WINAPI *CreateFontFn)(IDirect3DDevice9 *, INT, UINT, UINT, UINT, BOOL, DWORD, DWORD, DWORD, DWORD,
                                      LPCSTR, void **);
typedef INT(STDMETHODCALLTYPE *DrawTextFn)(void *, void *, LPCSTR, INT, LPRECT, DWORD, D3DCOLOR);
typedef HRESULT(STDMETHODCALLTYPE *FontOp)(void *);
static Present g_present;
static CreateDevice g_create_device;
static Reset g_reset;
static void *g_font;  // ID3DXFont (d3dx9_43, which the game ships): DrawTextA is slot 14, OnLostDevice 16, OnResetDevice 17
static UINT g_bb_height = 720;
#define FONT_FN(i, type) ((type)(*(void ***)g_font)[i])

static void patch_vtable(void **slot, void *fn, void **old) {
    DWORD prot;
    VirtualProtect(slot, sizeof *slot, PAGE_EXECUTE_READWRITE, &prot);
    if (*slot != fn) {
        *old = *slot;
        *slot = fn;
    }
    VirtualProtect(slot, sizeof *slot, prot, &prot);
}

// A filled, alpha-blended rectangle in screen pixels; the device's state is put back afterwards.
static void draw_box(IDirect3DDevice9 *d, int x0, int y0, int x1, int y1, D3DCOLOR c) {
    struct {
        float x, y, z, rhw;
        D3DCOLOR c;
    } v[4] = {{x0 - 0.5f, y0 - 0.5f, 0, 1, c}, {x1 - 0.5f, y0 - 0.5f, 0, 1, c},
              {x0 - 0.5f, y1 - 0.5f, 0, 1, c}, {x1 - 0.5f, y1 - 0.5f, 0, 1, c}};
    IDirect3DStateBlock9 *sb = NULL;
    if (FAILED(IDirect3DDevice9_CreateStateBlock(d, D3DSBT_ALL, &sb))) return;
    IDirect3DDevice9_SetVertexShader(d, NULL);
    IDirect3DDevice9_SetPixelShader(d, NULL);
    IDirect3DDevice9_SetTexture(d, 0, NULL);
    IDirect3DDevice9_SetFVF(d, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_COLORWRITEENABLE, 0xf);
    IDirect3DDevice9_SetRenderState(d, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(d, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(d, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetTextureStageState(d, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(d, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(d, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_DrawPrimitiveUP(d, D3DPT_TRIANGLESTRIP, 2, v, sizeof v[0]);
    IDirect3DStateBlock9_Apply(sb);
    IDirect3DStateBlock9_Release(sb);
}

static void draw_fps(IDirect3DDevice9 *d) {
    static LARGE_INTEGER freq, last;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    if (last.QuadPart) {  // average frame time
        float ms = (float)((now.QuadPart - last.QuadPart) * 1000.0 / freq.QuadPart);
        g_frame_ms = g_frame_ms * 0.9f + ms * 0.1f;
    }
    last = now;
    if (!g_show_fps) return;
    int h = (int)(g_bb_height / 40 > 14 ? g_bb_height / 40 : 14);
    if (!g_font) {
        static CreateFontFn create;
        if (!create) create = (CreateFontFn)GetProcAddress(LoadLibraryA("d3dx9_43.dll"), "D3DXCreateFontA");
        if (!create || FAILED(create(d, h, 0, 400, 1, FALSE, 0, 0, 4 /* ANTIALIASED */, 0, "Consolas", &g_font))) return;
    }
    char t[48];
    float ms = g_frame_ms;
    int n = snprintf(t, sizeof t, "Frame %.1f ms  %.0f fps", ms, ms > 0 ? 1000 / ms : 0);
    RECT size = {0, 0, 0, 0};
    if (SUCCEEDED(IDirect3DDevice9_BeginScene(d))) {
        FONT_FN(14, DrawTextFn)(g_font, NULL, t, n, &size, DT_CALCRECT, 0);
        int x = h, y = h;
        draw_box(d, x - h / 2, y - h / 4, x + size.right + h / 2, y + size.bottom + h / 4, D3DCOLOR_ARGB(153, 8, 8, 20));
        RECT shadow = {x + 1, y + 1, x + 600, y + 200}, text = {x, y, x + 600, y + 200};
        FONT_FN(14, DrawTextFn)(g_font, NULL, t, n, &shadow, DT_NOCLIP, D3DCOLOR_ARGB(204, 0, 0, 0));
        FONT_FN(14, DrawTextFn)(g_font, NULL, t, n, &text, DT_NOCLIP, D3DCOLOR_ARGB(255, 178, 255, 178));
        IDirect3DDevice9_EndScene(d);
    }
}

static HRESULT STDMETHODCALLTYPE my_reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp) {
    if (g_font) FONT_FN(16, FontOp)(g_font);
    HRESULT hr = g_reset(d, pp);
    if (pp) g_bb_height = pp->BackBufferHeight;
    if (g_font) FONT_FN(17, FontOp)(g_font);
    return hr;
}

static HRESULT STDMETHODCALLTYPE my_present(IDirect3DDevice9 *d, const RECT *a, const RECT *b, HWND w,
                                            const RGNDATA *r) {
    draw_fps(d);
#ifdef CQ1FPS_TEST
    if (InterlockedExchange(&g_shot, 0)) {
        typedef HRESULT(WINAPI * SaveSurface)(const char *, int, IDirect3DSurface9 *, const PALETTEENTRY *, const RECT *);
        SaveSurface save = (SaveSurface)GetProcAddress(LoadLibraryA("d3dx9_43.dll"), "D3DXSaveSurfaceToFileA");
        IDirect3DSurface9 *s = NULL;
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\cq1fps-shot.bmp", g_dir);
        if (save && SUCCEEDED(IDirect3DDevice9_GetBackBuffer(d, 0, 0, D3DBACKBUFFER_TYPE_MONO, &s))) {
            save(path, 0 /* D3DXIFF_BMP */, s, NULL, NULL);
            IDirect3DSurface9_Release(s);
        }
    }
#endif
    return g_present(d, a, b, w, r);
}

static HRESULT STDMETHODCALLTYPE my_create_device(IDirect3D9 *self, UINT ad, D3DDEVTYPE t, HWND w, DWORD f,
                                                  D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out) {
    HRESULT hr = g_create_device(self, ad, t, w, f, pp, out);
    if (SUCCEEDED(hr) && *out) {
        void **vt = *(void ***)*out;
        patch_vtable(&vt[17], (void *)my_present, (void **)&g_present);
        patch_vtable(&vt[16], (void *)my_reset, (void **)&g_reset);
        if (pp) g_bb_height = pp->BackBufferHeight;
    }
    return hr;
}

typedef IDirect3D9 *(WINAPI *Create9)(UINT);
static Create9 g_create9;
static IDirect3D9 *WINAPI my_create9(UINT v) {
    IDirect3D9 *d = g_create9(v);
    if (d) patch_vtable(&(*(void ***)d)[16], (void *)my_create_device, (void **)&g_create_device);
    return d;
}

// Points Cq.exe's import of <dll>!<name> (or <dll>#<ordinal>, if it imports by ordinal) at fn; the
// original goes to *old.
static void hook_import(const char *dll, const char *name, WORD ordinal, void *fn, void **old) {
    char *base = (char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp(base + d->Name, dll)) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++)
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG ? ordinal && IMAGE_ORDINAL(names->u1.Ordinal) == ordinal
                                                       : !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, name))
                patch_vtable((void **)&iat->u1.Function, fn, old);
    }
}

// ---- DirectInput8Create, passed on --------------------------------------------------------------------
typedef HRESULT(WINAPI *DI8Create)(HINSTANCE, DWORD, REFIID, void **, void *);
EXPORT HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID id, void **out, void *outer) {
    static DI8Create real;
    if (!real) {
        char p[MAX_PATH];
        GetSystemDirectoryA(p, sizeof p);
        strcat(p, "\\dinput8.dll");
        real = (DI8Create)GetProcAddress(LoadLibraryA(p), "DirectInput8Create");
    }
    return real ? real(inst, ver, id, out, outer) : E_FAIL;
}

// ---- start ------------------------------------------------------------------------------------------------
// Both Cq.exe builds are linked at 0x400000 but marked DYNAMIC_BASE with relocations, so Windows
// (ASLR) loads them elsewhere; Wine/Proton keeps 0x400000. The table's addresses are moved by the
// difference, and so are the g_pConsole operands in the check bytes (the loader relocated those too).
static void rebase(struct build *b, DWORD d) {
    DWORD *a[] = {&b->console, &b->period, &b->freq_load[0], &b->freq_load[1], &b->freq_const, &b->game_app,
                  &b->sim_manager, &b->opener_slot, &b->opener_open, &b->sysfile_ctor, &b->gfx_alloc};
    for (size_t i = 0; i < sizeof a / sizeof a[0]; i++) *a[i] += d;
    for (int i = 0; i < L_COUNT; i++) b->lua[i] += d;
    for (int i = 0; i < 3; i++) {
        b->check[i].addr += d;
        for (int j = 0; j + 4 <= b->check[i].n; j++) {
            DWORD v;
            memcpy(&v, b->check[i].bytes + j, 4);
            if (v == b->console - d) {
                v = b->console;
                memcpy(b->check[i].bytes + j, &v, 4);
            }
        }
    }
}

static int right_exe(void) {
    char *base = (char *)GetModuleHandleA(NULL);
    DWORD slide = (DWORD)base - 0x400000;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD end = (DWORD)base + nt->OptionalHeader.SizeOfImage;
    for (size_t i = 0; i < sizeof kBuilds / sizeof kBuilds[0]; i++) {
        struct build r = kBuilds[i];
        rebase(&r, slide);
        int ok = 1;
        for (int j = 0; j < 3 && ok; j++)
            ok = r.check[j].addr + 8 <= end && !memcmp((void *)r.check[j].addr, r.check[j].bytes, r.check[j].n);
        if (ok) {
            g_b = r;
            logf("game: %s (loaded at %p)", g_b.name, base);
            return 1;
        }
    }
    logf("not a known Cq.exe build (loaded at %p): CQ1-FPS off", base);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *unused) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    GetModuleFileNameA(NULL, g_dir, sizeof g_dir);
    *strrchr(g_dir, '\\') = 0;
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\cq1fps.log", g_dir);
    g_log = fopen(p, "w");
    if (!right_exe()) return TRUE;  // DirectInput still works; CQ1-FPS stays off
    install();
    if (!g_ok) return TRUE;
    hook_import("d3d9.dll", "Direct3DCreate9", 0, (void *)my_create9, (void **)&g_create9);
#ifdef CQ1FPS_TEST  // 1.0.0.11 imports XInputGetState by name, the Steam build from XINPUT1_3 by ordinal 2
    hook_import("XINPUT9_1_0.dll", "XInputGetState", 0, (void *)my_xinput_get_state, (void **)&g_xinput_get_state);
    hook_import("XINPUT1_3.dll", "XInputGetState", 2, (void *)my_xinput_get_state, (void **)&g_xinput_get_state);
#endif
    CreateThread(NULL, 0, attach, NULL, 0, NULL);
    logf("CQ1-FPS: simulation at %d Hz, Show FPS %s (Help & Options > Settings > Advanced)", g_sim_hz,
         g_show_fps ? "on" : "off");
    return TRUE;
}
