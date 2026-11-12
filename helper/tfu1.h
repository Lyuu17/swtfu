// TFU1 (SWTFU.exe): every patch byte, helper routine and the game identity SWTFU-Helper uses.
// Hand-maintained. Machine code is one instruction per line (disassembly in the
// comment, +offset from the start of the block). Check site bytes with
// `SWTFU-Helper verify <decoded.exe>`.
#pragma once
#include "game.h"

static const GameInfo kTfu1Game = {"tfu1", L"swtfu.exe", "1c156705548fce9e33fa3423e75b5cbe36248319b96f4b4850bf2e8125692032", 
    B("\x6a\x00")                // +000: push 0
    + B("\x6a\x00")              // +002: push 0
    + B("\xe8\x07\x02\x00\x00")  // +004: call 0x210
    + B("\x83\xc4\x08")          // +009: add esp, 8
    + B("\xc2\x10\x00")          // +00c: ret 0x10
    + B("\xcc")                  // +00f: int3
, 0x1240440};

// Native FPS profile with investigated cinematic, DMM, and cloth timing fixes.
static const FixedPatch k_tfu1_fps_patches[] = {
    {"cinematic_limit", 0x4DC461, B("\xf3\x0f\x11\x82\x50\x01\x00\x00"), B("\x90\x90\x90\x90\x90\x90\x90\x90")},
    // Lua_SetMatrixFromCameraAnimation quantizes cinematic camera time to
    // floor(30t)/30 before sampling; props use exact time. At 60 the camera
    // steps every 2nd update while ridden ships move every update (judder).
    // NOP the quantizer so xmm0 keeps the exact time, like the prop path.
    {"cinematic_camera_exact_time", 0x4D95F6, 
        B("\x0f\x5a\xc8")                        // +000: cvtps2pd xmm1, xmm0
        + B("\xf2\x0f\x10\x05\x08\x1b\xf5\x00")  // +003: movsd xmm0, qword ptr [0xf51b08]
        + B("\xf2\x0f\x59\xc8")                  // +00b: mulsd xmm1, xmm0
        + B("\xf2\x0f\x58\x0d\x38\x00\xf6\x00")  // +00f: addsd xmm1, qword ptr [0xf60038]
        + B("\xf2\x0f\x2c\xd1")                  // +017: cvttsd2si edx, xmm1
        + B("\xf3\x0f\x2a\xca")                  // +01b: cvtsi2ss xmm1, edx
        + B("\xf3\x0f\x5a\xc9")                  // +01f: cvtss2sd xmm1, xmm1
        + B("\xf2\x0f\x5e\xc8")                  // +023: divsd xmm1, xmm0
        + B("\xf2\x0f\x5a\xc1")                  // +027: cvtsd2ss xmm0, xmm1
    , 
        Bytes(43, '\x90')    // +000: padding (43)
    },
    // ActionDirectLocomotionAI_StrafeNormalNav_Update: turn duration =
    // angle / (rate / gGameplayDt) with rate in 1/30 s ticks; at 60 it halves.
    // Load the 1/30 constant (kOneThirtieth 0xF51B04) instead of gGameplayDt.
    {"ai_strafe_turn_duration", 0x874CC3, B("\xf3\x0f\x10\x2d\x08\x40\x0d\x01"), B("\xf3\x0f\x10\x2d\x04\x1b\xf5\x00")},
    // DMM impact/audio speed calculation divided each frame's deformation by
    // a hardcoded 1/30 s. Use the timestep already supplied to DMM instead.
    {"dmm_impact_interval", 0xC93AD0, B("\xd9\x05\x04\x1b\xf5\x00"), B("\xd9\x05\x08\x40\x0d\x01")},
    // Native strict >30 frame checks mean 31 updates. At 60 FPS, >61 preserves
    // the same 31/30 second contact-message interval (62/60), not half of it.
    {"dmm_contact_interval_a", 0xC69601, B("\x83\xfa\x1e"), B("\x83\xfa\x3d")},
    {"dmm_contact_interval_b", 0xC69629, B("\x83\xfe\x1e"), B("\x83\xfe\x3d")},
    // DMM sleeps a quiet body after two qualifying final-substep updates.
    // Four at 60 Hz retain the original 2/30-second quiet duration. Intermediate
    // substeps cannot advance this counter; zero-dt scene updates also skip it.
    {"dmm_quiet_sleep_duration", 0xD567E4, B("\x3c\x01"), B("\x3c\x03")},
    // Cloth rotation recovery stores 30 updates, then subtracts one per
    // positive-dt update. Keep its duration when the update rate doubles.
    {"cloth_rotation_recovery", 0xBE9F11, B("\xf3\x0f\x10\x05\x10\x1b\xf5\x00"), B("\xf3\x0f\x10\x05\xf8\x22\xf5\x00")},
    // Initial binding hold lasts four updates; start its existing seconds-
    // based release on update nine rather than five in the 60 Hz profile.
    {"cloth_startup_hold_end", 0xBC96F2, B("\x83\xf8\x05"), B("\x83\xf8\x09")},
    {"cloth_startup_release", 0xBC96FA, B("\x83\xf8\x04"), B("\x83\xf8\x08")},
    // After transform discontinuities, inertial force is suppressed for three
    // positive-dt preparations. Preserve its nominal 0.1-second duration.
    {"cloth_motion_suppression", 0xBC8AA4, B("\xc7\x47\x50\x03\x00\x00\x00"), B("\xc7\x47\x50\x06\x00\x00\x00")},
    // DMM sweep suppression decrements once per eligible object update.
    // Constructor, reset and explicit setter all initialize the same counter.
    {"dmm_sweep_delay_reset", 0xC95790, B("\xc7\x86\x60\x15\x00\x00\x3c\x00\x00\x00"), B("\xc7\x86\x60\x15\x00\x00\x78\x00\x00\x00")},
    {"dmm_sweep_delay_set", 0xC957A0, B("\xc7\x81\x60\x15\x00\x00\x3c\x00\x00\x00"), B("\xc7\x81\x60\x15\x00\x00\x78\x00\x00\x00")},
    {"dmm_sweep_delay_initial", 0xC987CC, B("\xc7\x86\x60\x15\x00\x00\x3c\x00\x00\x00"), B("\xc7\x86\x60\x15\x00\x00\x78\x00\x00\x00")},
};

// Preserve native 30 Hz ratio smoothing duration at the selected 60 Hz rate.
static const HookSite k_tfu1_physics_smoothing_sites[] = {
    {"timescale_smoothing", 0x41BD4D, B("\xf3\x0f\x10\x96\xd4\x00\x00\x00")},
    {"physics_smoothing", 0x41BDDD, B("\xf3\x0f\x10\x56\x74")},
};
static const SourceCheck k_tfu1_physics_smoothing_sources[] = {
    {0x0, B("")},
};
static const HookGroup k_tfu1_physics_smoothing = {"physics_smoothing", k_tfu1_physics_smoothing_sites, 2, 
    B("\x9c")                                   // +000: pushfd
    + B("\xf3\x0f\x10\x96\xd4\x00\x00\x00")     // +001: movss xmm2, dword ptr [esi + 0xd4]
    + B("\x81\xbe\x50\x01\x00\x00\x00\x00\x70\x42") // +009: cmp dword ptr [esi + 0x150], 0x42700000
    + B("\x75\x1c")                             // +013: jne 0x31
    + B("\x81\xbe\xd4\x00\x00\x00\x00\x00\x80\x3f") // +015: cmp dword ptr [esi + 0xd4], 0x3f800000
    + B("\x76\x10")                             // +01f: jbe 0x31
    + B("\x81\xbe\xd4\x00\x00\x00\x00\x00\x80\x7f") // +021: cmp dword ptr [esi + 0xd4], 0x7f800000
    + B("\x73\x04")                             // +02b: jae 0x31
    + B("\xf3\x0f\x51\xd2")                     // +02d: sqrtss xmm2, xmm2
    + B("\x9d")                                 // +031: popfd
    + B("\xc3")                                 // +032: ret
    + Bytes(13, '\x90')                         // +033: padding (13)
    + B("\x9c")                                 // +040: pushfd
    + B("\xf3\x0f\x10\x96\x74\x00\x00\x00")     // +041: movss xmm2, dword ptr [esi + 0x74]
    + B("\x81\xbe\x50\x01\x00\x00\x00\x00\x70\x42") // +049: cmp dword ptr [esi + 0x150], 0x42700000
    + B("\x75\x1c")                             // +053: jne 0x71
    + B("\x81\xbe\x74\x00\x00\x00\x00\x00\x80\x3f") // +055: cmp dword ptr [esi + 0x74], 0x3f800000
    + B("\x76\x10")                             // +05f: jbe 0x71
    + B("\x81\xbe\x74\x00\x00\x00\x00\x00\x80\x7f") // +061: cmp dword ptr [esi + 0x74], 0x7f800000
    + B("\x73\x04")                             // +06b: jae 0x71
    + B("\xf3\x0f\x51\xd2")                     // +06d: sqrtss xmm2, xmm2
    + B("\x9d")                                 // +071: popfd
    + B("\xc3")                                 // +072: ret
    + Bytes(13, '\x90')                         // +073: padding (13)
, 64, k_tfu1_physics_smoothing_sources, 0};

