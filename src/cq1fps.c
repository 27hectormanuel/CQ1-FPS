// SPDX-License-Identifier: CC0-1.0
// CQ1-FPS: 60 and 120 FPS for the official Linux release of Costume Quest (GOG 2.0.0.3, Cq.bin.x86,
// SHA-256 7104dc7d...5100ef), loaded with LD_PRELOAD (see run.sh).
//
// Costume Quest's world advances in fixed simulation ticks of 1/30 s and the game draws one frame
// per tick, so it runs at 30 fps however fast the machine is. Costume Quest 2 (same engine) added a
// "60 FPS" option (GameApp::SetSim60Hz); this does the same at 60 or 120 Hz, and adds three
// checkboxes to Help & Options > Settings > Advanced: "60 FPS", "120 FPS" and "Show FPS" (frame
// time and frames per second, top left). Settings are kept in cq1fps-settings.txt in the game folder.
//
// Built without the system's C headers (SteamOS has none for i386): the few libc, SDL and GL
// declarations it needs are below.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "font.h"

// ---- libc / libdl --------------------------------------------------------------------------------------
typedef struct FILE FILE;
FILE *fopen(const char *, const char *);
int fclose(FILE *);
int fputs(const char *, FILE *);
int fflush(FILE *);
char *fgets(char *, int, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
size_t fread(void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int sscanf(const char *, const char *, ...);
char *getenv(const char *);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);
long readlink(const char *, char *, size_t);
char *getcwd(char *, size_t);
int mprotect(void *, size_t, int);
void *malloc(size_t);
void free(void *);
void *dlsym(void *, const char *);
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1l)

// ---- SDL2 and desktop GL (the parts used) --------------------------------------------------------------
static int (*sdl_poll)(void *);
static void (*sdl_swap)(void *);
static uint32_t (*sdl_ticks)(void);
static void (*sdl_drawable_size)(void *, int *, int *);

typedef unsigned int GLenum, GLuint;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef double GLdouble;
#define GL_TEXTURE_2D 0x0DE1
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_QUADS 0x0007
#define GL_ALPHA 0x1906
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_TEXTURE 0x1702
#define GL_ALL_ATTRIB_BITS 0x000fffff
#define GL_CLIENT_ALL_ATTRIB_BITS 0xffffffff
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_ARRAY_BUFFER 0x8892
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE0 0x84C0
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_SCISSOR_TEST 0x0C11
#define GL_STENCIL_TEST 0x0B90
#define GL_ALPHA_TEST 0x0BC0
#define GL_LIGHTING 0x0B50
#define GL_FOG 0x0B60
static struct {
    void (*GetIntegerv)(GLenum, GLint *);
    void (*PushAttrib)(GLuint);
    void (*PopAttrib)(void);
    void (*PushClientAttrib)(GLuint);
    void (*PopClientAttrib)(void);
    void (*MatrixMode)(GLenum);
    void (*PushMatrix)(void);
    void (*PopMatrix)(void);
    void (*LoadIdentity)(void);
    void (*Ortho)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFunc)(GLenum, GLenum);
    void (*Begin)(GLenum);
    void (*End)(void);
    void (*Vertex2f)(GLfloat, GLfloat);
    void (*TexCoord2f)(GLfloat, GLfloat);
    void (*Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*GenTextures)(GLsizei, GLuint *);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*PixelStorei)(GLenum, GLint);
    void (*UseProgram)(GLuint);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*BindBuffer)(GLenum, GLuint);
    void (*ActiveTexture)(GLenum);
} gl;

// ---- log ------------------------------------------------------------------------------------------------
static FILE *g_log;
static void logf(const char *fmt, ...) {
    char b[600];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (g_log) fputs(b, g_log), fputs("\n", g_log), fflush(g_log);
}

#include "pausepatch.h"

// ---- the game (Cq.bin.x86, GOG 2.0.0.3; checked in init) ---------------------------------------------------
// Everything that holds the 30 Hz rate:
#define SIM_PERIOD_CONST 0x08eedc40      // float 1/30 in .rodata, read only by GameApp::Init and
                                         // GameApp::_RequestRender (render pacing)
