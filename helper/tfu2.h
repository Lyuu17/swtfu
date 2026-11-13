// TFU2 (SWTFU2.exe): every patch byte, helper routine and the game identity SWTFU-Helper uses.
// Hand-maintained. Machine code is one instruction per line (disassembly in the
// comment, +offset from the start of the block). Check site bytes with
// `SWTFU-Helper verify <decoded.exe>`.
#pragma once
#include "game.h"

static const GameInfo kTfu2Game = {"tfu2", L"swtfu2.exe", "79874d834449c3ae82925d4ddec9f6cfc7f197ff02af456ebf942db3a2b65167", 
    B("\x68\x00\x00\x40\x00")    // +000: push 0x400000
    + B("\xe8\xf6\x08\x00\x00")  // +005: call 0x900
    + B("\x59")                  // +00a: pop ecx
    + B("\xc3")                  // +00b: ret
    + Bytes(4, '\xcc')           // +00c: padding (4)
, 0x13CB118, 32500};



// TFU2 60 FPS profile: patch definitions (applied by helper/fps.cpp).
//
// Cap: float at [[gGameObj 0x13CB118] + 0x264], read every frame by Game_FrameTick
// (0x4142C0) -> Frame_ComputeDt (0x4098F0), which snaps gGameplayDt to 1/cap.
// Cutscenes (Edl_StoreGameState 0x612850) save the cap to [[0x13DB544] + 0x284],
// force 30.0 and Edl_RestoreGameState (0x613050) restores the saved value.
static const FixedPatch k_tfu2_fps_patches[] = {
    // Edl_StoreGameState: `fld [kThirty 0xF34AF8]; fstp [eax+264h]` forces 30 for the
    // cutscene. NOP both (keeps the x87 stack balanced) so cutscenes keep the cap.
    {"cinematic_limit", 0x612D0E, B("\xd9\x05\xf8\x4a\xf3\x00\xd9\x98\x64\x02\x00\x00"), B("\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90")},
    // Lua SetMatrixFromCameraAnimation -> sub_6103B0 rounds cutscene camera time to
    // (int)(30t + 0.5)/30 before sampling (TFU1 0x4D95F6 did floor). At 60 the camera
    // would step every 2nd update while props move every update (judder). NOP the
    // fld 30 / fmul / fadd 0.5 / ftol / fidivr sequence so the exact t from
    // `fld [ebp+0Ch]` (0x610439) is what `fstp [esp+50h]` stores; x87 stack stays balanced.
    {"cinematic_camera_exact_time", 0x61043C, 
        B("\xdd\x05\xf0\x4a\xf3\x00")    // +000: fld qword ptr [0xf34af0]
        + B("\xdc\xc9")                  // +006: fmul st(1), st(0)
        + B("\xd9\xc9")                  // +008: fxch st(1)
        + B("\xdc\x05\x30\x44\xf3\x00")  // +00a: fadd qword ptr [0xf34430]
        + B("\xe8\x0f\xa0\xe7\xff")      // +010: call 0xffe7a024
        + B("\x89\x44\x24\x4c")          // +015: mov dword ptr [esp + 0x4c], eax
        + B("\xda\x7c\x24\x4c")          // +019: fidivr dword ptr [esp + 0x4c]
    , 
        Bytes(29, '\x90')    // +000: padding (29)
    },
    // ForceGrip_IntegratePos_1over30 (0x9E4BE0, per update via ForceGrip_HeldPosUpdate
    // 0x9FC930): held object pos = pos + (1/30) * velocity, a hardcoded step. At 60
    // updates/s it moves 2x per second (PCGamingWiki: gripped objects drift up, worse
    // at 60). Load the existing 1/60 float (0xF35118) instead of 1/30 (0xF34DA8).
    {"force_grip_integrate_step", 0x9E4C1E, B("\xf3\x0f\x10\x05\xa8\x4d\xf3\x00"), B("\xf3\x0f\x10\x05\x18\x51\xf3\x00")},
    // Same integrator shape in the camera behaviour (sub_9E5790): pos + (1/30) * velocity.
    {"camera_integrate_step", 0x9E5821, B("\xf3\x0f\x10\x05\xa8\x4d\xf3\x00"), B("\xf3\x0f\x10\x05\x18\x51\xf3\x00")},
    // sub_806130 (per update via sub_407D50 <- GameLoop_FrameStep) passes a hardcoded
    // 1/30 as the dt of sub_803190 (per-channel timers) and of vfunc +0x8C.
    {"channel_timers_dt", 0x80613C, B("\xd9\x05\xa8\x4d\xf3\x00"), B("\xd9\x05\x18\x51\xf3\x00")},
    {"channel_vfunc_dt", 0x806163, B("\xd9\x05\xa8\x4d\xf3\x00"), B("\xd9\x05\x18\x51\xf3\x00")},
    // Ports of TFU1 fixes (same engine code, matched by shape).
    // DMM_ContactMessages (0xA56220): `frame - stamp > 30` on the per-update counter
    // dword_13CB13C. >61 keeps the 31/30 s interval at 60 updates/s.
    {"dmm_contact_interval_a", 0xA562C1, B("\x83\xfa\x1e"), B("\x83\xfa\x3d")},
    {"dmm_contact_interval_b", 0xA562EF, B("\x83\xfe\x1e"), B("\x83\xfe\x3d")},
    // DMM_QuietCounter_Update (0xBF5690): sleep after 2 quiet updates -> 4 at 60.
    {"dmm_quiet_sleep_duration", 0xBF5819, B("\x3c\x01"), B("\x3c\x03")},
    // Cloth startup binding hold: 4 updates, release on 5 -> 8 and 9 at 60.
    {"cloth_startup_hold_end", 0xA39085, B("\x83\xf8\x05"), B("\x83\xf8\x09")},
    {"cloth_startup_release", 0xA39093, B("\x83\xf8\x04"), B("\x83\xf8\x08")},
    // Cloth_VerletStep_MotionSuppress (0xA1D200): 3 updates without inertial force -> 6.
    {"cloth_motion_suppression", 0xA1D37C, B("\xc7\x47\x50\x03\x00\x00\x00"), B("\xc7\x47\x50\x06\x00\x00\x00")},
    // Cloth rotation recovery (sub_A239D0, twin of TFU1 sub_BE9D90): counter = 30.0,
    // then -1 per call. Load TFU2's 60.0 constant (0xF395BC) instead.
    {"cloth_rotation_recovery", 0xA23B68, B("\xd9\x05\xf8\x4a\xf3\x00"), B("\xd9\x05\xbc\x95\xf3\x00")},
    // DMM sweep delay counter (+0x1650), 60 updates -> 120: reset, setter, constructor.
    {"dmm_sweep_delay_reset", 0xA418CE, B("\xc7\x86\x50\x16\x00\x00\x3c\x00\x00\x00"), B("\xc7\x86\x50\x16\x00\x00\x78\x00\x00\x00")},
    {"dmm_sweep_delay_set", 0xA418E0, B("\xc7\x81\x50\x16\x00\x00\x3c\x00\x00\x00"), B("\xc7\x81\x50\x16\x00\x00\x78\x00\x00\x00")},
    {"dmm_sweep_delay_initial", 0xA43629, B("\xc7\x86\x50\x16\x00\x00\x3c\x00\x00\x00"), B("\xc7\x86\x50\x16\x00\x00\x78\x00\x00\x00")},
};