// Preserve the native camera lerp response when the cap changes from 30 to 60.
static const HookSite k_tfu1_camera_smoothing_sites[] = {
    {"camera_eye_xz", 0x455C85, B("\xf3\x0f\x10\x64\x24\x50")},
    {"camera_eye_y", 0x455CAE, B("\xf3\x0f\x10\x4c\x24\x50")},
    {"camera_target_xz", 0x45623D, B("\xf3\x0f\x10\x54\x24\x30")},
    {"camera_target_y", 0x4562A3, B("\xf3\x0f\x10\x44\x24\x30")},
};
static const SourceCheck k_tfu1_camera_smoothing_sources[] = {
    {0x0, B("")},
};
static const HookGroup k_tfu1_camera_smoothing = {"camera_smoothing", k_tfu1_camera_smoothing_sites, 4, 
    B("\xf3\x0f\x10\x64\x24\x54")               // +000: movss xmm4, dword ptr [esp + 0x54]
    + B("\x9c")                                 // +006: pushfd
    + B("\x50")                                 // +007: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +008: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +00d: test eax, eax
    + B("\x74\x38")                             // +00f: je 0x49
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +011: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x2c")                             // +01b: jne 0x49
    + B("\x83\xec\x10")                         // +01d: sub esp, 0x10
    + B("\x0f\x11\x3c\x24")                     // +020: movups xmmword ptr [esp], xmm7
    + B("\x68\x00\x00\x80\x3f")                 // +024: push 0x3f800000
    + B("\xf3\x0f\x10\x3c\x24")                 // +029: movss xmm7, dword ptr [esp]
    + B("\xf3\x0f\x5c\xfc")                     // +02e: subss xmm7, xmm4
    + B("\xf3\x0f\x51\xff")                     // +032: sqrtss xmm7, xmm7
    + B("\xf3\x0f\x10\x24\x24")                 // +036: movss xmm4, dword ptr [esp]
    + B("\xf3\x0f\x5c\xe7")                     // +03b: subss xmm4, xmm7
    + B("\x83\xc4\x04")                         // +03f: add esp, 4
    + B("\x0f\x10\x3c\x24")                     // +042: movups xmm7, xmmword ptr [esp]
    + B("\x83\xc4\x10")                         // +046: add esp, 0x10
    + B("\x58")                                 // +049: pop eax
    + B("\x9d")                                 // +04a: popfd
    + B("\xc3")                                 // +04b: ret
    + Bytes(52, '\x90')                         // +04c: padding (52)
    + B("\xf3\x0f\x10\x4c\x24\x54")             // +080: movss xmm1, dword ptr [esp + 0x54]
    + B("\x9c")                                 // +086: pushfd
    + B("\x50")                                 // +087: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +088: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +08d: test eax, eax
    + B("\x74\x38")                             // +08f: je 0xc9
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +091: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x2c")                             // +09b: jne 0xc9
    + B("\x83\xec\x10")                         // +09d: sub esp, 0x10
    + B("\x0f\x11\x3c\x24")                     // +0a0: movups xmmword ptr [esp], xmm7
    + B("\x68\x00\x00\x80\x3f")                 // +0a4: push 0x3f800000
    + B("\xf3\x0f\x10\x3c\x24")                 // +0a9: movss xmm7, dword ptr [esp]
    + B("\xf3\x0f\x5c\xf9")                     // +0ae: subss xmm7, xmm1
    + B("\xf3\x0f\x51\xff")                     // +0b2: sqrtss xmm7, xmm7
    + B("\xf3\x0f\x10\x0c\x24")                 // +0b6: movss xmm1, dword ptr [esp]
    + B("\xf3\x0f\x5c\xcf")                     // +0bb: subss xmm1, xmm7
    + B("\x83\xc4\x04")                         // +0bf: add esp, 4
    + B("\x0f\x10\x3c\x24")                     // +0c2: movups xmm7, xmmword ptr [esp]
    + B("\x83\xc4\x10")                         // +0c6: add esp, 0x10
    + B("\x58")                                 // +0c9: pop eax
    + B("\x9d")                                 // +0ca: popfd
    + B("\xc3")                                 // +0cb: ret
    + Bytes(52, '\x90')                         // +0cc: padding (52)
    + B("\xf3\x0f\x10\x54\x24\x34")             // +100: movss xmm2, dword ptr [esp + 0x34]
    + B("\x9c")                                 // +106: pushfd
    + B("\x50")                                 // +107: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +108: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +10d: test eax, eax
    + B("\x74\x38")                             // +10f: je 0x149
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +111: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x2c")                             // +11b: jne 0x149
    + B("\x83\xec\x10")                         // +11d: sub esp, 0x10
    + B("\x0f\x11\x3c\x24")                     // +120: movups xmmword ptr [esp], xmm7
    + B("\x68\x00\x00\x80\x3f")                 // +124: push 0x3f800000
    + B("\xf3\x0f\x10\x3c\x24")                 // +129: movss xmm7, dword ptr [esp]
    + B("\xf3\x0f\x5c\xfa")                     // +12e: subss xmm7, xmm2
    + B("\xf3\x0f\x51\xff")                     // +132: sqrtss xmm7, xmm7
    + B("\xf3\x0f\x10\x14\x24")                 // +136: movss xmm2, dword ptr [esp]
    + B("\xf3\x0f\x5c\xd7")                     // +13b: subss xmm2, xmm7
    + B("\x83\xc4\x04")                         // +13f: add esp, 4
    + B("\x0f\x10\x3c\x24")                     // +142: movups xmm7, xmmword ptr [esp]
    + B("\x83\xc4\x10")                         // +146: add esp, 0x10
    + B("\x58")                                 // +149: pop eax
    + B("\x9d")                                 // +14a: popfd
    + B("\xc3")                                 // +14b: ret
    + Bytes(52, '\x90')                         // +14c: padding (52)
    + B("\xf3\x0f\x10\x44\x24\x34")             // +180: movss xmm0, dword ptr [esp + 0x34]
    + B("\x9c")                                 // +186: pushfd
    + B("\x50")                                 // +187: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +188: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +18d: test eax, eax
    + B("\x74\x38")                             // +18f: je 0x1c9
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +191: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x2c")                             // +19b: jne 0x1c9
    + B("\x83\xec\x10")                         // +19d: sub esp, 0x10
    + B("\x0f\x11\x3c\x24")                     // +1a0: movups xmmword ptr [esp], xmm7
    + B("\x68\x00\x00\x80\x3f")                 // +1a4: push 0x3f800000
    + B("\xf3\x0f\x10\x3c\x24")                 // +1a9: movss xmm7, dword ptr [esp]
    + B("\xf3\x0f\x5c\xf8")                     // +1ae: subss xmm7, xmm0
    + B("\xf3\x0f\x51\xff")                     // +1b2: sqrtss xmm7, xmm7
    + B("\xf3\x0f\x10\x04\x24")                 // +1b6: movss xmm0, dword ptr [esp]
    + B("\xf3\x0f\x5c\xc7")                     // +1bb: subss xmm0, xmm7
    + B("\x83\xc4\x04")                         // +1bf: add esp, 4
    + B("\x0f\x10\x3c\x24")                     // +1c2: movups xmm7, xmmword ptr [esp]
    + B("\x83\xc4\x10")                         // +1c6: add esp, 0x10
    + B("\x58")                                 // +1c9: pop eax
    + B("\x9d")                                 // +1ca: popfd
    + B("\xc3")                                 // +1cb: ret
    + Bytes(52, '\x90')                         // +1cc: padding (52)
, 128, k_tfu1_camera_smoothing_sources, 0};

