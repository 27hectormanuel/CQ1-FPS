# CQ1-FPS

**60 and 120 FPS for Costume Quest (the official Linux release).**

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

## Requirements

- **Costume Quest for Linux, GOG version 2.0.0.3** (`game/Cq.bin.x86`, SHA-256
  `7104dc7db2e5a9bc30cfebbacb34656072a8aed4d5b86515208ba93e3d5100ef`). Tested on a Steam Deck
  (SteamOS). Another build of the game is detected and CQ1-FPS turns itself off (see
  `cq1fps.log` in the game folder); the Steam Linux build hasn't been tried.
- The game is 32-bit and needs 32-bit libraries, including libGLU. `run.sh` uses the Steam
  runtime's when Steam is installed.

## Use

Download `cq1fps.so` and `run.sh` from the [releases](../../releases) (or build them, below), keep
them together, and start the game with:

```sh
./run.sh "<GOG install>/Costume Quest/game" -window      # any game options after the folder
```

or, for a launcher that runs `Cq.bin.x86` itself (from the `game` folder):
`LD_PRELOAD=/path/to/cq1fps.so ./Cq.bin.x86`.

Then open the pause menu (Esc) › Help & Options › Settings › Advanced.

The same switches work from Lua (for example from a debug console): `cq64.SetSim60(1)` / `(0)`,
`cq64.SetSim120(1)` / `(0)`, `cq64.SetShowFPS(1)` / `(0)`, their `Get…` versions, and
`cq64.GetFPS()`.

## How it works

The 30 Hz rate is held in a few places, all changed to the chosen rate (addresses are for
2.0.0.3 and are checked at start):

- `SimWorld::SimWorld` builds each world's tick period and frequency from immediates (1/30, 30.0);
- `SimWorld::_UpdateTimeScale` and its inlined copy in `SimWorld::OnTick` recompute them (for slow
  motion) from constants shared with unrelated code, so those two loads are pointed at the
  library's own values instead;
- a 1/30 constant read by `GameApp::Init` and `GameApp::_RequestRender` paces the drawing, and
  `GameApp`'s frame smoothing factor is `powf(0.1, period)`;
- worlds that already exist are marked to recompute on their next tick.

The checkboxes are ActionScript (`ui/advanced.as`, compiled with JPEXS FFDec by `ui/build.sh` into
`src/advanced_action.h`) inserted at the end of frame 1 of the game's `pause.gfx`; the library
serves that movie through the game's own Flash file opener. They call the `cq64` Lua functions
the library registers.

## Build

```sh
CLANG=<clang with the x86 target and ld.lld> ./build.sh      # -> out/cq1fps.so, out/run.sh
```

Needs no 32-bit C headers, only the system's 32-bit `libc.so.6` (`LIB32`, default `/usr/lib32`),
Python 3, ffmpeg (renders the counter's font from DejaVu Sans Mono, `FONT=<ttf>` for another) and
`fc-match`. After changing `ui/advanced.as`: `FFDEC=<ffdec.jar> ui/build.sh` (needs Java).

## Notes

- Loading a level while the game's pause menu is open crashes the game (in
  `SessionManagerCallbackImpl::OnUnpause`), with or without CQ1-FPS; close the menu first if a
  mod or console loads levels.
- Automated test bots (the developers' debug menu "Tests") press menu buttons like a player and can
  change these checkboxes.

## License

CC0 1.0 (public domain); see [LICENSE](LICENSE). Costume Quest belongs to Double Fine; this
project ships none of its files. The counter's font is rendered from DejaVu Sans Mono (Bitstream
Vera license).
