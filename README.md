# SWTFU 60 FPS patch candidate

Launch the Steam game, then double-click **Apply 60 FPS.cmd**.
The game must be running. Keep this folder's files together.
Python 3 is required.

For **60 FPS plus borderless fullscreen**, run **Apply 60 FPS + Borderless.cmd**
instead. On the first run, if the game is in exclusive fullscreen, close it
normally when the helper asks. The helper waits for it to exit, backs up the
active language's video configuration, enables the game's native windowed
mode, and relaunches through Steam. It then removes the borders and fills the
current monitor. Keep the helper open during this one-time setup.
If the game is closed, the combined helper launches it through Steam.

The combined helper also applies a targeted renderer reset fix **before**
resizing. The original game can retry forever after a resize because its
device-reset check rejects a healthy Direct3D device. This can cause a white
screen and very slow updates. The fix accepts both a healthy device and the
normal reset-ready device state, while retaining the original resource
release/rebuild calls and rejection of a device that is still lost.
`borderless-last-run.json` records each local run.

You can switch to other apps. The combined helper now keeps game updates and
audio running in the background, as requested. Real focus notifications are
preserved, and the native cursor visibility check is made idempotent so it
can release/show the mouse without accumulating ShowCursor calls every frame.
It does not force keyboard focus or make the game always on top.

The movie/resize fix ignores minimized and duplicate-size notifications,
preserving movie textures when the window has not genuinely changed size.
Valid positive-size changes retain the native resize path. These patches,
including their actual x86 instructions, passed isolated native checks.
The user has confirmed working Alt-Tab, refocus, movie playback, and cursor
visibility with the current window fixes. Physics validation is separate.
Each combined-helper run also records five seconds of read-only game-loop
timing in `borderless-last-run.json`, without requiring game focus.

The movie fixes process window messages during both synchronous playback
and movie-worker waits. Cursor handling checks the calling thread's actual
ShowCursor counter and refreshes it during movies. The combined command leaves
a read-only diagnostic watch open for ten minutes. If the window stops responding,
it captures one local thread/stack minidump under
`analysis/hangs/`. Leave the frozen game running briefly for capture. The
watch does not change focus or write to game memory. Closing the terminal
stops only the watch; the runtime patches stay applied until game exit.
**Capture freeze.cmd** can also capture an existing frozen session manually.

**Restore Borderless.cmd** restores the window styles and the original
`VideoWindowed` setting. It waits for a normal game exit before restoring the
configuration, because the game saves its cached configuration when closing.
Other configuration settings are preserved. Restarting also resets the FPS
setting to 30 and removes the in-memory renderer fix. Backups are saved beside
the scripts. None of these helpers replace the installed executable or DLLs.

To undo, double-click **Restore 30 FPS.cmd**, or restart the game.
The original executable, DLLs, assets, and saves are not modified.

The **60 FPS profile remains experimental**: full gameplay and frame-pacing
validation are still pending. It is not yet claimed to preserve all physics
behavior. See `analysis/FPS_AUDIT.md` for evidence and remaining work.

The opening ships/player judder investigation has a separate, uninstalled
fixed-step prototype. Its owned native replays preserve the original 30 Hz
simulation clock's float rounding and derive presentation time from that
timeline plus pending time. Two DMM contact-age checks still count rendered
frames and retain their 60 Hz compensation. These checks do not establish
live game equivalence; the opening judder fix is not ready for user testing.
The prototype also scopes the native packed timestep, inverse timestep and
clamped target alongside scalar time; see
[`analysis/simulation-clocks.md`](analysis/simulation-clocks.md).

## Controller reconnect and launch-movie skip

Both Apply commands now also run `swtfu/controller.py` (runtime only, like the other fixes).
- A controller that disconnects mid-game (battery, cable) works again once reconnected. The original game
  switches to a DirectInput fallback on disconnect and never switches back, discarding the reconnected
  XInput pad. The fix makes a successful XInput read win; DirectInput-only controllers are unchanged.
- The launch movies before the main menu can be skipped immediately with any key or controller button.
  Story movies and cutscenes keep their original skip rules.
Apply while the launch movies play (the Borderless command does this automatically when it starts the game).
`python -m swtfu.controller --inspect` shows whether the fixes are active. Restarting the game removes them.

## What changes

