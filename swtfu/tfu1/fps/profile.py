"""Native FPS profile with investigated cinematic, DMM, and cloth timing fixes."""
import ctypes as C
import struct
import time

from swtfu.patching import apply_code_patch, FrozenThreads, CodeBusy
from swtfu.tfu1.fps import physics_smoothing, camera_smoothing, input_timing, update_rate

HELPERS = (physics_smoothing, camera_smoothing, input_timing, update_rate)

ADDRESS = 0x4DC461
ORIGINAL = bytes.fromhex("f30f118250010000")  # movss [edx+150h], xmm0 (30 FPS)
PATCHED = b"\x90" * len(ORIGINAL)
# Cinematics run at 60 (user choice). Known gap: cutscene subtitles/crawl are
# script-timed per update and still run fast (see 60FPS_ISSUES.md #5).
# Add "cinematic_limit" here to keep the original 30 FPS cinematics instead.
KEEP_ORIGINAL = set()
CINEMATIC_STATE = 0x12A1EF4
SAVED_LIMIT_OFFSET = 324
PATCHES = (
    ("cinematic_limit", ADDRESS, ORIGINAL, PATCHED),
    # Lua_SetMatrixFromCameraAnimation quantizes cinematic camera time to
    # floor(30t)/30 before sampling; props use exact time. At 60 the camera
    # steps every 2nd update while ridden ships move every update (judder).
    # NOP the quantizer so xmm0 keeps the exact time, like the prop path.
    ("cinematic_camera_exact_time", 0x4D95F6,
     bytes.fromhex("0f5ac8f20f1005081bf500f20f59c8f20f580d3800f600f20f2cd1f30f2acaf30f5ac9f20f5ec8f20f5ac1"),
     b"\x90" * 43),
    # ActionDirectLocomotionAI_StrafeNormalNav_Update: turn duration =
    # angle / (rate / gGameplayDt) with rate in 1/30 s ticks; at 60 it halves.
    # Load the 1/30 constant (kOneThirtieth 0xF51B04) instead of gGameplayDt.
    ("ai_strafe_turn_duration", 0x874CC3, bytes.fromhex("f30f102d08400d01"), bytes.fromhex("f30f102d041bf500")),
    # DMM impact/audio speed calculation divided each frame's deformation by
    # a hardcoded 1/30 s. Use the timestep already supplied to DMM instead.
    ("dmm_impact_interval", 0xC93AD0, bytes.fromhex("d905041bf500"), bytes.fromhex("d90508400d01")),
    # Native strict >30 frame checks mean 31 updates. At 60 FPS, >61 preserves
    # the same 31/30 second contact-message interval (62/60), not half of it.
    ("dmm_contact_interval_a", 0xC69601, bytes.fromhex("83fa1e"), bytes.fromhex("83fa3d")),
    ("dmm_contact_interval_b", 0xC69629, bytes.fromhex("83fe1e"), bytes.fromhex("83fe3d")),
    # DMM sleeps a quiet body after two qualifying final-substep updates.
    # Four at 60 Hz retain the original 2/30-second quiet duration. Intermediate
    # substeps cannot advance this counter; zero-dt scene updates also skip it.
    ("dmm_quiet_sleep_duration", 0xD567E4, bytes.fromhex("3c01"), bytes.fromhex("3c03")),
    # Cloth rotation recovery stores 30 updates, then subtracts one per
    # positive-dt update. Keep its duration when the update rate doubles.
    ("cloth_rotation_recovery", 0xBE9F11, bytes.fromhex("f30f1005101bf500"), bytes.fromhex("f30f1005f822f500")),
    # Initial binding hold lasts four updates; start its existing seconds-
    # based release on update nine rather than five in the 60 Hz profile.
    ("cloth_startup_hold_end", 0xBC96F2, bytes.fromhex("83f805"), bytes.fromhex("83f809")),
    ("cloth_startup_release", 0xBC96FA, bytes.fromhex("83f804"), bytes.fromhex("83f808")),
    # After transform discontinuities, inertial force is suppressed for three
    # positive-dt preparations. Preserve its nominal 0.1-second duration.
    ("cloth_motion_suppression", 0xBC8AA4, bytes.fromhex("c7475003000000"), bytes.fromhex("c7475006000000")),
    # DMM sweep suppression decrements once per eligible object update.
    # Constructor, reset and explicit setter all initialize the same counter.
    ("dmm_sweep_delay_reset", 0xC95790, bytes.fromhex("c786601500003c000000"), bytes.fromhex("c7866015000078000000")),
    ("dmm_sweep_delay_set", 0xC957A0, bytes.fromhex("c781601500003c000000"), bytes.fromhex("c7816015000078000000")),
    ("dmm_sweep_delay_initial", 0xC987CC, bytes.fromhex("c786601500003c000000"), bytes.fromhex("c7866015000078000000")),
)