// TFU2: keep the 30 Hz timescale/physics smoothing duration at 60 updates/s.
//
// sub_405180 moves gTimeScale (0x111D4C8) toward its target [this+0xE4] and
// sub_405220 moves the physics scale [this+0x90] toward [this+0x8C]*gTimeScale,
// each by at most a factor r per update ([this+0xE8] / [this+0x94]), x87 code.
// Same logic as TFU1 GameLoop_FrameStep 0x41BD4D/0x41BDDD (see
// swtfu/tfu1/fps/physics_smoothing.py). At 60 updates/s, sqrt(r) per update
// keeps the per-second rate. Every read of r is a 6-byte x87 memory operand:
// `fld`, `fmul` or `fdiv`; each becomes `call helper; nop`. The helper replays
// the load, takes the root only when the cap is exactly 60 and 1 < r < inf,
// then completes `fmul`/`fdiv` with the popping form.
static const HookSite k_tfu2_physics_smoothing_sites[] = {
    {"timescale_ratio_up_check", 0x4051BF, B("\xd9\x81\xe8\x00\x00\x00")},
    {"timescale_ratio_up", 0x4051CE, B("\xd8\x89\xe8\x00\x00\x00")},
    {"timescale_ratio_down_check", 0x4051F6, B("\xd9\x81\xe8\x00\x00\x00")},
    {"timescale_ratio_down", 0x405205, B("\xd8\xb1\xe8\x00\x00\x00")},
    {"physics_ratio_up_check", 0x40526F, B("\xd9\x81\x94\x00\x00\x00")},
    {"physics_ratio_up", 0x405280, B("\xd9\x81\x94\x00\x00\x00")},
    {"physics_ratio_down_check", 0x40529D, B("\xd9\x81\x94\x00\x00\x00")},
    {"physics_ratio_down", 0x4052B4, B("\xd8\xb1\x94\x00\x00\x00")},
};
static const SourceCheck k_tfu2_physics_smoothing_sources[] = {
    {0x0, B("")},
};
static const HookGroup k_tfu2_physics_smoothing = {"physics_smoothing", k_tfu2_physics_smoothing_sites, 8, 
    B("\xd9\x81\xe8\x00\x00\x00")               // +000: fld dword ptr [ecx + 0xe8]
    + B("\x9c")                                 // +006: pushfd
    + B("\x50")                                 // +007: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +008: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +00d: test eax, eax
    + B("\x74\x26")                             // +00f: je 0x37
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +011: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +01b: jne 0x37
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x3f") // +01d: cmp dword ptr [ecx + 0xe8], 0x3f800000
    + B("\x76\x0e")                             // +027: jbe 0x37
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x7f") // +029: cmp dword ptr [ecx + 0xe8], 0x7f800000
    + B("\x73\x02")                             // +033: jae 0x37
    + B("\xd9\xfa")                             // +035: fsqrt
    + B("\x58")                                 // +037: pop eax
    + B("\x9d")                                 // +038: popfd
    + B("\xc3")                                 // +039: ret
    + Bytes(6, '\x90')                          // +03a: padding (6)
    + B("\xd9\x81\xe8\x00\x00\x00")             // +040: fld dword ptr [ecx + 0xe8]
    + B("\x9c")                                 // +046: pushfd
    + B("\x50")                                 // +047: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +048: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +04d: test eax, eax
    + B("\x74\x26")                             // +04f: je 0x77
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +051: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +05b: jne 0x77
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x3f") // +05d: cmp dword ptr [ecx + 0xe8], 0x3f800000
    + B("\x76\x0e")                             // +067: jbe 0x77
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x7f") // +069: cmp dword ptr [ecx + 0xe8], 0x7f800000
    + B("\x73\x02")                             // +073: jae 0x77
    + B("\xd9\xfa")                             // +075: fsqrt
    + B("\x58")                                 // +077: pop eax
    + B("\x9d")                                 // +078: popfd
    + B("\xde\xc9")                             // +079: fmulp st(1)
    + B("\xc3")                                 // +07b: ret
    + Bytes(4, '\x90')                          // +07c: padding (4)
    + B("\xd9\x81\xe8\x00\x00\x00")             // +080: fld dword ptr [ecx + 0xe8]
    + B("\x9c")                                 // +086: pushfd
    + B("\x50")                                 // +087: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +088: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +08d: test eax, eax
    + B("\x74\x26")                             // +08f: je 0xb7
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +091: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +09b: jne 0xb7
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x3f") // +09d: cmp dword ptr [ecx + 0xe8], 0x3f800000
    + B("\x76\x0e")                             // +0a7: jbe 0xb7
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x7f") // +0a9: cmp dword ptr [ecx + 0xe8], 0x7f800000
    + B("\x73\x02")                             // +0b3: jae 0xb7
    + B("\xd9\xfa")                             // +0b5: fsqrt
    + B("\x58")                                 // +0b7: pop eax
    + B("\x9d")                                 // +0b8: popfd
    + B("\xc3")                                 // +0b9: ret
    + Bytes(6, '\x90')                          // +0ba: padding (6)
    + B("\xd9\x81\xe8\x00\x00\x00")             // +0c0: fld dword ptr [ecx + 0xe8]
    + B("\x9c")                                 // +0c6: pushfd
    + B("\x50")                                 // +0c7: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +0c8: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +0cd: test eax, eax
    + B("\x74\x26")                             // +0cf: je 0xf7
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +0d1: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +0db: jne 0xf7
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x3f") // +0dd: cmp dword ptr [ecx + 0xe8], 0x3f800000
    + B("\x76\x0e")                             // +0e7: jbe 0xf7
    + B("\x81\xb9\xe8\x00\x00\x00\x00\x00\x80\x7f") // +0e9: cmp dword ptr [ecx + 0xe8], 0x7f800000
    + B("\x73\x02")                             // +0f3: jae 0xf7
    + B("\xd9\xfa")                             // +0f5: fsqrt
    + B("\x58")                                 // +0f7: pop eax
    + B("\x9d")                                 // +0f8: popfd
    + B("\xde\xf9")                             // +0f9: fdivp st(1)
    + B("\xc3")                                 // +0fb: ret
    + Bytes(4, '\x90')                          // +0fc: padding (4)
    + B("\xd9\x81\x94\x00\x00\x00")             // +100: fld dword ptr [ecx + 0x94]
    + B("\x9c")                                 // +106: pushfd
    + B("\x50")                                 // +107: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +108: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +10d: test eax, eax
    + B("\x74\x26")                             // +10f: je 0x137
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +111: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +11b: jne 0x137
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x3f") // +11d: cmp dword ptr [ecx + 0x94], 0x3f800000
    + B("\x76\x0e")                             // +127: jbe 0x137
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x7f") // +129: cmp dword ptr [ecx + 0x94], 0x7f800000
    + B("\x73\x02")                             // +133: jae 0x137
    + B("\xd9\xfa")                             // +135: fsqrt
    + B("\x58")                                 // +137: pop eax
    + B("\x9d")                                 // +138: popfd
    + B("\xc3")                                 // +139: ret
    + Bytes(6, '\x90')                          // +13a: padding (6)
    + B("\xd9\x81\x94\x00\x00\x00")             // +140: fld dword ptr [ecx + 0x94]
    + B("\x9c")                                 // +146: pushfd
    + B("\x50")                                 // +147: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +148: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +14d: test eax, eax
    + B("\x74\x26")                             // +14f: je 0x177
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +151: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +15b: jne 0x177
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x3f") // +15d: cmp dword ptr [ecx + 0x94], 0x3f800000
    + B("\x76\x0e")                             // +167: jbe 0x177
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x7f") // +169: cmp dword ptr [ecx + 0x94], 0x7f800000
    + B("\x73\x02")                             // +173: jae 0x177
    + B("\xd9\xfa")                             // +175: fsqrt
    + B("\x58")                                 // +177: pop eax
    + B("\x9d")                                 // +178: popfd
    + B("\xc3")                                 // +179: ret
    + Bytes(6, '\x90')                          // +17a: padding (6)
    + B("\xd9\x81\x94\x00\x00\x00")             // +180: fld dword ptr [ecx + 0x94]
    + B("\x9c")                                 // +186: pushfd
    + B("\x50")                                 // +187: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +188: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +18d: test eax, eax
    + B("\x74\x26")                             // +18f: je 0x1b7
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +191: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +19b: jne 0x1b7
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x3f") // +19d: cmp dword ptr [ecx + 0x94], 0x3f800000
    + B("\x76\x0e")                             // +1a7: jbe 0x1b7
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x7f") // +1a9: cmp dword ptr [ecx + 0x94], 0x7f800000
    + B("\x73\x02")                             // +1b3: jae 0x1b7
    + B("\xd9\xfa")                             // +1b5: fsqrt
    + B("\x58")                                 // +1b7: pop eax
    + B("\x9d")                                 // +1b8: popfd
    + B("\xc3")                                 // +1b9: ret
    + Bytes(6, '\x90')                          // +1ba: padding (6)
    + B("\xd9\x81\x94\x00\x00\x00")             // +1c0: fld dword ptr [ecx + 0x94]
    + B("\x9c")                                 // +1c6: pushfd
    + B("\x50")                                 // +1c7: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +1c8: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +1cd: test eax, eax
    + B("\x74\x26")                             // +1cf: je 0x1f7
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +1d1: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1a")                             // +1db: jne 0x1f7
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x3f") // +1dd: cmp dword ptr [ecx + 0x94], 0x3f800000
    + B("\x76\x0e")                             // +1e7: jbe 0x1f7
    + B("\x81\xb9\x94\x00\x00\x00\x00\x00\x80\x7f") // +1e9: cmp dword ptr [ecx + 0x94], 0x7f800000
    + B("\x73\x02")                             // +1f3: jae 0x1f7
    + B("\xd9\xfa")                             // +1f5: fsqrt
    + B("\x58")                                 // +1f7: pop eax
    + B("\x9d")                                 // +1f8: popfd
    + B("\xde\xf9")                             // +1f9: fdivp st(1)
    + B("\xc3")                                 // +1fb: ret
    + Bytes(4, '\x90')                          // +1fc: padding (4)
, 64, k_tfu2_physics_smoothing_sources, 0};