The patch verifies the game executable's SHA-256, checks its decoded code
signature and selects the engine's native 60 FPS setting. In-engine cinematics also run at
60 (camera sampled at exact time; cutscene subtitles still run fast, see 60FPS_ISSUES.md), forwards
the actual frame interval to DMM impact calculations, and preserves DMM's
contact-message interval in simulation seconds. The DMM sweep delay retains
its nominal duration through all three constructor/reset/setter initializers.
DMM quiet-body sleep also keeps its original duration: four quiet updates at
60 FPS replace two at 30 FPS, including scenes with adaptive solver substeps.
Cloth rotation recovery keeps its original duration rather than expiring
twice as fast. A small runtime helper also
preserves timescale and physics smoothing duration at 60 updates per second.
Camera eye/target lerps also retain their original convergence rate through
four verified hooks after the native coefficient clamps.
Mouse-camera deltas are normalized to a nominal 30 Hz sample using real
elapsed time, and player-camera angular filters retain their original
convergence rate. Those hooks are active only in the 60 FPS profile.
Authored animation sample
rates and encoded movie frame rates are preserved. Restore 30 FPS reverses
these FPS code changes and updates an active cinematic's saved limit safely.
The complete FPS profile applies while game threads are stopped. A failed
change attempts to restore the prior profile before resuming; incomplete
rollback reports that the game must be restarted.
The combined borderless helper includes additional runtime code for movies
and window handling. Run helpers from a normal terminal; this environment's
game memory-write access was denied during investigation.

The native timing function derives both gameplay and physics timesteps from
that setting. The Havok wrapper splits 30 FPS frames into two approximately
1/60-second solver steps, and takes one such step at 60 FPS. Other systems,
including DMM and Euphoria, receive explicit timesteps. These code findings
support this approach but do not replace gameplay tests. The isolated x86
harness executes the original timing/physics dispatch instructions and the
FPS patch sites: 13,977,790 assertions pass, plus profile/lifetime and helper-installation tests.
The full harness also uses an own-process x86 SEH diagnostic DLL; rebuild it
with `& .\analysis\build_scope_unwind.ps1` (toolchain paths are in that script).
The separate [native scene harness](analysis/dmm-materials/native-scene.md) also
executes original construction, updates, rebuilds and destruction, plus free and
anchored tetrahedron scenes. It reproduces and checks the
[sleep-duration fix](analysis/dmm-materials/sleep.md); broader worlds remain unverified.
The cloth checks execute the original integrator for a single unconstrained
vertex across masses, forces and pinned states, linear drag, spring/damper
response, and a coupled two-vertex spring. Havok/Euphoria dispatch tests stub
their external interfaces; full worlds and cloth collisions are unverified.

Supported original executable SHA-256:
`1c156705548fce9e33fa3423e75b5cbe36248319b96f4b4850bf2e8125692032`

For inspection without changes:

```powershell
python -m swtfu.fps --inspect
```

`analysis/probe_timing.py` is a read-only timing probe. Its clock measurements
are not GPU-present or physics-solver call measurements. Decoded images and
decompiled files under `analysis` are investigation artifacts, not patches;
do not copy them into the game installation.

## Repository layout

| Path | What |
|------|------|
| `*.cmd` | Double-click launchers (run from this folder). |
| `swtfu/game.py` | Attach to the running game; hash/signature checks; memory reads. |
| `swtfu/patching.py` | Shared patch engine: thread freeze, verified byte swap, `ShortCode` assembler. |
| `swtfu/fps/` | 60 FPS profile: `profile.py` (fixed byte patches + apply/rollback), helper hooks `physics_smoothing`, `camera_smoothing`, `input_timing`, `update_rate`. CLI: `python -m swtfu.fps --apply/--restore/--inspect`. |
| `swtfu/borderless.py` | Borderless window setup and the combined launcher. |
| `swtfu/controller.py` | Controller reconnect and launch-movie skip. |
| `swtfu/renderer_reset.py`, `windowed_playback.py`, `movie_messages.py` | Resize/reset, background playback and movie message-pump fixes. |
| `swtfu/frame_pacing.py`, `hang_diagnostics.py` | Read-only diagnostics. |
| `60FPS_ISSUES.md` | Per-issue 60 FPS investigation tracker. |
| `analysis/` | Research: audits, probes, fixtures, notes (`STATUS.md`, `FPS_AUDIT.md`). |
| `native/` | C++ research prototypes. |

Run `python check.py` after changing anything in `swtfu/`; it runs the
runtime-helper tests (no game needed).