// Preserve nominal 30 Hz mouse response and angular filter duration at 60 Hz.
static const HookSite k_tfu1_input_timing_sites[] = {
    {"mouse_horizontal_timing", 0xC34CE8, B("\xe8\x73\x92\xa6\xff")},
    {"mouse_vertical_timing", 0xC34EA8, B("\xe8\xc3\x90\xa6\xff")},
    {"angular_yaw_smoothing", 0x7AEA90, B("\xf3\x0f\x10\x96\x14\x56\x00\x00")},
    {"angular_pitch_smoothing", 0x7AEB87, B("\xf3\x0f\x10\x86\x08\x56\x00\x00")},
};
static const SourceCheck k_tfu1_input_timing_sources[] = {
    {0x69DF60, B("\x8b\x81\xe0\x0c\x00\x00\xd9\x40\x08\xc3")},
    {0x69DF70, B("\x8b\x81\xe0\x0c\x00\x00\xd9\x40\x0c\xc3")},
};
static const HookGroup k_tfu1_input_timing = {"input_timing", k_tfu1_input_timing_sites, 4, 
    B("\x8b\x81\xe0\x0c\x00\x00")               // +000: mov eax, dword ptr [ecx + 0xce0]
    + B("\xd9\x40\x08")                         // +006: fld dword ptr [eax + 8]
    + B("\x9c")                                 // +009: pushfd
    + B("\x50")                                 // +00a: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +00b: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +010: test eax, eax
    + B("\x74\x2e")                             // +012: je 0x42
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +014: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x22")                             // +01e: jne 0x42
    + B("\xa1\x0c\x40\x0d\x01")                 // +020: mov eax, dword ptr [0x10d400c]
    + B("\x85\xc0")                             // +025: test eax, eax
    + B("\x74\x17")                             // +027: je 0x40
    + B("\x3d\x00\x00\x80\x7f")                 // +029: cmp eax, 0x7f800000
    + B("\x73\x10")                             // +02e: jae 0x40
    + B("\xd9\x05\x04\x1b\xf5\x00")             // +030: fld dword ptr [0xf51b04]
    + B("\xd8\x35\x0c\x40\x0d\x01")             // +036: fdiv dword ptr [0x10d400c]
    + B("\xde\xc9")                             // +03c: fmulp st(1)
    + B("\xeb\x02")                             // +03e: jmp 0x42
    + B("\xd8\xc0")                             // +040: fadd st(0)
    + B("\x58")                                 // +042: pop eax
    + B("\x9d")                                 // +043: popfd
    + B("\xc3")                                 // +044: ret
    + Bytes(59, '\x90')                         // +045: padding (59)
    + B("\x8b\x81\xe0\x0c\x00\x00")             // +080: mov eax, dword ptr [ecx + 0xce0]
    + B("\xd9\x40\x0c")                         // +086: fld dword ptr [eax + 0xc]
    + B("\x9c")                                 // +089: pushfd
    + B("\x50")                                 // +08a: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +08b: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +090: test eax, eax
    + B("\x74\x2e")                             // +092: je 0xc2
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +094: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x22")                             // +09e: jne 0xc2
    + B("\xa1\x0c\x40\x0d\x01")                 // +0a0: mov eax, dword ptr [0x10d400c]
    + B("\x85\xc0")                             // +0a5: test eax, eax
    + B("\x74\x17")                             // +0a7: je 0xc0
    + B("\x3d\x00\x00\x80\x7f")                 // +0a9: cmp eax, 0x7f800000
    + B("\x73\x10")                             // +0ae: jae 0xc0
    + B("\xd9\x05\x04\x1b\xf5\x00")             // +0b0: fld dword ptr [0xf51b04]
    + B("\xd8\x35\x0c\x40\x0d\x01")             // +0b6: fdiv dword ptr [0x10d400c]
    + B("\xde\xc9")                             // +0bc: fmulp st(1)
    + B("\xeb\x02")                             // +0be: jmp 0xc2
    + B("\xd8\xc0")                             // +0c0: fadd st(0)
    + B("\x58")                                 // +0c2: pop eax
    + B("\x9d")                                 // +0c3: popfd
    + B("\xc3")                                 // +0c4: ret
    + Bytes(59, '\x90')                         // +0c5: padding (59)
    + B("\xf3\x0f\x10\x96\x14\x56\x00\x00")     // +100: movss xmm2, dword ptr [esi + 0x5614]
    + B("\x9c")                                 // +108: pushfd
    + B("\x50")                                 // +109: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +10a: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +10f: test eax, eax
    + B("\x74\x27")                             // +111: je 0x13a
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +113: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x1b")                             // +11d: jne 0x13a
    + B("\x66\x0f\x7e\xd8")                     // +11f: movd eax, xmm3
    + B("\x85\xc0")                             // +123: test eax, eax
    + B("\x74\x13")                             // +125: je 0x13a
    + B("\x3d\x00\x00\x80\x3f")                 // +127: cmp eax, 0x3f800000
    + B("\x77\x0c")                             // +12c: ja 0x13a
    + B("\xf6\x05\x64\x04\x24\x01\x01")         // +12e: test byte ptr [0x1240464], 1
    + B("\x74\x03")                             // +135: je 0x13a
    + B("\x0f\x57\xdb")                         // +137: xorps xmm3, xmm3
    + B("\x58")                                 // +13a: pop eax
    + B("\x9d")                                 // +13b: popfd
    + B("\xc3")                                 // +13c: ret
    + Bytes(67, '\x90')                         // +13d: padding (67)
    + B("\xf3\x0f\x10\x86\x08\x56\x00\x00")     // +180: movss xmm0, dword ptr [esi + 0x5608]
    + B("\x9c")                                 // +188: pushfd
    + B("\x50")                                 // +189: push eax
    + B("\xa1\x40\x04\x24\x01")                 // +18a: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                             // +18f: test eax, eax
    + B("\x74\x27")                             // +191: je 0x1ba
    + B("\x81\xb8\x50\x01\x00\x00\x00\x00\x70\x42") // +193: cmp dword ptr [eax + 0x150], 0x42700000
    + B("\x75\x1b")                             // +19d: jne 0x1ba
    + B("\x66\x0f\x7e\xd8")                     // +19f: movd eax, xmm3
    + B("\x85\xc0")                             // +1a3: test eax, eax
    + B("\x74\x13")                             // +1a5: je 0x1ba
    + B("\x3d\x00\x00\x80\x3f")                 // +1a7: cmp eax, 0x3f800000
    + B("\x77\x0c")                             // +1ac: ja 0x1ba
    + B("\xf6\x05\x64\x04\x24\x01\x01")         // +1ae: test byte ptr [0x1240464], 1
    + B("\x74\x03")                             // +1b5: je 0x1ba
    + B("\x0f\x57\xdb")                         // +1b7: xorps xmm3, xmm3
    + B("\x58")                                 // +1ba: pop eax
    + B("\x9d")                                 // +1bb: popfd
    + B("\xc3")                                 // +1bc: ret
    + Bytes(67, '\x90')                         // +1bd: padding (67)
, 128, k_tfu1_input_timing_sources, 2};