def inspect_sites(game):
    result = {}
    for name, address, original, patched in PATCHES:
        current = game.read(address, len(original))
        result[name] = "60" if current == patched else "30" if current == original else "unsupported"
    try:
        for module in HELPERS:
            result.update(module.inspect_sites(game))
    except (OSError, RuntimeError):
        result["smoothing"] = "unsupported"
    return result


def _snapshot(game):
    groups = []
    for module in HELPERS:
        _, currents = module.validate(game)
        groups.append((module, tuple((site[0], current) for site, current in zip(module.SITES, currents))))
    current = game.f32(game.context + 0x150)
    fixed = []
    for name, address, original, patched in PATCHES:
        code = game.read(address, len(original))
        if code not in (original, patched):
            raise RuntimeError(f"Unsupported {name} code; no FPS changes applied")
        fixed.append((address, code))
    cinematic = game.u32(CINEMATIC_STATE)
    saved = game.f32(cinematic + SAVED_LIMIT_OFFSET) if cinematic else None
    if current not in (30, 60) or saved not in (None, 30, 60):
        raise RuntimeError("Unexpected frame-limit or cinematic state; no FPS changes applied")
    return fixed, groups, current, cinematic, saved


def _write_float(game, address, value):
    data = C.create_string_buffer(struct.pack("<f", value))
    count = C.c_size_t()
    if not game.kernel.WriteProcessMemory(game.handle, address, data, 4, C.byref(count)):
        raise C.WinError(C.get_last_error())
    if count.value != 4 or game.f32(address) != value:
        raise RuntimeError("Frame-limit write did not verify")


def _apply_frozen(game, fps):
    fixed, groups, current, cinematic, saved = _snapshot(game)
    changed = False
    try:
        for (_, code), (name, address, original, patched) in zip(fixed, PATCHES):
            target = patched if fps == 60 and name not in KEEP_ORIGINAL else original
            changed |= apply_code_patch(game, address, code, target)
        for module, _ in groups:
            changed |= module.set_enabled(game, fps == 60)
        if cinematic and saved != fps:
            _write_float(game, cinematic + SAVED_LIMIT_OFFSET, fps)
            changed = True
        changed |= game.set_limit(current, fps)
    except BaseException as error:
        failures = []
        # Threads remain stopped during rollback. Validate owned helpers
        # before touching their calls; do not overwrite foreign code.
        for module, before in reversed(groups):
            try:
                module.validate(game)
                for address, code in reversed(before):
                    now = game.read(address, len(code))
                    if now != code:
                        apply_code_patch(game, address, now, code)
            except BaseException as rollback_error:
                failures.append(str(rollback_error))
        for (address, code), (_, _, original, patched) in reversed(tuple(zip(fixed, PATCHES))):
            try:
                now = game.read(address, len(code))
                if now not in (original, patched):
                    raise RuntimeError(f"Unrecognized code during rollback at {address:#x}")
                if now != code:
                    apply_code_patch(game, address, now, code)
            except BaseException as rollback_error:
                failures.append(str(rollback_error))
        for address, value in ((game.context + 0x150, current), (cinematic + SAVED_LIMIT_OFFSET, saved)):
            if value is None:
                continue
            try:
                if game.f32(address) != value:
                    _write_float(game, address, value)
            except BaseException as rollback_error:
                failures.append(str(rollback_error))
        if failures:
            raise RuntimeError(f"FPS profile failed: {error}; rollback incomplete: {'; '.join(failures)}. Restart the game.") from error
        raise RuntimeError(f"FPS profile failed; prior profile restored: {error}") from error
    return changed


def set_profile(game, fps):
    if fps not in (30, 60):
        raise RuntimeError("Only the investigated 30 and 60 FPS profiles are supported")
    _snapshot(game)  # Fast read-only rejection before suspending threads.
    ranges = [(address, len(original)) for _, address, original, _ in PATCHES]
    ranges.extend((site[0], len(site[1])) for module in HELPERS for site in module.SITES)
    deadline = time.monotonic() + 5
    while True:
        try:
            # Revalidate/snapshot AFTER the whole profile is quiescent, then
            # keep threads stopped until commit or verified rollback finishes.
            with FrozenThreads(game, ADDRESS, len(ORIGINAL), ranges=ranges):
                return _apply_frozen(game, fps)
        except CodeBusy:
            if time.monotonic() >= deadline:
                raise RuntimeError("FPS code is in use; no profile changes applied. Retry the helper.") from None
            time.sleep(0.005)