#define SIMWORLD_CTOR_PERIOD 0x0872897f  // SimWorld::SimWorld: movl $<1/30>, 0xa3c(%ebx)
#define SIMWORLD_CTOR_FREQ 0x087289ac    //                     movl $<30.0>, 0xa40(%ebx)
// SimWorld::_UpdateTimeScale and its inlined copy in SimWorld::OnTick recompute a world's period
// and frequency (for slow motion) from 1/30 and 30.0 constants shared with unrelated code; their
// "flds <constant>" instructions are pointed at g_sim_period / g_sim_freq instead.
static const uint32_t kPeriodLoads[2] = {0x087284de, 0x0872a98d}, kFreqLoads[2] = {0x087284f3, 0x0872a9a2};
#define G_SIM_MANAGER ((char **)0x09188e04)  // g_pSimManager: +0xc points to the 4 world slots
#define W_DIRTY 0xa54                        // SimWorld: recompute period/frequency on the next tick
#define G_GAME_APP ((char **)0x0917ea40)     // sg_pCurrentGameApp: +0xd8 = powf(0.1, period)
#define APP_SMOOTHING 0xd8
// The Advanced page's movie is opened through FlashFileOpener::OpenFile (vtable slot 2); the stock
// GFxFileOpener::OpenFile opens a file from disk.
#define FLASH_OPENER_SLOT 0x08ed2230
typedef void *(*OpenFile)(void *self, const char *path);
static const OpenFile flash_open_file = (OpenFile)0x08550230, gfx_open_file = (OpenFile)0x08ce52d0;
// The game's Lua 5.1 (lua_Number is float), reached through the console (g_pConsole + 0x18).
#define G_CONSOLE ((char **)0x0917eab0)
#define LUA_GLOBALSINDEX (-10002)
typedef int (*LuaFn)(void *L);
static int (*const lua_loadbuffer)(void *, const char *, size_t, const char *) = (void *)0x08dea8d0;
static int (*const lua_pcall)(void *, int, int, int) = (void *)0x08df3be0;
static int (*const lua_gettop)(void *) = (void *)0x08df1000;
static void (*const lua_settop)(void *, int) = (void *)0x08df1010;
static void (*const lua_getfield)(void *, int, const char *) = (void *)0x08df2dc0;
static void (*const lua_setfield)(void *, int, const char *) = (void *)0x08df34d0;
static int (*const lua_type)(void *, int) = (void *)0x08df14e0;
static void (*const lua_pushcclosure)(void *, LuaFn, int) = (void *)0x08df2bc0;
static void (*const lua_pushnumber)(void *, float) = (void *)0x08df2a00;
static void (*const lua_pushboolean)(void *, int) = (void *)0x08df2c60;
static float (*const lua_tonumber)(void *, int) = (void *)0x08df1ee0;
static int (*const lua_toboolean)(void *, int) = (void *)0x08df20e0;
static const char *(*const lua_tolstring)(void *, int, size_t *) = (void *)0x08df21f0;
#define LUA_TBOOLEAN 1
#define LUA_TNUMBER 3
static const struct {
    uint32_t addr;
    uint8_t bytes[8];
} kCheck[] = {
    {0x0872897f, {0xc7, 0x83, 0x3c, 0x0a, 0x00, 0x00, 0x89, 0x88}},  // SimWorld::SimWorld: period
    {0x087289ac, {0xc7, 0x83, 0x40, 0x0a, 0x00, 0x00, 0x00, 0x00}},  //                    frequency
    {0x087284de, {0xd9, 0x05, 0x9c, 0x17, 0xed, 0x08, 0x00, 0x00}},  // _UpdateTimeScale: 1/30 (6 bytes)
    {0x087284f3, {0xd9, 0x05, 0x68, 0xe0, 0xec, 0x08, 0x00, 0x00}},  //                   30.0 (6 bytes)
    {0x0872a98d, {0xd9, 0x05, 0x9c, 0x17, 0xed, 0x08, 0x00, 0x00}},  // OnTick: 1/30 (6 bytes)
    {0x0872a9a2, {0xd9, 0x05, 0x68, 0xe0, 0xec, 0x08, 0x00, 0x00}},  //         30.0 (6 bytes)
    {0x08df2a00, {0x8b, 0x44, 0x24, 0x04, 0xd9, 0x44, 0x24, 0x08}},  // lua_pushnumber (float)
};
static const uint8_t kCheckLen[] = {8, 8, 6, 6, 6, 6, 8};