// Scale per-update amounts by 30 / cap so they keep their 30 Hz rate at 60.
//
// Each site is a movss load of a per-update amount. It is replaced by a call to a
// helper that replays the load and multiplies the register by 30 / cap
// (x1 at 30 including cinematics, x0.5 at 60).
// - Trail_AdvanceUVScroll (0x4677D0), Trail_SimStep (0x469280): UV offset (+0xE74)
//   += mUV_Scroll_Speed (+0xE78) once per update, no frame time.
// - PlayerSith_SteerAccum_Update (0x7B3AD0): Force steering accumulates rate * input per update.
// - ActionDirectLocomotionAI_StrafeNormalNav_Update (0x874A40): turn step capped
//   at a fixed 5 degrees per update (0xFB9154) before sub_872830.
static const HookSite k_tfu1_update_rate_sites[] = {
    {"trail_uv_scroll_advance", 0x46781D, B("\xf3\x0f\x10\x81\x78\x0e\x00\x00")},
    {"trail_uv_scroll_simstep", 0x4692D2, B("\xf3\x0f\x10\x86\x78\x0e\x00\x00")},
    {"ai_strafe_turn_step_cap", 0x874BBE, B("\xf3\x0f\x10\x15\x54\x91\xfb\x00")},
    {"force_steer_rate", 0x7B3B5C, B("\xf3\x0f\x10\x8e\x20\x58\x00\x00")},
};
static const SourceCheck k_tfu1_update_rate_sources[] = {
    {0x0, B("")},
};
static const HookGroup k_tfu1_update_rate = {"update_rate", k_tfu1_update_rate_sites, 4, 
    B("\xf3\x0f\x10\x81\x78\x0e\x00\x00")    // +000: movss xmm0, dword ptr [ecx + 0xe78]
    + B("\x9c")                              // +008: pushfd
    + B("\x50")                              // +009: push eax
    + B("\xa1\x40\x04\x24\x01")              // +00a: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                          // +00f: test eax, eax
    + B("\x74\x19")                          // +011: je 0x2c
    + B("\x83\xb8\x50\x01\x00\x00\x00")      // +013: cmp dword ptr [eax + 0x150], 0
    + B("\x74\x10")                          // +01a: je 0x2c
    + B("\xf3\x0f\x59\x05\x10\x1b\xf5\x00")  // +01c: mulss xmm0, dword ptr [0xf51b10]
    + B("\xf3\x0f\x5e\x80\x50\x01\x00\x00")  // +024: divss xmm0, dword ptr [eax + 0x150]
    + B("\x58")                              // +02c: pop eax
    + B("\x9d")                              // +02d: popfd
    + B("\xc3")                              // +02e: ret
    + Bytes(17, '\xcc')                      // +02f: padding (17)
    + B("\xf3\x0f\x10\x86\x78\x0e\x00\x00")  // +040: movss xmm0, dword ptr [esi + 0xe78]
    + B("\x9c")                              // +048: pushfd
    + B("\x50")                              // +049: push eax
    + B("\xa1\x40\x04\x24\x01")              // +04a: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                          // +04f: test eax, eax
    + B("\x74\x19")                          // +051: je 0x6c
    + B("\x83\xb8\x50\x01\x00\x00\x00")      // +053: cmp dword ptr [eax + 0x150], 0
    + B("\x74\x10")                          // +05a: je 0x6c
    + B("\xf3\x0f\x59\x05\x10\x1b\xf5\x00")  // +05c: mulss xmm0, dword ptr [0xf51b10]
    + B("\xf3\x0f\x5e\x80\x50\x01\x00\x00")  // +064: divss xmm0, dword ptr [eax + 0x150]
    + B("\x58")                              // +06c: pop eax
    + B("\x9d")                              // +06d: popfd
    + B("\xc3")                              // +06e: ret
    + Bytes(17, '\xcc')                      // +06f: padding (17)
    + B("\xf3\x0f\x10\x15\x54\x91\xfb\x00")  // +080: movss xmm2, dword ptr [0xfb9154]
    + B("\x9c")                              // +088: pushfd
    + B("\x50")                              // +089: push eax
    + B("\xa1\x40\x04\x24\x01")              // +08a: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                          // +08f: test eax, eax
    + B("\x74\x19")                          // +091: je 0xac
    + B("\x83\xb8\x50\x01\x00\x00\x00")      // +093: cmp dword ptr [eax + 0x150], 0
    + B("\x74\x10")                          // +09a: je 0xac
    + B("\xf3\x0f\x59\x15\x10\x1b\xf5\x00")  // +09c: mulss xmm2, dword ptr [0xf51b10]
    + B("\xf3\x0f\x5e\x90\x50\x01\x00\x00")  // +0a4: divss xmm2, dword ptr [eax + 0x150]
    + B("\x58")                              // +0ac: pop eax
    + B("\x9d")                              // +0ad: popfd
    + B("\xc3")                              // +0ae: ret
    + Bytes(17, '\xcc')                      // +0af: padding (17)
    + B("\xf3\x0f\x10\x8e\x20\x58\x00\x00")  // +0c0: movss xmm1, dword ptr [esi + 0x5820]
    + B("\x9c")                              // +0c8: pushfd
    + B("\x50")                              // +0c9: push eax
    + B("\xa1\x40\x04\x24\x01")              // +0ca: mov eax, dword ptr [0x1240440]
    + B("\x85\xc0")                          // +0cf: test eax, eax
    + B("\x74\x19")                          // +0d1: je 0xec
    + B("\x83\xb8\x50\x01\x00\x00\x00")      // +0d3: cmp dword ptr [eax + 0x150], 0
    + B("\x74\x10")                          // +0da: je 0xec
    + B("\xf3\x0f\x59\x0d\x10\x1b\xf5\x00")  // +0dc: mulss xmm1, dword ptr [0xf51b10]
    + B("\xf3\x0f\x5e\x88\x50\x01\x00\x00")  // +0e4: divss xmm1, dword ptr [eax + 0x150]
    + B("\x58")                              // +0ec: pop eax
    + B("\x9d")                              // +0ed: popfd
    + B("\xc3")                              // +0ee: ret
    + Bytes(17, '\xcc')                      // +0ef: padding (17)
, 64, k_tfu1_update_rate_sources, 0};
static const HookGroup* const k_tfu1_fps_helpers[] = {&k_tfu1_physics_smoothing, &k_tfu1_camera_smoothing, &k_tfu1_input_timing, &k_tfu1_update_rate};
static const FpsProfile kTfu1Fps = {k_tfu1_fps_patches, 14, k_tfu1_fps_helpers, 4, 0x150, 0x12A1EF4, 0x144};

// Controller reconnect and launch-movie skip (runtime only; game files unchanged).
//
// Reconnect: sub_6A2940 polls XInput pad 0 every frame. When it fails, sub_6B8CD0
// latches a DirectInput fallback flag ([[input+0xCDC]]) that is never cleared.
// From then on the DirectInput branch runs after every successful XInput read
// and overwrites it (zeros when the stale device cannot be acquired), so a
// reconnected pad is read but ignored. Hook: after a successful XInput read,
// return through the native epilogue instead of entering the fallback branch.
// Controllers without XInput still use the DirectInput path unchanged.
//
// Launch-movie skip: any key/mouse/pad press already requests a skip, but the
// load-time movie runner sub_419080 marks movies skippable only after loading
// finishes, and never while booting into the main menu (byte_10D4006). The launch
// movies end about when that load finishes, so they never skip. Hook the runner's
// per-frame SetSkippable call: while booting, every movie except the loading-screen
// loop is skippable immediately. Level-load (story) movies keep the original rule.
static const uint32_t kPadSite = 0x6A2A80;
static const Bytes kPadOriginal = B("\x53\x8b\xce\xff\xd2\xeb\x6d");
static const uint32_t kPadEpilogue = 0x6A2C6C;
static const Bytes kPadHead = B("\x53\x8b\xce\xff\xd2\x8b\x86\xdc\x0c\x00\x00");
static const uint32_t kSkipSite = 0x4190E6;
static const Bytes kSkipOriginal = B("\x8b\x4c\x24\x1c\x8b\x10\x8b\x52\x48\x51\x8b\xc8\xff\xd2");
static const uint32_t kSkipOffset = 64;
static const Bytes kSkipCode = 
    B("\x8b\x4c\x24\x20")                // +000: mov ecx, dword ptr [esp + 0x20]
    + B("\x80\x3d\x06\x40\x0d\x01\x00")  // +004: cmp byte ptr [0x10d4006], 0
    + B("\x74\x09")                      // +00b: je 0x16
    + B("\x80\x7c\x24\x18\x00")          // +00d: cmp byte ptr [esp + 0x18], 0
    + B("\x75\x02")                      // +012: jne 0x16
    + B("\xb1\x01")                      // +014: mov cl, 1
    + B("\x8b\x10")                      // +016: mov edx, dword ptr [eax]
    + B("\x8b\x52\x48")                  // +018: mov edx, dword ptr [edx + 0x48]
    + B("\x51")                          // +01b: push ecx
    + B("\x8b\xc8")                      // +01c: mov ecx, eax
    + B("\xff\xd2")                      // +01e: call edx
    + B("\xc3")                          // +020: ret
;