// TFU2: scale per-update amounts by 30 / cap so they keep their 30 Hz rate at 60.
//
// x87 form of swtfu/tfu1/fps/update_rate.py. Each site is a 6-byte `fld` of a
// per-update amount, replaced by `call helper; nop`. The helper replays the
// load, then multiplies by 30.0 and divides by the cap ([gGameObj]+0x264).
// - trail_accum_a (sub_A87870) / trail_accum_b (sub_A89590): trail accumulator
//   +0x91C += step +0x920 once per call, no frame time (step from asset data,
//   default 1.0 in sub_8C4680).
static const HookSite k_tfu2_update_rate_sites[] = {
    {"trail_step_a", 0xA87885, B("\xd9\x87\x20\x09\x00\x00")},
    {"trail_step_b", 0xA895E1, B("\xd9\x83\x20\x09\x00\x00")},
};
static const SourceCheck k_tfu2_update_rate_sources[] = {
    {0x0, B("")},
};
static const HookGroup k_tfu2_update_rate = {"update_rate", k_tfu2_update_rate_sites, 2, 
    B("\xd9\x87\x20\x09\x00\x00")        // +000: fld dword ptr [edi + 0x920]
    + B("\x9c")                          // +006: pushfd
    + B("\x50")                          // +007: push eax
    + B("\xa1\x18\xb1\x3c\x01")          // +008: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                      // +00d: test eax, eax
    + B("\x74\x15")                      // +00f: je 0x26
    + B("\x83\xb8\x64\x02\x00\x00\x00")  // +011: cmp dword ptr [eax + 0x264], 0
    + B("\x74\x0c")                      // +018: je 0x26
    + B("\xd8\x0d\xf8\x4a\xf3\x00")      // +01a: fmul dword ptr [0xf34af8]
    + B("\xd8\xb0\x64\x02\x00\x00")      // +020: fdiv dword ptr [eax + 0x264]
    + B("\x58")                          // +026: pop eax
    + B("\x9d")                          // +027: popfd
    + B("\xc3")                          // +028: ret
    + Bytes(23, '\x90')                  // +029: padding (23)
    + B("\xd9\x83\x20\x09\x00\x00")      // +040: fld dword ptr [ebx + 0x920]
    + B("\x9c")                          // +046: pushfd
    + B("\x50")                          // +047: push eax
    + B("\xa1\x18\xb1\x3c\x01")          // +048: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                      // +04d: test eax, eax
    + B("\x74\x15")                      // +04f: je 0x66
    + B("\x83\xb8\x64\x02\x00\x00\x00")  // +051: cmp dword ptr [eax + 0x264], 0
    + B("\x74\x0c")                      // +058: je 0x66
    + B("\xd8\x0d\xf8\x4a\xf3\x00")      // +05a: fmul dword ptr [0xf34af8]
    + B("\xd8\xb0\x64\x02\x00\x00")      // +060: fdiv dword ptr [eax + 0x264]
    + B("\x58")                          // +066: pop eax
    + B("\x9d")                          // +067: popfd
    + B("\xc3")                          // +068: ret
    + Bytes(23, '\x90')                  // +069: padding (23)
, 64, k_tfu2_update_rate_sources, 0};