// Other libraries (the cq64 debug console) look this up and leave the frame rate to this one.
__attribute__((visibility("default"))) int cq1fps_present = 1;

// ---- state ----------------------------------------------------------------------------------------------
static int g_ok;
static float g_sim_period = 1.0f / 30, g_sim_freq = 30;
static int g_sim_hz = 30, g_show_fps, g_vsync_off;  // g_vsync_off: 120 FPS switched V-Sync off
// cq64.SetSim60 / SetSim120 only record the wish; the next poll applies it, outside whatever Lua is
// doing (the in-game pause movie is warmed up while a level loads, which presses each checkbox).
static volatile int g_want_hz;
static char g_pause_path[600];
static int g_pause_ready;
static volatile float g_frame_ms;  // average frame time (render thread)

static void poke(uint32_t addr, const void *src, size_t n, int prot_after) {
    uintptr_t page = addr & ~0xfffu;
    size_t len = addr + n - page;
    mprotect((void *)page, len, 7);
    memcpy((void *)(uintptr_t)addr, src, n);
    mprotect((void *)page, len, prot_after);
}

// cq1fps-settings.txt: "simrate 30|60|120", "showfps 0|1", "vsyncoff 0|1" (120 FPS switched V-Sync off, so it
// comes back on after; the game saves V-Sync in its own options meanwhile).
static void save_settings(void) {
    char s[64];
    FILE *f = fopen("cq1fps-settings.txt", "w");
    if (!f) return;
    snprintf(s, sizeof s, "simrate %d\nshowfps %d\nvsyncoff %d\n", g_sim_hz, g_show_fps, g_vsync_off);
    fputs(s, f);
    fclose(f);
}

static int load_settings(void) {  // the saved rate
    char l[64];
    int v, hz = 30;
    FILE *f = fopen("cq1fps-settings.txt", "r");
    if (!f) return hz;
    while (fgets(l, sizeof l, f)) {
        if (sscanf(l, "simrate %d", &v) == 1 && (v == 30 || v == 60 || v == 120)) hz = v;
        if (sscanf(l, "showfps %d", &v) == 1) g_show_fps = v != 0;
        if (sscanf(l, "vsyncoff %d", &v) == 1) g_vsync_off = v != 0;
    }
    fclose(f);
    return hz;
}

// ---- the simulation rate ---------------------------------------------------------------------------------
// Switches the simulation to 30, 60 or 120 Hz, at start or while playing.
static void set_sim_rate(int hz) {
    float period = 1.0f / hz, freq = (float)hz;
    float smoothing = hz == 120 ? 0.980994713f : hz == 60 ? 0.962350607f : 0.926118731f;  // powf(0.1, period)
    g_sim_period = period, g_sim_freq = freq;
    poke(SIM_PERIOD_CONST, &period, 4, 1);
    poke(SIMWORLD_CTOR_PERIOD + 6, &period, 4, 5);  // for worlds made from now on
    poke(SIMWORLD_CTOR_FREQ + 6, &freq, 4, 5);
    if (*G_GAME_APP) *(float *)(*G_GAME_APP + APP_SMOOTHING) = smoothing;
    char **worlds = *G_SIM_MANAGER ? *(char ***)(*G_SIM_MANAGER + 0xc) : 0;
    for (int i = 0; worlds && i < 4; i++)
        if (worlds[i]) worlds[i][W_DIRTY] = 1;  // existing worlds recompute on their next tick
    g_sim_hz = hz;
    logf("simulation at %d Hz", hz);
}

static void *open_file(void *self, const char *p) {
    size_t n = strlen(p);
    if (g_pause_ready && n >= 19) {
        char tail[20];
        for (int i = 0; i < 19; i++) tail[i] = (char)(p[n - 19 + i] | (p[n - 19 + i] >= 'A' && p[n - 19 + i] <= 'Z' ? 32 : 0));
        tail[19] = 0;
        if (!strcmp(tail, "pause/opt/pause.gfx")) return gfx_open_file(self, g_pause_path);
    }
    return flash_open_file(self, p);
}

