"""TFU2 60 FPS profile: patch definitions (applied by helper/fps.cpp).

Cap: float at [[gGameObj 0x13CB118] + 0x264], read every frame by Game_FrameTick
(0x4142C0) -> Frame_ComputeDt (0x4098F0), which snaps gGameplayDt to 1/cap.
Cutscenes (Edl_StoreGameState 0x612850) save the cap to [[0x13DB544] + 0x284],
force 30.0 and Edl_RestoreGameState (0x613050) restores the saved value.
See docs/tfu2-60fps.md.
"""
LIMIT_OFFSET = 0x264
CINEMATIC_STATE = 0x13DB544
SAVED_LIMIT_OFFSET = 0x284

PATCHES = (
    # Edl_StoreGameState: `fld [kThirty 0xF34AF8]; fstp [eax+264h]` forces 30 for the
    # cutscene. NOP both (keeps the x87 stack balanced) so cutscenes keep the cap.
    ("cinematic_limit", 0x612D0E, bytes.fromhex("d905f84af300d99864020000"), b"\x90" * 12),
    # Ports of TFU1 fixes (same engine code, matched by shape; see docs/tfu2-60fps.md).
    # DMM_ContactMessages (0xA56220): `frame - stamp > 30` on the per-update counter
    # dword_13CB13C. >61 keeps the 31/30 s interval at 60 updates/s.
    ("dmm_contact_interval_a", 0xA562C1, bytes.fromhex("83fa1e"), bytes.fromhex("83fa3d")),
    ("dmm_contact_interval_b", 0xA562EF, bytes.fromhex("83fe1e"), bytes.fromhex("83fe3d")),
    # DMM_QuietCounter_Update (0xBF5690): sleep after 2 quiet updates -> 4 at 60.
    ("dmm_quiet_sleep_duration", 0xBF5819, bytes.fromhex("3c01"), bytes.fromhex("3c03")),
    # Cloth startup binding hold: 4 updates, release on 5 -> 8 and 9 at 60.
    ("cloth_startup_hold_end", 0xA39085, bytes.fromhex("83f805"), bytes.fromhex("83f809")),
    ("cloth_startup_release", 0xA39093, bytes.fromhex("83f804"), bytes.fromhex("83f808")),
    # Cloth_VerletStep_MotionSuppress (0xA1D200): 3 updates without inertial force -> 6.
    ("cloth_motion_suppression", 0xA1D37C, bytes.fromhex("c7475003000000"), bytes.fromhex("c7475006000000")),
    # DMM sweep delay counter (+0x1650), 60 updates -> 120: reset, setter, constructor.
    ("dmm_sweep_delay_reset", 0xA418CE, bytes.fromhex("c786501600003c000000"), bytes.fromhex("c7865016000078000000")),
    ("dmm_sweep_delay_set", 0xA418E0, bytes.fromhex("c781501600003c000000"), bytes.fromhex("c7815016000078000000")),
    ("dmm_sweep_delay_initial", 0xA43629, bytes.fromhex("c786501600003c000000"), bytes.fromhex("c7865016000078000000")),
)
HELPERS = ()