// TFU2: keep the button-mash timing minigame at its 30 Hz pace at 60 updates/s.
//
// MiniGameButtonMash_Update (0x986F80, vtable 0xFF56A4) runs once per update. Its
// time limit already uses gGameplayDt, but three steps do not (all must scale
// together or the balance of marker speed, fill and decay changes):
// - marker box +0x60..+0x6C += velocity (SSE `addps xmm0,xmm1` at 0x987850);
// - meter +0x28 += diff * [+0x30] while the marker overlaps the target (0x9878CA);
// - meter -= diff * [+0x34] (0x98793E).
// Each step is scaled by 30/cap. `[+0x3C] *= -1.0` (0x987872) is the marker
// bounce, an event rather than a rate, so it is left alone.
// Assumed to be the Gorog QTE (PCGamingWiki: buggy at high FPS); no "Gorog"/"QTE"
// strings exist.
static const HookSite k_tfu2_minigame_sites[] = {
    {"mash_marker_step", 0x987850, B("\x0f\x58\xc1\x0f\x29\x44\x24\x50")},
    {"mash_fill", 0x9878CA, B("\xd8\x4e\x30\xd8\x46\x28")},
    {"mash_decay", 0x98793E, B("\xd8\x4e\x34\xde\xe9")},
};
static const SourceCheck k_tfu2_minigame_sources[] = {
    {0x0, B("")},
};
static const HookGroup k_tfu2_minigame = {"minigame", k_tfu2_minigame_sites, 3, 
    B("\x9c")                                // +000: pushfd
    + B("\x50")                              // +001: push eax
    + B("\xa1\x18\xb1\x3c\x01")              // +002: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                          // +007: test eax, eax
    + B("\x74\x2e")                          // +009: je 0x39
    + B("\x83\xb8\x64\x02\x00\x00\x00")      // +00b: cmp dword ptr [eax + 0x264], 0
    + B("\x74\x25")                          // +012: je 0x39
    + B("\x83\xec\x10")                      // +014: sub esp, 0x10
    + B("\x0f\x11\x14\x24")                  // +017: movups xmmword ptr [esp], xmm2
    + B("\xf3\x0f\x10\x15\xf8\x4a\xf3\x00")  // +01b: movss xmm2, dword ptr [0xf34af8]
    + B("\xf3\x0f\x5e\x90\x64\x02\x00\x00")  // +023: divss xmm2, dword ptr [eax + 0x264]
    + B("\x0f\xc6\xd2\x00")                  // +02b: shufps xmm2, xmm2, 0
    + B("\x0f\x59\xc2")                      // +02f: mulps xmm0, xmm2
    + B("\x0f\x10\x14\x24")                  // +032: movups xmm2, xmmword ptr [esp]
    + B("\x83\xc4\x10")                      // +036: add esp, 0x10
    + B("\x58")                              // +039: pop eax
    + B("\x9d")                              // +03a: popfd
    + B("\x0f\x58\xc1")                      // +03b: addps xmm0, xmm1
    + B("\x0f\x29\x44\x24\x54")              // +03e: movaps xmmword ptr [esp + 0x54], xmm0
    + B("\xc3")                              // +043: ret
    + Bytes(28, '\x90')                      // +044: padding (28)
    + B("\xd8\x4e\x30")                      // +060: fmul dword ptr [esi + 0x30]
    + B("\x9c")                              // +063: pushfd
    + B("\x50")                              // +064: push eax
    + B("\xa1\x18\xb1\x3c\x01")              // +065: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                          // +06a: test eax, eax
    + B("\x74\x15")                          // +06c: je 0x83
    + B("\x83\xb8\x64\x02\x00\x00\x00")      // +06e: cmp dword ptr [eax + 0x264], 0
    + B("\x74\x0c")                          // +075: je 0x83
    + B("\xd8\x0d\xf8\x4a\xf3\x00")          // +077: fmul dword ptr [0xf34af8]
    + B("\xd8\xb0\x64\x02\x00\x00")          // +07d: fdiv dword ptr [eax + 0x264]
    + B("\x58")                              // +083: pop eax
    + B("\x9d")                              // +084: popfd
    + B("\xd8\x46\x28")                      // +085: fadd dword ptr [esi + 0x28]
    + B("\xc3")                              // +088: ret
    + Bytes(55, '\x90')                      // +089: padding (55)
    + B("\xd8\x4e\x34")                      // +0c0: fmul dword ptr [esi + 0x34]
    + B("\x9c")                              // +0c3: pushfd
    + B("\x50")                              // +0c4: push eax
    + B("\xa1\x18\xb1\x3c\x01")              // +0c5: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                          // +0ca: test eax, eax
    + B("\x74\x15")                          // +0cc: je 0xe3
    + B("\x83\xb8\x64\x02\x00\x00\x00")      // +0ce: cmp dword ptr [eax + 0x264], 0
    + B("\x74\x0c")                          // +0d5: je 0xe3
    + B("\xd8\x0d\xf8\x4a\xf3\x00")          // +0d7: fmul dword ptr [0xf34af8]
    + B("\xd8\xb0\x64\x02\x00\x00")          // +0dd: fdiv dword ptr [eax + 0x264]
    + B("\x58")                              // +0e3: pop eax
    + B("\x9d")                              // +0e4: popfd
    + B("\xde\xe9")                          // +0e5: fsubp st(1)
    + B("\xc3")                              // +0e7: ret
    + Bytes(56, '\x90')                      // +0e8: padding (56)
, 96, k_tfu2_minigame_sources, 0};