// ---- Lua ------------------------------------------------------------------------------------------------
// cq1fps_rate([hz]): the simulation rate (the one asked for, if not applied yet); 30, 60 or 120 asks
// for it (applied on the next poll)
static int l_rate(void *L) {
    if (lua_type(L, 1) == LUA_TNUMBER) {
        int hz = (int)lua_tonumber(L, 1);
        if (hz == 30 || hz == 60 || hz == 120) g_want_hz = hz;
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
// cq1fps_fps(): frame time (ms) and frames per second, as drawn
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
    "cq64.GetFPS = function() local _, fps = cq1fps_fps() return fps end\n"
    "vsync_for(cq1fps_rate())  -- 120 saved from last time\n";

// Registers the functions once the game's Lua is up (and again if it is ever made anew).
static void register_lua(void) {
    char *c = *G_CONSOLE;
    void *L = c ? *(void **)(c + 0x18) : 0;
    if (!L) return;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_GLOBALSINDEX, "cq1fps_rate");
    int have = lua_type(L, -1) == 6;  // LUA_TFUNCTION
    lua_getfield(L, LUA_GLOBALSINDEX, "app");
    int app = lua_type(L, -1) == 5;  // LUA_TTABLE: the game's bindings are in
    lua_settop(L, top);
    if (have || !app) return;
    static const struct { const char *name; LuaFn fn; } kFns[] = {
        {"cq1fps_rate", l_rate}, {"cq1fps_showfps", l_show_fps}, {"cq1fps_fps", l_fps}, {"cq1fps_vsyncoff", l_vsync_off}};
    for (size_t i = 0; i < sizeof kFns / sizeof kFns[0]; i++) {
        lua_pushcclosure(L, kFns[i].fn, 0);
        lua_setfield(L, LUA_GLOBALSINDEX, kFns[i].name);
    }
    int r = lua_loadbuffer(L, kLua, sizeof kLua - 1, "=cq1fps");
    if (!r) r = lua_pcall(L, 0, 0, 0);
    if (r) {
        const char *e = lua_tolstring(L, -1, 0);
        logf("Lua: %s", e ? e : "error");
    } else logf("Lua: cq64.GetSim60/SetSim60/GetSim120/SetSim120/GetShowFPS/SetShowFPS/GetFPS registered");
    lua_settop(L, top);
}

// The game calls SDL_PollEvent from its main loop (the game thread): the rate is switched here.
int SDL_PollEvent(void *e) {
    if (g_ok) {
        static uint32_t last;
        uint32_t t = sdl_ticks();
        if (t - last >= 200) {
            last = t;
            register_lua();
            int hz = g_want_hz;
            if (hz) {
                g_want_hz = 0;
                if (hz != g_sim_hz) set_sim_rate(hz), save_settings();
            }
        }
    }
    return sdl_poll(e);
}

// ---- Show FPS (drawn over the frame, render thread) ------------------------------------------------------
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
    gl.TexCoord2f(u0, v0), gl.Vertex2f(x0, y0);
    gl.TexCoord2f(u1, v0), gl.Vertex2f(x1, y0);
    gl.TexCoord2f(u1, v1), gl.Vertex2f(x1, y1);
    gl.TexCoord2f(u0, v1), gl.Vertex2f(x0, y1);
}

#define ATLAS_W (FONT_W * FONT_COUNT + 1)  // one solid column at the end, for the box
static void text(float x, float y, float s, const char *t) {
    for (int i = 0; t[i]; i++, x += FONT_W * s) {
        int c = (unsigned char)t[i] - FONT_FIRST;
        if (c <= 0 || c >= FONT_COUNT) continue;
        quad(x, y, x + FONT_W * s, y + FONT_H * s, (float)(c * FONT_W) / ATLAS_W, 0, (float)((c + 1) * FONT_W) / ATLAS_W, 1);
    }
}