// Pump the window queue on the owning thread during synchronous movies.
//
// The native movie worker renders independently, while its caller sleeps in
// sub_415C80 without returning to WinMain's message pump. Preserve its sleep,
// audio updates and worker lifetime; process a bounded batch of messages there.
static const uint32_t kMovieAddress = 0x415D41;
static const Bytes kMovieOriginal = B("\x8b\x35\x40\x49\xf3\x00");
static const Bytes kMovieCode = 
    B("\x55")                            // +000: push ebp
    + B("\x8b\xec")                      // +001: mov ebp, esp
    + B("\x83\xec\x20")                  // +003: sub esp, 0x20
    + B("\x53")                          // +006: push ebx
    + B("\x56")                          // +007: push esi
    + B("\xff\x75\x08")                  // +008: push dword ptr [ebp + 8]
    + B("\xff\x15\x40\x49\xf3\x00")      // +00b: call dword ptr [0xf34940]
    + B("\x83\xc4\x04")                  // +011: add esp, 4
    + B("\xbb\x40\x00\x00\x00")          // +014: mov ebx, 0x40
    + B("\x8d\x75\xe0")                  // +019: lea esi, [ebp - 0x20]
    + B("\x6a\x01")                      // +01c: push 1
    + B("\x6a\x00")                      // +01e: push 0
    + B("\x6a\x00")                      // +020: push 0
    + B("\x6a\x00")                      // +022: push 0
    + B("\x56")                          // +024: push esi
    + B("\xff\x15\x64\x4d\xf3\x00")      // +025: call dword ptr [0xf34d64]
    + B("\x85\xc0")                      // +02b: test eax, eax
    + B("\x74\x3a")                      // +02d: je 0x69
    + B("\x83\x7e\x04\x12")              // +02f: cmp dword ptr [esi + 4], 0x12
    + B("\x75\x0e")                      // +033: jne 0x43
    + B("\xff\x76\x08")                  // +035: push dword ptr [esi + 8]
    + B("\xff\x15\x0c\x40\xf3\x00")      // +038: call dword ptr [0xf3400c]
    + B("\x83\xc4\x04")                  // +03e: add esp, 4
    + B("\xeb\x26")                      // +041: jmp 0x69
    + B("\x81\x7e\x04\x04\x01\x00\x00")  // +043: cmp dword ptr [esi + 4], 0x104
    + B("\x75\x0c")                      // +04a: jne 0x58
    + B("\x83\x7e\x08\x79")              // +04c: cmp dword ptr [esi + 8], 0x79
    + B("\x74\x14")                      // +050: je 0x66
    + B("\x83\x7e\x08\x12")              // +052: cmp dword ptr [esi + 8], 0x12
    + B("\x74\x0e")                      // +056: je 0x66
    + B("\x56")                          // +058: push esi
    + B("\xff\x15\x68\x4d\xf3\x00")      // +059: call dword ptr [0xf34d68]
    + B("\x56")                          // +05f: push esi
    + B("\xff\x15\x6c\x4d\xf3\x00")      // +060: call dword ptr [0xf34d6c]
    + B("\x4b")                          // +066: dec ebx
    + B("\x75\xb3")                      // +067: jne 0x1c
    + B("\x8b\x0d\x40\x04\x24\x01")      // +069: mov ecx, dword ptr [0x1240440]
    + B("\x85\xc9")                      // +06f: test ecx, ecx
    + B("\x74\x0d")                      // +071: je 0x80
    + B("\x8b\x09")                      // +073: mov ecx, dword ptr [ecx]
    + B("\x85\xc9")                      // +075: test ecx, ecx
    + B("\x74\x07")                      // +077: je 0x80
    + B("\xb8\x00\xda\x69\x00")          // +079: mov eax, 0x69da00
    + B("\xff\xd0")                      // +07e: call eax
    + B("\x5e")                          // +080: pop esi
    + B("\x5b")                          // +081: pop ebx
    + B("\xc9")                          // +082: leave
    + B("\xc3")                          // +083: ret
    + Bytes(28, '\x90')                  // +084: padding (28)
    + B("\x55")                          // +0a0: push ebp
    + B("\x8b\xec")                      // +0a1: mov ebp, esp
    + B("\x53")                          // +0a3: push ebx
    + B("\x8b\x5d\x08")                  // +0a4: mov ebx, dword ptr [ebp + 8]
    + B("\x85\xdb")                      // +0a7: test ebx, ebx
    + B("\x74\x1a")                      // +0a9: je 0xc5
    + B("\x53")                          // +0ab: push ebx
    + B("\xff\x15\xfc\x4a\xf3\x00")      // +0ac: call dword ptr [0xf34afc]
    + B("\x83\xc4\x04")                  // +0b2: add esp, 4
    + B("\x84\xc0")                      // +0b5: test al, al
    + B("\x75\x0c")                      // +0b7: jne 0xc5
    + B("\x6a\x0a")                      // +0b9: push 0xa
    + B("\xe8\x40\xff\xff\xff")          // +0bb: call 0
    + B("\x83\xc4\x04")                  // +0c0: add esp, 4
    + B("\xeb\xe6")                      // +0c3: jmp 0xab
    + B("\x53")                          // +0c5: push ebx
    + B("\xff\x15\xb8\x4b\xf3\x00")      // +0c6: call dword ptr [0xf34bb8]
    + B("\x83\xc4\x04")                  // +0cc: add esp, 4
    + B("\x5b")                          // +0cf: pop ebx
    + B("\xc9")                          // +0d0: leave
    + B("\xc3")                          // +0d1: ret
    + Bytes(14, '\x90')                  // +0d2: padding (14)
;
static const uint32_t kMovieWaitOffset = 160;
static const uint32_t kMovieWaitSites[] = {0x40726D, 0x415CA6};
static const Bytes kMovieWaitOriginal = B("\xff\x15\xb8\x4b\xf3\x00");