// TFU2: keep per-update camera smoothing at its 30 Hz response at 60 updates/s.
//
// Both steps are inside camera behaviour updates (TFUCameraBehaviorFollow_Update
// 0xA00580, virtual Update(dt) at vtable slot 0xFFBA94) but ignore dt:
// - camera_smooth_fixed_alpha (sub_9DC0C0): step = (target - current) * alpha,
//   alpha from [ecx+20h] or [ecx+24h]; both paths meet at 0x9DC1FF
//   (`fstp [esp+8]`). At 60 use 1 - sqrt(1 - alpha), as TFU1's camera_smoothing.
// - ForceGrip_DecayStep_0p6 (0x9EF190, called from the follow camera): value
//   +0x9C *= 0.6 per update (0x9EF1A4 `fmul qword [0xF92850]`). At 60 use sqrt(0.6).
// Both only change when the cap is exactly 60 and the value is in range.
static const HookSite k_tfu2_camera_smoothing_sites[] = {
    {"camera_fixed_alpha", 0x9DC1FF, B("\xd9\x5c\x24\x08\x0f\x57\xd2")},
    {"camera_grip_decay", 0x9EF1A4, B("\xdc\x0d\x50\x28\xf9\x00")},
};
static const SourceCheck k_tfu2_camera_smoothing_sources[] = {
    {0x0, B("")},
};
static const HookGroup k_tfu2_camera_smoothing = {"camera_smoothing", k_tfu2_camera_smoothing_sites, 2, 
    B("\x9c")                                   // +000: pushfd
    + B("\x50")                                 // +001: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +002: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +007: test eax, eax
    + B("\x74\x28")                             // +009: je 0x33
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +00b: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x1c")                             // +015: jne 0x33
    + B("\x83\xec\x04")                         // +017: sub esp, 4
    + B("\xd9\x14\x24")                         // +01a: fst dword ptr [esp]
    + B("\x58")                                 // +01d: pop eax
    + B("\x85\xc0")                             // +01e: test eax, eax
    + B("\x7e\x11")                             // +020: jle 0x33
    + B("\x3d\x00\x00\x80\x3f")                 // +022: cmp eax, 0x3f800000
    + B("\x73\x0a")                             // +027: jae 0x33
    + B("\xd9\xe8")                             // +029: fld1
    + B("\xde\xe1")                             // +02b: fsubrp st(1)
    + B("\xd9\xfa")                             // +02d: fsqrt
    + B("\xd9\xe8")                             // +02f: fld1
    + B("\xde\xe1")                             // +031: fsubrp st(1)
    + B("\x58")                                 // +033: pop eax
    + B("\x9d")                                 // +034: popfd
    + B("\xd9\x5c\x24\x0c")                     // +035: fstp dword ptr [esp + 0xc]
    + B("\x0f\x57\xd2")                         // +039: xorps xmm2, xmm2
    + B("\xc3")                                 // +03c: ret
    + Bytes(19, '\x90')                         // +03d: padding (19)
    + B("\xdd\x05\x50\x28\xf9\x00")             // +050: fld qword ptr [0xf92850]
    + B("\x9c")                                 // +056: pushfd
    + B("\x50")                                 // +057: push eax
    + B("\xa1\x18\xb1\x3c\x01")                 // +058: mov eax, dword ptr [0x13cb118]
    + B("\x85\xc0")                             // +05d: test eax, eax
    + B("\x74\x0e")                             // +05f: je 0x6f
    + B("\x81\xb8\x64\x02\x00\x00\x00\x00\x70\x42") // +061: cmp dword ptr [eax + 0x264], 0x42700000
    + B("\x75\x02")                             // +06b: jne 0x6f
    + B("\xd9\xfa")                             // +06d: fsqrt
    + B("\x58")                                 // +06f: pop eax
    + B("\x9d")                                 // +070: popfd
    + B("\xde\xc9")                             // +071: fmulp st(1)
    + B("\xc3")                                 // +073: ret
    + Bytes(44, '\x90')                         // +074: padding (44)
, 80, k_tfu2_camera_smoothing_sources, 0};
static const HookGroup* const k_tfu2_fps_helpers[] = {&k_tfu2_physics_smoothing, &k_tfu2_update_rate, &k_tfu2_minigame, &k_tfu2_camera_smoothing};
static const FpsProfile kTfu2Fps = {k_tfu2_fps_patches, 16, k_tfu2_fps_helpers, 4, 0x264, 0x13DB544, 0x284};