static void rect(float x0, float y0, float x1, float y1) {
    float u = (ATLAS_W - 0.5f) / ATLAS_W;
    quad(x0, y0, x1, y1, u, 0, u, 1);
}

static void draw_fps(int w, int h) {
    char t[48];
    float ms = g_frame_ms;
    snprintf(t, sizeof t, "Frame %.1f ms  %.0f fps", ms, ms > 0 ? 1000 / ms : 0);
    static GLuint tex;
    GLint program = 0, fb = 0, buf = 0, active = 0, unpack = 4;
    if (gl.UseProgram) gl.GetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (gl.BindFramebuffer) gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    if (gl.BindBuffer) gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf);
    if (gl.ActiveTexture) gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0);
    gl.PushAttrib(GL_ALL_ATTRIB_BITS);
    gl.PushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    gl.MatrixMode(GL_TEXTURE), gl.PushMatrix(), gl.LoadIdentity();
    gl.MatrixMode(GL_PROJECTION), gl.PushMatrix(), gl.LoadIdentity();
    gl.Ortho(0, w, h, 0, -1, 1);
    gl.MatrixMode(GL_MODELVIEW), gl.PushMatrix(), gl.LoadIdentity();
    if (gl.UseProgram) gl.UseProgram(0);
    if (gl.BindFramebuffer) gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (gl.BindBuffer) gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    gl.Viewport(0, 0, w, h);
    gl.Disable(GL_DEPTH_TEST), gl.Disable(GL_CULL_FACE), gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_STENCIL_TEST), gl.Disable(GL_ALPHA_TEST), gl.Disable(GL_LIGHTING), gl.Disable(GL_FOG);
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.Enable(GL_TEXTURE_2D);
    if (!tex) {
        static unsigned char atlas[ATLAS_W * FONT_H];
        for (int y = 0; y < FONT_H; y++) {
            memcpy(atlas + y * ATLAS_W, kFont + y * FONT_W * FONT_COUNT, FONT_W * FONT_COUNT);
            atlas[y * ATLAS_W + ATLAS_W - 1] = 255;
        }
        gl.GenTextures(1, &tex);
        gl.BindTexture(GL_TEXTURE_2D, tex);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_W, FONT_H, 0, GL_ALPHA, GL_UNSIGNED_BYTE, atlas);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    gl.BindTexture(GL_TEXTURE_2D, tex);
    float s = h >= 1000 ? 2.0f : 1.0f, x = 12 * s, y = 12 * s;
    gl.Begin(GL_QUADS);
    gl.Color4f(0.03f, 0.03f, 0.08f, 0.6f);
    rect(x - 6 * s, y - 4 * s, x + strlen(t) * FONT_W * s + 6 * s, y + FONT_H * s + 4 * s);
    gl.Color4f(0, 0, 0, 0.8f);
    text(x + s, y + s, s, t);
    gl.Color4f(0.7f, 1.0f, 0.7f, 1);
    text(x, y, s, t);
    gl.End();
    gl.MatrixMode(GL_MODELVIEW), gl.PopMatrix();
    gl.MatrixMode(GL_PROJECTION), gl.PopMatrix();
    gl.MatrixMode(GL_TEXTURE), gl.PopMatrix();
    gl.PopClientAttrib();
    gl.PopAttrib();
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    if (gl.ActiveTexture) gl.ActiveTexture(active);
    if (gl.BindBuffer) gl.BindBuffer(GL_ARRAY_BUFFER, buf);
    if (gl.BindFramebuffer) gl.BindFramebuffer(GL_FRAMEBUFFER, fb);
    if (gl.UseProgram) gl.UseProgram(program);
}

void SDL_GL_SwapWindow(void *window) {
    if (g_ok) {
        static uint32_t last;
        uint32_t now = sdl_ticks();
        if (last) g_frame_ms = g_frame_ms * 0.9f + (float)(now - last) * 0.1f;  // average frame time
        last = now;
        int w = 0, h = 0;
        if (g_show_fps) sdl_drawable_size(window, &w, &h);
        if (w > 0 && h > 0) draw_fps(w, h);
    }
    sdl_swap(window);
}