// Keep the current subtitle line across the pause menu (original game bug, any FPS).
//
// Subtitles: ui.ShowSubtitle (sub_4DE860) -> UI_ShowSubtitleText (0x541A20) sets
// strSubtitle/subtitleDuration on the visible "UIContract" element; the HUD page
// then counts the duration on the UI clock. Opening the pause menu (Anark event
// onPauseGame -> sub_53F650(1), pause byte 0x12A288D) hides it, and the line is
// gone when the game resumes, although the dialogue resumes with it.
//
// Fix (one helper page + one data page):
// - show:  record text, duration and start time (KUSER_SHARED_DATA interrupt time).
// - hide:  ui.HideSubtitle (sub_541AC0) forgets the line; while paused it is skipped
//          (the dialogue engine hides lines when their audio stops, and pause stops it).
// - pause: on sub_53F650(1) keep the remaining time; on (0) queue a re-show with
//          the time left at pause (paused time excluded).
// - frame: before UI_FrameUpdate in Frame_UpdateWorld (0x418B97), once unpaused,
//          call UI_ShowSubtitleText(text, remaining) (same thread as Lua). Re-showing
//          while the menu is up was tried and is the suspect of a game crash.
// The code is position independent; DATA_SLOT holds the data page address.
static const Bytes kSubtitleCode = 
    B("\x60")                            // +000: pushal
    + B("\xe8\x00\x00\x00\x00")          // +001: call 6
    + B("\x5b")                          // +006: pop ebx
    + B("\x8b\x9b\xfa\x02\x00\x00")      // +007: mov ebx, dword ptr [ebx + 0x2fa]
    + B("\x8b\x74\x24\x24")              // +00d: mov esi, dword ptr [esp + 0x24]
    + B("\x8d\x7b\x20")                  // +011: lea edi, [ebx + 0x20]
    + B("\x39\xfe")                      // +014: cmp esi, edi
    + B("\x74\x14")                      // +016: je 0x2c
    + B("\xc6\x43\x01\x00")              // +018: mov byte ptr [ebx + 1], 0
    + B("\xb9\xff\x00\x00\x00")          // +01c: mov ecx, 0xff
    + B("\xac")                          // +021: lodsb al, byte ptr [esi]
    + B("\xaa")                          // +022: stosb byte ptr es:[edi], al
    + B("\x84\xc0")                      // +023: test al, al
    + B("\x74\x05")                      // +025: je 0x2c
    + B("\xe2\xf8")                      // +027: loop 0x21
    + B("\xc6\x07\x00")                  // +029: mov byte ptr [edi], 0
    + B("\x8b\x44\x24\x28")              // +02c: mov eax, dword ptr [esp + 0x28]
    + B("\x89\x43\x04")                  // +030: mov dword ptr [ebx + 4], eax
    + B("\x8b\x15\x0c\x00\xfe\x7f")      // +033: mov edx, dword ptr [0x7ffe000c]
    + B("\xa1\x08\x00\xfe\x7f")          // +039: mov eax, dword ptr [0x7ffe0008]
    + B("\x3b\x15\x10\x00\xfe\x7f")      // +03e: cmp edx, dword ptr [0x7ffe0010]
    + B("\x75\xed")                      // +044: jne 0x33
    + B("\x89\x43\x08")                  // +046: mov dword ptr [ebx + 8], eax
    + B("\x89\x53\x0c")                  // +049: mov dword ptr [ebx + 0xc], edx
    + B("\xc6\x03\x01")                  // +04c: mov byte ptr [ebx], 1
    + B("\x61")                          // +04f: popal
    + B("\x56")                          // +050: push esi
    + B("\x8b\x35\xb0\x43\xf3\x00")      // +051: mov esi, dword ptr [0xf343b0]
    + B("\x68\x27\x1a\x54\x00")          // +057: push 0x541a27
    + B("\xc3")                          // +05c: ret
    + Bytes(99, '\x90')                  // +05d: padding (99)
    + B("\x53")                          // +0c0: push ebx
    + B("\xe8\x00\x00\x00\x00")          // +0c1: call 0xc6
    + B("\x5b")                          // +0c6: pop ebx
    + B("\x8b\x9b\x3a\x02\x00\x00")      // +0c7: mov ebx, dword ptr [ebx + 0x23a]
    + B("\x80\x3d\x8d\x28\x2a\x01\x00")  // +0cd: cmp byte ptr [0x12a288d], 0
    + B("\x75\x11")                      // +0d4: jne 0xe7
    + B("\x66\xc7\x03\x00\x00")          // +0d6: mov word ptr [ebx], 0
    + B("\x5b")                          // +0db: pop ebx
    + B("\xa1\xf4\xbb\x0d\x01")          // +0dc: mov eax, dword ptr [0x10dbbf4]
    + B("\x68\xc5\x1a\x54\x00")          // +0e1: push 0x541ac5
    + B("\xc3")                          // +0e6: ret
    + B("\xfe\x43\x02")                  // +0e7: inc byte ptr [ebx + 2]
    + B("\x5b")                          // +0ea: pop ebx
    + B("\xc3")                          // +0eb: ret
    + Bytes(148, '\x90')                 // +0ec: padding (148)
    + B("\x60")                          // +180: pushal
    + B("\xe8\x00\x00\x00\x00")          // +181: call 0x186
    + B("\x5b")                          // +186: pop ebx
    + B("\x8b\x9b\x7a\x01\x00\x00")      // +187: mov ebx, dword ptr [ebx + 0x17a]
    + B("\x8a\x4c\x24\x24")              // +18d: mov cl, byte ptr [esp + 0x24]
    + B("\x84\xc9")                      // +191: test cl, cl
    + B("\x74\x5e")                      // +193: je 0x1f3
    + B("\x80\x3b\x01")                  // +195: cmp byte ptr [ebx], 1
    + B("\x75\x6c")                      // +198: jne 0x206
    + B("\x80\x7b\x01\x00")              // +19a: cmp byte ptr [ebx + 1], 0
    + B("\x75\x66")                      // +19e: jne 0x206
    + B("\x8b\x15\x0c\x00\xfe\x7f")      // +1a0: mov edx, dword ptr [0x7ffe000c]
    + B("\xa1\x08\x00\xfe\x7f")          // +1a6: mov eax, dword ptr [0x7ffe0008]
    + B("\x3b\x15\x10\x00\xfe\x7f")      // +1ab: cmp edx, dword ptr [0x7ffe0010]
    + B("\x75\xed")                      // +1b1: jne 0x1a0
    + B("\x2b\x43\x08")                  // +1b3: sub eax, dword ptr [ebx + 8]
    + B("\x1b\x53\x0c")                  // +1b6: sbb edx, dword ptr [ebx + 0xc]
    + B("\x89\x43\x14")                  // +1b9: mov dword ptr [ebx + 0x14], eax
    + B("\x89\x53\x18")                  // +1bc: mov dword ptr [ebx + 0x18], edx
    + B("\xdf\x6b\x14")                  // +1bf: fild qword ptr [ebx + 0x14]
    + B("\x68\x95\xbf\xd6\x33")          // +1c2: push 0x33d6bf95
    + B("\xd8\x0c\x24")                  // +1c7: fmul dword ptr [esp]
    + B("\x58")                          // +1ca: pop eax
    + B("\xd8\x6b\x04")                  // +1cb: fsubr dword ptr [ebx + 4]
    + B("\xd9\x53\x10")                  // +1ce: fst dword ptr [ebx + 0x10]
    + B("\x68\xcd\xcc\xcc\x3d")          // +1d1: push 0x3dcccccd
    + B("\xd8\x1c\x24")                  // +1d6: fcomp dword ptr [esp]
    + B("\xdf\xe0")                      // +1d9: fnstsw ax
    + B("\x59")                          // +1db: pop ecx
    + B("\x9e")                          // +1dc: sahf
    + B("\x76\x0f")                      // +1dd: jbe 0x1ee
    + B("\x8b\x43\x10")                  // +1df: mov eax, dword ptr [ebx + 0x10]
    + B("\x89\x43\x1c")                  // +1e2: mov dword ptr [ebx + 0x1c], eax
    + B("\xc6\x43\x01\x01")              // +1e5: mov byte ptr [ebx + 1], 1
    + B("\xc6\x03\x00")                  // +1e9: mov byte ptr [ebx], 0
    + B("\xeb\x18")                      // +1ec: jmp 0x206
    + B("\xc6\x03\x00")                  // +1ee: mov byte ptr [ebx], 0
    + B("\xeb\x13")                      // +1f1: jmp 0x206
    + B("\x80\x7b\x01\x00")              // +1f3: cmp byte ptr [ebx + 1], 0
    + B("\x74\x0d")                      // +1f7: je 0x206
    + B("\xc6\x43\x01\x00")              // +1f9: mov byte ptr [ebx + 1], 0
    + B("\x8b\x43\x1c")                  // +1fd: mov eax, dword ptr [ebx + 0x1c]
    + B("\x89\x43\x10")                  // +200: mov dword ptr [ebx + 0x10], eax
    + B("\xc6\x03\x03")                  // +203: mov byte ptr [ebx], 3
    + B("\x61")                          // +206: popal
    + B("\x8a\x4c\x24\x04")              // +207: mov cl, byte ptr [esp + 4]
    + B("\xa1\x40\x04\x24\x01")          // +20b: mov eax, dword ptr [0x1240440]
    + B("\x68\x59\xf6\x53\x00")          // +210: push 0x53f659
    + B("\xc3")                          // +215: ret
    + Bytes(42, '\x90')                  // +216: padding (42)
    + B("\x60")                          // +240: pushal
    + B("\xe8\x00\x00\x00\x00")          // +241: call 0x246
    + B("\x5b")                          // +246: pop ebx
    + B("\x8b\x9b\xba\x00\x00\x00")      // +247: mov ebx, dword ptr [ebx + 0xba]
    + B("\x80\x3b\x03")                  // +24d: cmp byte ptr [ebx], 3
    + B("\x75\x1d")                      // +250: jne 0x26f
    + B("\x80\x3d\x8d\x28\x2a\x01\x00")  // +252: cmp byte ptr [0x12a288d], 0
    + B("\x75\x14")                      // +259: jne 0x26f
    + B("\xc6\x03\x00")                  // +25b: mov byte ptr [ebx], 0
    + B("\xff\x73\x10")                  // +25e: push dword ptr [ebx + 0x10]
    + B("\x8d\x43\x20")                  // +261: lea eax, [ebx + 0x20]
    + B("\x50")                          // +264: push eax
    + B("\xb8\x20\x1a\x54\x00")          // +265: mov eax, 0x541a20
    + B("\xff\xd0")                      // +26a: call eax
    + B("\x83\xc4\x08")                  // +26c: add esp, 8
    + B("\x61")                          // +26f: popal
    + B("\x68\xf0\x85\x54\x00")          // +270: push 0x5485f0
    + B("\xc3")                          // +275: ret
    + Bytes(138, '\x90')                 // +276: padding (138)
;
static const SubtitleSite kSubtitleSites[] = {
    {"subtitle_show", 0x541A20, B("\x56\x8b\x35\xb0\x43\xf3\x00"), 0x0, 0xE9},
    {"subtitle_hide", 0x541AC0, B("\xa1\xf4\xbb\x0d\x01"), 0xC0, 0xE9},
    {"subtitle_pause", 0x53F650, B("\x8a\x4c\x24\x04\xa1\x40\x04\x24\x01"), 0x180, 0xE9},
    {"subtitle_frame", 0x418B97, B("\xe8\x54\xfa\x12\x00"), 0x240, 0xE8},
};


