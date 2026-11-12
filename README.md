# SWTFU 60 FPS runtime fixes

Runtime fixes for the Steam PC builds of *Star Wars: The Force Unleashed* (TFU1)
and *The Force Unleashed II* (TFU2): 60 FPS without speeding the game up.
TFU1 also gets borderless fullscreen, controller reconnect and skippable launch movies.

| Game | Status |
|---|---|
| TFU1 (`SWTFU.exe`, SHA-256 `1c156705…92032`) | 60 FPS profile, borderless, controller: done. |
| TFU2 (`SWTFU2.exe`, SHA-256 `79874d83…65167`) | 60 FPS profile **experimental**: cap, cutscenes, DMM, cloth, physics and camera smoothing, trails, Force-grip integration and the mash minigame. Borderless works (window only). A few leads are still open. |

Nothing on disk is modified. `SWTFU-Helper.exe` recognises whichever game is running
(process name, executable hash and code signature) and patches the **running** game's memory.
Restarting the game removes every change. No Python or runtime is required.

## Use

Start the game through Steam, then double-click one of (TFU2: 60 FPS and borderless launchers work; hang and pacing capture are TFU1-only):

| Launcher | Does |
|---|---|
| **Apply 60 FPS + Borderless.cmd** | Everything below. Starts the game if needed. On first use it waits for you to close an exclusive-fullscreen game once, switches the game to native windowed mode and relaunches it. Then it watches for 10 minutes and saves a dump if the window freezes. |
| **Apply 60 FPS.cmd** | 60 FPS profile + controller fixes only. |
| **Restore 30 FPS.cmd** | Undo the 60 FPS profile in the running game. |
| **Restore Borderless.cmd** | Restore the window and the original `VideoWindowed` setting (waits for the game to exit, because the game rewrites its config on close). |
| **Capture freeze.cmd** | Save a hang dump + snapshot to `analysis\hangs\`. |
| **Record frame pacing.cmd** | Record 120 s of update timing to `analysis\pacing\`. |

Command line: `SWTFU-Helper.exe` with no arguments lists every command
(`fps apply|restore|inspect`, `controller`, `borderless …`, `hang …`, `pacing`, `selftest`).

## What the fixes do

- **60 FPS profile.** Raises the engine's native frame cap to 60. Things the game
  counts per update instead of per second keep their 30 FPS speed: DMM contact,
  sleep and sweep timers, cloth recovery, physics and timescale smoothing, camera
  lerps, mouse and angular filters, trail UV scrolling and AI strafe turning.
  The cinematic camera samples exact time, so riding a ship no longer judders.
  Cutscenes also run at 60.
  The profile is applied while the game's threads are suspended. A failure rolls
  everything back.
- **Borderless.** Uses the game's own windowed mode, removes the borders, and fills
  the current monitor. Focus is never forced and the window is never made topmost.
  Also fixes:
  - the device-reset loop after a resize (white screen, very slow updates);
  - background playback and audio;
  - cursor visibility;
  - window-message pumping during movies.
- **Controller.** A pad that disconnects mid-game (battery, cable) works again
  when it reconnects. The launch movies before the main menu can be skipped
  with any button. Story movies keep their original rules.
- **Subtitles after pause.** The line on screen when you open the pause menu
  comes back for its remaining time when you resume (original game bug).

The evidence for each fix is in the comments of `helper/tfu1.h` and `helper/tfu2.h`.

## Repository layout

| Path | What |
|---|---|
| `*.cmd` | Launchers (call `SWTFU-Helper.exe`). |
| `helper/` | **The tool**, in C++ (`CMakeLists.txt`). `fps.cpp` is the profile transaction. `fixes.cpp` holds controller, subtitle, movie, playback and renderer. `borderless.cpp` is the window and config. `diagnostics.cpp` is hang and pacing. `patching.cpp` suspends threads and swaps verified bytes. `game.cpp` attaches to the game process. `selftest.cpp` has offline checks; `verify.cpp` checks patch sites against a decoded image. |
| `helper/tfu1.h`, `helper/tfu2.h` | Each game's patch bytes, helper routines and identity, with the reasoning for each fix. Machine code is one instruction per line with its disassembly. |
| `tools/decode_exe.py` | Decrypts either game's SteamStub-wrapped exe into an image for IDA and `verify` (needs `pefile`, `pycryptodome`). |
| `.github/workflows/` | CI: x86 build, selftest. |

Local-only, git-ignored files: `analysis/` (decoded images, IDA databases, research
notes, hang dumps and pacing captures), the game executable,
`borderless-state.json` (what Restore Borderless undoes),
`borderless-last-run.json` (last run log) and `Config.before-borderless.xml`
(config backup).

## Developing

Requires Visual Studio 2022+ with C++ x86 tools and CMake.

```powershell
cmake -S helper -B helper/build -A Win32        # once
cmake --build helper/build --config Release     # writes SWTFU-Helper.exe to the repo root
.\SWTFU-Helper.exe selftest
python tools/decode_exe.py "<install>\SWTFU.exe" analysis/SWTFU.decoded.exe   # once per game
.\SWTFU-Helper.exe verify analysis/SWTFU.decoded.exe                         # every patch site's original bytes
```

To change a patch, edit `helper/tfu1.h` or `helper/tfu2.h`, keep each instruction's disassembly
comment accurate, rebuild, and run `verify` against both games' decoded images.