// ---- start ------------------------------------------------------------------------------------------------
static void *gl_proc(const char *name) {
    static void *(*get)(const char *);
    if (!get) get = (void *(*)(const char *))dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    void *p = dlsym(RTLD_DEFAULT, name);
    return p ? p : get ? get(name) : 0;
}

__attribute__((constructor)) static void init(void) {
    sdl_poll = (int (*)(void *))dlsym(RTLD_NEXT, "SDL_PollEvent");
    sdl_swap = (void (*)(void *))dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    if (!sdl_poll || !sdl_swap) return;  // not a program that uses SDL2: stay out of the way
    char exe[512] = "";
    long n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n < 10) return;
    exe[n] = 0;
    if (strcmp(exe + n - 10, "Cq.bin.x86")) return;  // only the game, not its launcher
    g_log = fopen("cq1fps.log", "w");
    for (size_t i = 0; i < sizeof kCheck / sizeof kCheck[0]; i++)
        if (memcmp((void *)(uintptr_t)kCheck[i].addr, kCheck[i].bytes, kCheckLen[i]) ||
            *(float *)SIM_PERIOD_CONST != 1.0f / 30) {
            logf("unexpected code at 0x%x: not the GOG 2.0.0.3 Cq.bin.x86; CQ1-FPS off", kCheck[i].addr);
            return;
        }
    sdl_ticks = (uint32_t(*)(void))dlsym(RTLD_NEXT, "SDL_GetTicks");
    sdl_drawable_size = (void (*)(void *, int *, int *))dlsym(RTLD_NEXT, "SDL_GL_GetDrawableSize");
    if (!sdl_drawable_size)  // the game ships SDL 2.0.0, which predates it
        sdl_drawable_size = (void (*)(void *, int *, int *))dlsym(RTLD_NEXT, "SDL_GetWindowSize");
#define GL(name) *(void **)&gl.name = gl_proc("gl" #name)
    GL(GetIntegerv), GL(PushAttrib), GL(PopAttrib), GL(PushClientAttrib), GL(PopClientAttrib), GL(MatrixMode);
    GL(PushMatrix), GL(PopMatrix), GL(LoadIdentity), GL(Ortho), GL(Enable), GL(Disable), GL(BlendFunc);
    GL(Begin), GL(End), GL(Vertex2f), GL(TexCoord2f), GL(Color4f), GL(GenTextures), GL(BindTexture);
    GL(TexImage2D), GL(TexParameteri), GL(Viewport), GL(PixelStorei), GL(UseProgram), GL(BindFramebuffer);
    GL(BindBuffer), GL(ActiveTexture);
    if (!sdl_ticks || !sdl_drawable_size || !gl.Begin || !gl.GetIntegerv) {
        logf("SDL or GL functions missing; CQ1-FPS off");
        return;
    }
    for (int i = 0; i < 2; i++) {
        uint32_t a = (uint32_t)(uintptr_t)&g_sim_period, b = (uint32_t)(uintptr_t)&g_sim_freq;
        poke(kPeriodLoads[i] + 2, &a, 4, 5);
        poke(kFreqLoads[i] + 2, &b, 4, 5);
    }
    int hz = load_settings();
    if (hz != 30) set_sim_rate(hz);
    // The Advanced page's additions (ui/advanced.as), in a copy of the player's own movie.
    char cwd[512];  // the game folder (the game runs from it); the movie loader wants a full path
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    snprintf(g_pause_path, sizeof g_pause_path, "%s/cq1fps-pause.gfx", cwd);
    if (*(uint32_t *)FLASH_OPENER_SLOT == (uint32_t)(uintptr_t)flash_open_file &&
        pp_build_pause("Linux/Packs/Man_Gfx.~h", g_pause_path)) {
        uint32_t fn = (uint32_t)(uintptr_t)open_file;
        poke(FLASH_OPENER_SLOT, &fn, 4, 1);
        g_pause_ready = 1;
    }
    g_ok = 1;
    logf("CQ1-FPS: simulation at %d Hz, Show FPS %s (Help & Options > Settings > Advanced)", g_sim_hz,
         g_show_fps ? "on" : "off");
}