// Supported-build windowed resize and background playback fixes.
//
// Keep genuine focus notifications intact. Continue the native game loop when
// unfocused, avoid focus-driven audio suspension, and balance native ShowCursor
// calls so background updates cannot inflate its per-thread display counter.
// Ignore minimized/duplicate client-size notifications that needlessly reset
// movie textures. Genuine positive-size changes keep the original resize path.
static const FixedPatch kPlaybackPatches[] = {
    {"resize_notifications", 0xB984A0, 
        B("\x8b\x54\x24\x04")                // +000: mov edx, dword ptr [esp + 4]
        + B("\x85\xd2")                      // +004: test edx, edx
        + B("\x7f\x05")                      // +006: jg 0xd
        + B("\xba\x01\x00\x00\x00")          // +008: mov edx, 1
        + B("\x8b\x44\x24\x08")              // +00d: mov eax, dword ptr [esp + 8]
        + B("\x85\xc0")                      // +011: test eax, eax
        + B("\x7f\x05")                      // +013: jg 0x1a
        + B("\xb8\x01\x00\x00\x00")          // +015: mov eax, 1
        + B("\x89\x41\x34")                  // +01a: mov dword ptr [ecx + 0x34], eax
        + B("\x89\x81\xdc\x1e\x00\x00")      // +01d: mov dword ptr [ecx + 0x1edc], eax
        + B("\x89\x51\x30")                  // +023: mov dword ptr [ecx + 0x30], edx
        + B("\x89\x91\xd8\x1e\x00\x00")      // +026: mov dword ptr [ecx + 0x1ed8], edx
        + B("\xc6\x81\xa4\x01\x00\x00\x01")  // +02c: mov byte ptr [ecx + 0x1a4], 1
        + B("\xb0\x01")                      // +033: mov al, 1
        + B("\xc2\x08\x00")                  // +035: ret 8
        + Bytes(8, '\xcc')                   // +038: padding (8)
    , 
        B("\x8b\x54\x24\x04")                // +000: mov edx, dword ptr [esp + 4]
        + B("\x83\xfa\x01")                  // +004: cmp edx, 1
        + B("\x7e\x2c")                      // +007: jle 0x35
        + B("\x8b\x44\x24\x08")              // +009: mov eax, dword ptr [esp + 8]
        + B("\x83\xf8\x01")                  // +00d: cmp eax, 1
        + B("\x7e\x23")                      // +010: jle 0x35
        + B("\x39\x51\x30")                  // +012: cmp dword ptr [ecx + 0x30], edx
        + B("\x75\x05")                      // +015: jne 0x1c
        + B("\x39\x41\x34")                  // +017: cmp dword ptr [ecx + 0x34], eax
        + B("\x74\x19")                      // +01a: je 0x35
        + B("\x89\x41\x34")                  // +01c: mov dword ptr [ecx + 0x34], eax
        + B("\x89\x81\xdc\x1e\x00\x00")      // +01f: mov dword ptr [ecx + 0x1edc], eax
        + B("\x89\x51\x30")                  // +025: mov dword ptr [ecx + 0x30], edx
        + B("\x89\x91\xd8\x1e\x00\x00")      // +028: mov dword ptr [ecx + 0x1ed8], edx
        + B("\xc6\x81\xa4\x01\x00\x00\x01")  // +02e: mov byte ptr [ecx + 0x1a4], 1
        + B("\xb0\x01")                      // +035: mov al, 1
        + B("\xc2\x08\x00")                  // +037: ret 8
        + Bytes(6, '\x90')                   // +03a: padding (6)
    },
    {"balanced_cursor", 0x69DA00, 
        B("\x56")                                   // +000: push esi
        + B("\x8b\xf1")                             // +001: mov esi, ecx
        + B("\x8b\x46\x2c")                         // +003: mov eax, dword ptr [esi + 0x2c]
        + B("\x85\xc0")                             // +006: test eax, eax
        + B("\x74\x51")                             // +008: je 0x5b
        + B("\x83\xf8\x01")                         // +00a: cmp eax, 1
        + B("\x74\x4c")                             // +00d: je 0x5b
        + B("\xff\x15\x40\x4d\xf3\x00")             // +00f: call dword ptr [0xf34d40]
        + B("\x39\x46\x18")                         // +015: cmp dword ptr [esi + 0x18], eax
        + B("\x74\x1d")                             // +018: je 0x37
        + B("\x80\xbe\x15\x0d\x00\x00\x00")         // +01a: cmp byte ptr [esi + 0xd15], 0
        + B("\x75\x38")                             // +021: jne 0x5b
        + B("\x6a\x01")                             // +023: push 1
        + B("\xff\x15\x44\x4d\xf3\x00")             // +025: call dword ptr [0xf34d44]
        + B("\xc7\x05\xa0\xd0\x0e\x01\x01\x00\x00\x00") // +02b: mov dword ptr [0x10ed0a0], 1
        + B("\x5e")                                 // +035: pop esi
        + B("\xc3")                                 // +036: ret
        + B("\x83\x3d\xa0\xd0\x0e\x01\x00")         // +037: cmp dword ptr [0x10ed0a0], 0
        + B("\x74\x1b")                             // +03e: je 0x5b
        + B("\x80\xbe\x15\x0d\x00\x00\x00")         // +040: cmp byte ptr [esi + 0xd15], 0
        + B("\x75\x12")                             // +047: jne 0x5b
        + B("\x6a\x00")                             // +049: push 0
        + B("\xff\x15\x44\x4d\xf3\x00")             // +04b: call dword ptr [0xf34d44]
        + B("\xc7\x05\xa0\xd0\x0e\x01\x00\x00\x00\x00") // +051: mov dword ptr [0x10ed0a0], 0
        + B("\x5e")                                 // +05b: pop esi
        + B("\xc3")                                 // +05c: ret
        + B("\xcc")                                 // +05d: int3
        + B("\xcc")                                 // +05e: int3
        + B("\xcc")                                 // +05f: int3
    , 
        B("\x56")                            // +000: push esi
        + B("\x8b\xf1")                      // +001: mov esi, ecx
        + B("\x83\x7e\x2c\x01")              // +003: cmp dword ptr [esi + 0x2c], 1
        + B("\x76\x4d")                      // +007: jbe 0x56
        + B("\x80\xbe\x15\x0d\x00\x00\x00")  // +009: cmp byte ptr [esi + 0xd15], 0
        + B("\x75\x44")                      // +010: jne 0x56
        + B("\xff\x15\x40\x4d\xf3\x00")      // +012: call dword ptr [0xf34d40]
        + B("\x39\x46\x18")                  // +018: cmp dword ptr [esi + 0x18], eax
        + B("\x75\x1e")                      // +01b: jne 0x3b
        + B("\x6a\x00")                      // +01d: push 0
        + B("\xff\x15\x44\x4d\xf3\x00")      // +01f: call dword ptr [0xf34d44]
        + B("\x83\xf8\xff")                  // +025: cmp eax, -1
        + B("\x7d\x08")                      // +028: jge 0x32
        + B("\x6a\x01")                      // +02a: push 1
        + B("\xff\x15\x44\x4d\xf3\x00")      // +02c: call dword ptr [0xf34d44]
        + B("\xc6\x05\xa0\xd0\x0e\x01\x00")  // +032: mov byte ptr [0x10ed0a0], 0
        + B("\xeb\x1b")                      // +039: jmp 0x56
        + B("\x6a\x01")                      // +03b: push 1
        + B("\xff\x15\x44\x4d\xf3\x00")      // +03d: call dword ptr [0xf34d44]
        + B("\x85\xc0")                      // +043: test eax, eax
        + B("\x7e\x08")                      // +045: jle 0x4f
        + B("\x6a\x00")                      // +047: push 0
        + B("\xff\x15\x44\x4d\xf3\x00")      // +049: call dword ptr [0xf34d44]
        + B("\xc6\x05\xa0\xd0\x0e\x01\x01")  // +04f: mov byte ptr [0x10ed0a0], 1
        + B("\x5e")                          // +056: pop esi
        + B("\xc3")                          // +057: ret
        + Bytes(8, '\x90')                   // +058: padding (8)
    },
    {"background_updates", 0x401704, 
        B("\xe8\x37\xa9\x27\x00")            // +000: call 0x27a93c
        + B("\xeb\x18")                      // +005: jmp 0x1f
        + B("\xbf\x02\x00\x00\x00")          // +007: mov edi, 2
        + B("\x8b\xce")                      // +00c: mov ecx, esi
        + B("\x83\xef\x01")                  // +00e: sub edi, 1
        + B("\xe8\x16\x52\x02\x00")          // +011: call 0x2522c
        + B("\x85\xff")                      // +016: test edi, edi
        + B("\x75\xf2")                      // +018: jne 0xc
        + B("\xe8\xdd\xa8\x27\x00")          // +01a: call 0x27a8fc
        + B("\x88\x9e\xf8\x00\x00\x00")      // +01f: mov byte ptr [esi + 0xf8], bl
        + B("\xbf\x04\x01\x00\x00")          // +025: mov edi, 0x104
        + B("\x80\xbe\xf8\x00\x00\x00\x00")  // +02a: cmp byte ptr [esi + 0xf8], 0
        + B("\x74\x09")                      // +031: je 0x3c
    , 
        Bytes(5, '\x90')                     // +000: padding (5)
        + B("\xeb\x18")                      // +005: jmp 0x1f
        + B("\xbf\x02\x00\x00\x00")          // +007: mov edi, 2
        + B("\x8b\xce")                      // +00c: mov ecx, esi
        + B("\x83\xef\x01")                  // +00e: sub edi, 1
        + B("\xe8\x16\x52\x02\x00")          // +011: call 0x2522c
        + B("\x85\xff")                      // +016: test edi, edi
        + B("\x75\xf2")                      // +018: jne 0xc
        + Bytes(5, '\x90')                   // +01a: padding (5)
        + B("\x88\x9e\xf8\x00\x00\x00")      // +01f: mov byte ptr [esi + 0xf8], bl
        + B("\xbf\x04\x01\x00\x00")          // +025: mov edi, 0x104
        + B("\x80\xbe\xf8\x00\x00\x00\x00")  // +02a: cmp byte ptr [esi + 0xf8], 0
        + B("\x90")                          // +031: nop
        + B("\x90")                          // +032: nop
    },
};


