# CQ1-FPS

**60 and 120 FPS for Costume Quest: the official Windows (Steam and non-Steam) and Linux releases.**

Costume Quest's world advances in fixed ticks of 1/30 s, and the game draws one frame per tick,
so it runs at 30 fps however fast your machine is. Costume Quest 2 (same engine) added a "60 FPS"
option; CQ1-FPS does the same for the first game, at 60 or 120 ticks a second. Movement,
animation, the camera and battles all get smoother; game time stays real-time.

It adds three checkboxes to **Help & Options › Settings › Advanced**, left of Done:

| | |
|---|---|
| **60 FPS** | the world runs at 60 ticks a second |
| **120 FPS** | 120 ticks a second. The game draws one frame per tick, and with V-Sync on it can't draw faster than your screen refreshes, so **120 FPS switches V-Sync off** while it is on and back on when you leave it. On a 60 Hz screen you still only see 60 of those frames (with some tearing); use a 120 Hz or faster display to see them all. |
| **Show FPS** | frame time and frames per second, top left |

Checking one rate unchecks the other; neither is the original 30. It switches at once, while
playing, and is remembered (`cq1fps-settings.txt` in the game folder).

No game files are included or changed: the Advanced page is built at start from your own copy
of the game's menu (`Linux/Packs/Man_Gfx`) into `cq1fps-pause.gfx` in the game folder, and the
frame rate is patched in memory.

## Supported games

| | Game | File |
|---|---|---|
| **Windows, Steam** | `Cq.exe`, current Steam build (SHA-256 `d9e1ee3e7e78ae80…`) | `dinput8.dll` |
| **Windows, non-Steam** | `Cq.exe` 1.0.0.11, the retail / GOG Windows release (SHA-256 `9d5acea0f5da7686…`) | `dinput8.dll` |
| **Linux** | GOG 2.0.0.3, `game/Cq.bin.x86` (SHA-256 `7104dc7db2e5a9bc…`) | `cq1fps.so` + `run.sh` |

Another build of the game is detected and CQ1-FPS turns itself off (see `cq1fps.log` in the game
folder). The Windows DLL works when Windows loads the game at a random address (ASLR) and under
Wine / Proton.

## Install

Download the zip from the [releases](../../releases).

**Windows (Steam or not):** copy `windows/dinput8.dll` into the game folder, next to `Cq.exe`
(Steam: right-click Costume Quest › Manage › Browse local files). The game loads it by itself.
To remove it, delete `dinput8.dll`. It replaces any other `dinput8.dll` mod in that folder. Needs
the Universal C Runtime, which Windows 10 and later have.

**Steam on Linux / Steam Deck (the Windows version through Proton):** as above, and add
`WINEDLLOVERRIDES="dinput8=n,b" %command%` to the game's launch options so Proton uses it.

**Linux (GOG):** keep `linux/cq1fps.so` and `linux/run.sh` together and start the game with

```sh
./run.sh "<GOG install>/Costume Quest/game" -window      # any game options after the folder
```

or, from the `game` folder, `LD_PRELOAD=/path/to/cq1fps.so ./Cq.bin.x86`. The game is 32-bit and
needs 32-bit libraries, including libGLU; `run.sh` uses the Steam runtime's when Steam is installed.

Then: Help & Options › Settings › Advanced (from the title screen or the pause menu).

The same switches work from Lua (for example from a debug console): `cq64.SetSim60(1)` / `(0)`,
`cq64.SetSim120(1)` / `(0)`, `cq64.SetShowFPS(1)` / `(0)`, their `Get…` versions, and
`cq64.GetFPS()`.

## Tested

Steam Deck (SteamOS), 1280×720, 2026-10-23, all through the Advanced page and in game:

| | 30 Hz | 60 Hz | 120 Hz (V-Sync off) |
|---|---|---|---|
| Linux GOG 2.0.0.3 | 33.3 ms (30 fps) | 16.5 ms (60 fps) | 8.3–9.5 ms (~110–120 fps) |
| Windows 1.0.0.11 (Proton 10) | 33.3 ms (30 fps) | 17.4 ms (~58 fps) | 8.4 ms (118 fps) |
| Windows Steam build (Proton 10) | | 16.7 ms (60 fps) | 8.5 ms (118 fps) |

Not tried on a Windows PC itself.

## How it works

The 30 Hz rate is held in a few places, all changed to the chosen rate (addresses are checked at
start):

- Linux: `SimWorld::SimWorld` builds each world's tick period and frequency from immediates (1/30,
  30.0); `SimWorld::_UpdateTimeScale` and its inlined copy in `SimWorld::OnTick` recompute them (for
  slow motion) from constants shared with unrelated code, so those loads are pointed at the
  library's own values; a 1/30 constant read by `GameApp::Init` and `GameApp::_RequestRender`
  paces the drawing.
- Windows: the period is a variable (read by the same functions), and the two loads of the 30.0
  frequency are pointed at the library's value.
- Both: `GameApp`'s frame smoothing factor is `powf(0.1, period)`, and worlds that already exist
  are marked to recompute on their next tick.

The checkboxes are ActionScript (`ui/advanced.as`, compiled with JPEXS FFDec by `ui/build.sh` into
`src/advanced_action.h`) inserted at the end of frame 1 of the game's `pause.gfx`
(`src/pausepatch.h`); the library serves that movie through the game's own Flash file opener. They
call the `cq64` Lua functions the library registers. On Windows the library is `dinput8.dll`
(passing `DirectInput8Create` on to the system's), runs its work from a timer on the game window's
thread, and draws Show FPS with Direct3D 9 (`Present`) and the game's D3DX.

## Build

```sh
CLANG=<clang with the x86 target and ld.lld> ./build.sh          # Linux: out/cq1fps.so, out/run.sh
MINGW=<i686-w64-mingw32-clang> ./build-windows.sh                 # Windows: out/dinput8.dll
```

Linux needs no 32-bit C headers, only the system's 32-bit `libc.so.6` (`LIB32`, default
`/usr/lib32`), Python 3, ffmpeg (renders the counter's font from DejaVu Sans Mono, `FONT=<ttf>` for
another) and `fc-match`. Windows needs any i686 MinGW compiler (llvm-mingw works); `TEST=1` makes a
test build. After changing `ui/advanced.as`: `FFDEC=<ffdec.jar> ui/build.sh` (needs Java).

## Notes

- Loading a level while the game's pause menu is open crashes the game (in
  `SessionManagerCallbackImpl::OnUnpause`), with or without CQ1-FPS; close the menu first if a
  mod or console loads levels.
- Automated test bots (the developers' debug menu "Tests") press menu buttons like a player and can
  change these checkboxes.
- After a restart at 120, V-Sync stays as the game saved it (Off). On Windows it isn't switched again
  at start: doing that from the library's timer hung the game (Direct3D 9); the page itself is fine.

## License

CC0 1.0 (public domain); see [LICENSE](LICENSE). Costume Quest belongs to Double Fine; this
project ships none of its files. The Linux counter's font is rendered from DejaVu Sans Mono
(Bitstream Vera license).