// TFU2: dispatch window messages while waiting for a movie (TFU1 movie_messages twin).
//
// movie_wait_pump_msgs (sub_56B550) loops `while (this->busy[+0xB4]) {
// PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE); Sleep(1); }`, so input, paint and
// focus messages pile up undispatched for the whole movie. The 15-byte peek
// block at 0x56B570 (the loop head) becomes `call helper` + NOPs. The helper
// removes and dispatches up to 64 messages per tick. WM_QUIT is re-posted to the
// thread instead of dispatched, so WinMain still sees it after the movie (the
// game's own sub_570380 pump dispatches the same way, without TranslateMessage).
// Registers the loop relies on (ebx=Sleep, edi=PeekMessageA, esi=this) are preserved.
static const HookSite k_tfu2_movie_sites[] = {
    {"movie_wait_pump", 0x56B570, B("\x6a\x00\x6a\x00\x6a\x00\x6a\x00\x8d\x4c\x24\x1c\x51\xff\xd7")},
};
static const SourceCheck k_tfu2_movie_sources[] = {{0x0, B("")}};
static const HookGroup kTfu2MovieMessages = {"movie_messages", k_tfu2_movie_sites, 1, 
    B("\x83\xec\x20")                        // +000: sub esp, 0x20
    + B("\xc7\x44\x24\x1c\x40\x00\x00\x00")  // +003: mov dword ptr [esp + 0x1c], 0x40
    + B("\x6a\x01")                          // +00b: push 1
    + B("\x6a\x00")                          // +00d: push 0
    + B("\x6a\x00")                          // +00f: push 0
    + B("\x6a\x00")                          // +011: push 0
    + B("\x8d\x44\x24\x10")                  // +013: lea eax, [esp + 0x10]
    + B("\x50")                              // +017: push eax
    + B("\xff\x15\x0c\xd8\xf1\x00")          // +018: call dword ptr [0xf1d80c]
    + B("\x85\xc0")                          // +01e: test eax, eax
    + B("\x74\x2d")                          // +020: je 0x4f
    + B("\x83\x7c\x24\x04\x12")              // +022: cmp dword ptr [esp + 4], 0x12
    + B("\x74\x0f")                          // +027: je 0x38
    + B("\x54")                              // +029: push esp
    + B("\xff\x15\x18\xd8\xf1\x00")          // +02a: call dword ptr [0xf1d818]
    + B("\xff\x4c\x24\x1c")                  // +030: dec dword ptr [esp + 0x1c]
    + B("\x75\xd5")                          // +034: jne 0xb
    + B("\xeb\x17")                          // +036: jmp 0x4f
    + B("\xff\x15\x1c\xd2\xf1\x00")          // +038: call dword ptr [0xf1d21c]
    + B("\xff\x74\x24\x0c")                  // +03e: push dword ptr [esp + 0xc]
    + B("\xff\x74\x24\x0c")                  // +042: push dword ptr [esp + 0xc]
    + B("\x6a\x12")                          // +046: push 0x12
    + B("\x50")                              // +048: push eax
    + B("\xff\x15\x14\xd8\xf1\x00")          // +049: call dword ptr [0xf1d814]
    + B("\x83\xc4\x20")                      // +04f: add esp, 0x20
    + B("\xc3")                              // +052: ret
    + Bytes(45, '\x90')                      // +053: padding (45)
, 128, k_tfu2_movie_sources, 0};