// Runtime fix for SWTFU's healthy-device resize/reset loop.
//
// The original reset routine accepts D3DERR_DEVICENOTRESET but rejects D3D_OK
// even when WM_SIZE explicitly requested a swap-chain resize. The replacement
// accepts both, and preserves rejection of DEVICELOST and other errors.
// It keeps the original resource-release, Reset, and resource-rebuild calls.
//
// Only the supported hash-checked running game is changed. Restart restores
// the original code.
static const FixedPatch kRendererReset = {"renderer_reset", 0xB5DD90, 
    B("\x56")                            // +000: push esi
    + B("\x8b\xf1")                      // +001: mov esi, ecx
    + B("\x8b\x46\x2c")                  // +003: mov eax, dword ptr [esi + 0x2c]
    + B("\x85\xc0")                      // +006: test eax, eax
    + B("\x74\x5b")                      // +008: je 0x65
    + B("\x80\xbe\xa4\x01\x00\x00\x00")  // +00a: cmp byte ptr [esi + 0x1a4], 0
    + B("\x74\x52")                      // +011: je 0x65
    + B("\x8b\x08")                      // +013: mov ecx, dword ptr [eax]
    + B("\x8b\x51\x0c")                  // +015: mov edx, dword ptr [ecx + 0xc]
    + B("\x50")                          // +018: push eax
    + B("\xff\xd2")                      // +019: call edx
    + B("\x3d\x69\x08\x76\x88")          // +01b: cmp eax, 0x88760869
    + B("\x74\x04")                      // +020: je 0x26
    + B("\x33\xc0")                      // +022: xor eax, eax
    + B("\x5e")                          // +024: pop esi
    + B("\xc3")                          // +025: ret
    + B("\x80\xbe\xa5\x01\x00\x00\x00")  // +026: cmp byte ptr [esi + 0x1a5], 0
    + B("\x75\x07")                      // +02d: jne 0x36
    + B("\x8b\xce")                      // +02f: mov ecx, esi
    + B("\xe8\xaa\xf9\xff\xff")          // +031: call 0xfffff9e0
    + B("\x8b\x46\x2c")                  // +036: mov eax, dword ptr [esi + 0x2c]
    + B("\x8b\x08")                      // +039: mov ecx, dword ptr [eax]
    + B("\x8d\x56\x30")                  // +03b: lea edx, [esi + 0x30]
    + B("\x52")                          // +03e: push edx
    + B("\x50")                          // +03f: push eax
    + B("\x8b\x41\x40")                  // +040: mov eax, dword ptr [ecx + 0x40]
    + B("\xff\xd0")                      // +043: call eax
    + B("\x3d\x6c\x08\x76\x88")          // +045: cmp eax, 0x8876086c
    + B("\x74\xd6")                      // +04a: je 0x22
    + B("\x85\xc0")                      // +04c: test eax, eax
    + B("\x75\xd2")                      // +04e: jne 0x22
    + B("\x8b\xce")                      // +050: mov ecx, esi
    + B("\xe8\xc9\x97\xff\xff")          // +052: call 0xffff9820
    + B("\x8b\xce")                      // +057: mov ecx, esi
    + B("\xe8\xf2\xf8\xff\xff")          // +059: call 0xfffff950
    + B("\xc6\x86\xa4\x01\x00\x00\x00")  // +05e: mov byte ptr [esi + 0x1a4], 0
    + B("\xb8\x01\x00\x00\x00")          // +065: mov eax, 1
    + B("\x5e")                          // +06a: pop esi
    + B("\xc3")                          // +06b: ret
    + Bytes(4, '\xcc')                   // +06c: padding (4)
, 
    B("\x56")                            // +000: push esi
    + B("\x8b\xf1")                      // +001: mov esi, ecx
    + B("\x8b\x46\x2c")                  // +003: mov eax, dword ptr [esi + 0x2c]
    + B("\x85\xc0")                      // +006: test eax, eax
    + B("\x74\x5b")                      // +008: je 0x65
    + B("\x80\xbe\xa4\x01\x00\x00\x00")  // +00a: cmp byte ptr [esi + 0x1a4], 0
    + B("\x74\x52")                      // +011: je 0x65
    + B("\x8b\x08")                      // +013: mov ecx, dword ptr [eax]
    + B("\x8b\x51\x0c")                  // +015: mov edx, dword ptr [ecx + 0xc]
    + B("\x50")                          // +018: push eax
    + B("\xff\xd2")                      // +019: call edx
    + B("\x85\xc0")                      // +01b: test eax, eax
    + B("\x74\x07")                      // +01d: je 0x26
    + B("\x3d\x69\x08\x76\x88")          // +01f: cmp eax, 0x88760869
    + B("\x75\x46")                      // +024: jne 0x6c
    + B("\x80\xbe\xa5\x01\x00\x00\x00")  // +026: cmp byte ptr [esi + 0x1a5], 0
    + B("\x75\x07")                      // +02d: jne 0x36
    + B("\x8b\xce")                      // +02f: mov ecx, esi
    + B("\xe8\xaa\xf9\xff\xff")          // +031: call 0xfffff9e0
    + B("\x8b\x46\x2c")                  // +036: mov eax, dword ptr [esi + 0x2c]
    + B("\x8b\x08")                      // +039: mov ecx, dword ptr [eax]
    + B("\x8d\x56\x30")                  // +03b: lea edx, [esi + 0x30]
    + B("\x52")                          // +03e: push edx
    + B("\x50")                          // +03f: push eax
    + B("\x8b\x41\x40")                  // +040: mov eax, dword ptr [ecx + 0x40]
    + B("\xff\xd0")                      // +043: call eax
    + B("\x3d\x6c\x08\x76\x88")          // +045: cmp eax, 0x8876086c
    + B("\x74\x20")                      // +04a: je 0x6c
    + B("\x85\xc0")                      // +04c: test eax, eax
    + B("\x75\x1c")                      // +04e: jne 0x6c
    + B("\x8b\xce")                      // +050: mov ecx, esi
    + B("\xe8\xc9\x97\xff\xff")          // +052: call 0xffff9820
    + B("\x8b\xce")                      // +057: mov ecx, esi
    + B("\xe8\xf2\xf8\xff\xff")          // +059: call 0xfffff950
    + B("\xc6\x86\xa4\x01\x00\x00\x00")  // +05e: mov byte ptr [esi + 0x1a4], 0
    + B("\xb8\x01\x00\x00\x00")          // +065: mov eax, 1
    + B("\x5e")                          // +06a: pop esi
    + B("\xc3")                          // +06b: ret
    + B("\x33\xc0")                      // +06c: xor eax, eax
    + B("\x5e")                          // +06e: pop esi
    + B("\xc3")                          // +06f: ret
};
