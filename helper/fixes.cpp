// Controller, movie, playback and renderer fixes (mirrors swtfu/controller.py,
// movie_messages.py, windowed_playback.py, renderer_reset.py).
// Helpers left by older helper versions in the same game session are refused
// ("restart the game") instead of upgraded.
#include "fixes.h"
#include "patching.h"
#include "tfu1.h"

// ---- controller reconnect + launch-movie skip

static Bytes controller_page(uint32_t cave) {
    Bytes page = kPadHead;
    uint32_t jump = kPadEpilogue - (cave + (uint32_t)kPadHead.size() + 5);
    page += '\xe9';
    page.append((const char*)&jump, 4);
    page.resize(kSkipOffset, '\xcc');
    return page + kSkipCode;
}

static std::vector<FixedPatch> controller_hooks(uint32_t cave) {
    return {{"controller_reconnect", kPadSite, kPadOriginal, rel32(0xE9, kPadSite, cave, kPadOriginal.size())},
            {"launch_movie_skip", kSkipSite, kSkipOriginal, rel32(0xE8, kSkipSite, cave + kSkipOffset, kSkipOriginal.size())}};
}

static uint32_t controller_cave(Game& game) {
    Bytes pad = game.read(kPadSite, kPadOriginal.size());
    Bytes skip = game.read(kSkipSite, kSkipOriginal.size());
    if (pad == kPadOriginal && skip == kSkipOriginal) return 0;
    uint32_t cave = (uint8_t)pad[0] == 0xE9 ? rel32_target(kPadSite, pad) : 0;
    bool ok = cave != 0;
    if (ok)
        for (const FixedPatch& hook : controller_hooks(cave))
            ok = ok && game.read(hook.address, hook.patched.size()) == hook.patched;
    if (!ok || game.read(cave, controller_page(cave).size()) != controller_page(cave))
        throw Error("Unsupported controller/movie code; restart the game");
    return cave;
}

bool apply_controller(Game& game) {
    if (controller_cave(game)) return false;
    uint32_t cave = publish_helper_page(game, controller_page);  // code embeds its own address
    for (const FixedPatch& hook : controller_hooks(cave)) apply_code_patch(game, hook.address, hook.original, hook.patched);
    return true;
}

bool controller_installed(Game& game) { return controller_cave(game) != 0; }

// ---- subtitle kept across the pause menu (swtfu/tfu1/subtitle_pause.py)

static Bytes subtitle_page(uint32_t data) { return kSubtitleCode + Bytes((const char*)&data, 4); }

static uint32_t subtitle_cave(Game& game) {
    const SubtitleSite& first = kSubtitleSites[0];
    Bytes now = game.read(first.address, first.original.size());
    uint32_t cave = (uint8_t)now[0] == first.opcode ? rel32_target(first.address, now) - first.offset : 0;
    bool original = true, hooked = cave != 0;
    for (const SubtitleSite& site : kSubtitleSites) {
        Bytes current = game.read(site.address, site.original.size());
        original = original && current == site.original;
        hooked = hooked && current == rel32(site.opcode, site.address, cave + site.offset, site.original.size());
    }
    if (original) return 0;
    if (!hooked || game.read(cave, kSubtitleCode.size()) != kSubtitleCode)
        throw Error("Unsupported subtitle code (older helper?); restart the game");
    return cave;
}

bool apply_subtitle_pause(Game& game) {
    if (subtitle_cave(game)) return false;
    void* data = VirtualAllocEx(game.handle, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);  // zeroed: state 0
    if (!data) fail_win("VirtualAllocEx");
    uint32_t cave = publish_helper_page(game, subtitle_page((uint32_t)(uintptr_t)data));
    // Frame (re-show) hook last; the others only record state until it exists.
    for (const SubtitleSite& site : kSubtitleSites)
        apply_code_patch(game, site.address, site.original, rel32(site.opcode, site.address, cave + site.offset, site.original.size()));
    return true;
}

bool subtitle_pause_installed(Game& game) { return subtitle_cave(game) != 0; }

// ---- movie message pump

static Bytes wait_hook(uint32_t site, uint32_t helper) {
    return rel32(0xE8, site, helper + kMovieWaitOffset, kMovieWaitOriginal.size());
}

static bool apply_movie_messages(Game& game) {
    Bytes current = game.read(kMovieAddress, kMovieOriginal.size());
    uint32_t helper = 0;
    if ((uint8_t)current[0] == 0xBE && (uint8_t)current.back() == 0x90) {
        memcpy(&helper, current.data() + 1, 4);
        if (game.read(helper, kMovieCode.size()) != kMovieCode)
            throw Error("Movie helper from an older version is installed; restart the game");
    } else if (current != kMovieOriginal) {
        throw Error("Unsupported movie wait code; no movie patch applied");
    }
    for (uint32_t site : kMovieWaitSites) {
        Bytes now = game.read(site, kMovieWaitOriginal.size());
        if (now != kMovieWaitOriginal && !(helper && now == wait_hook(site, helper)))
            throw Error("Unsupported movie worker wait at " + hex(site) + "; no movie patch applied");
    }
    bool changed = false;
    if (!helper) {
        helper = publish_helper_page(game, kMovieCode);
        Bytes hook = B("\xbe");  // mov esi, helper (replaces mov esi,[LECSleep])
        hook.append((const char*)&helper, 4);
        hook += '\x90';
        changed |= apply_code_patch(game, kMovieAddress, current, hook);
    }
    for (uint32_t site : kMovieWaitSites)
        changed |= apply_code_patch(game, site, game.read(site, kMovieWaitOriginal.size()), wait_hook(site, helper));
    return changed;
}

// ---- background playback, cursor, resize

bool apply_playback_fixes(Game& game) {
    uint32_t renderer = game.u32(0x13BB5D4);
    if (!renderer || game.u32(renderer + 80) != 1) throw Error("Background playback fixes require native windowed mode");
    uint32_t cursor = game.u32(0x10ED0A0);
    if (cursor != 0 && cursor != 1) throw Error("Unexpected native cursor state; no playback patches applied");
    for (const FixedPatch& patch : kPlaybackPatches) {
        Bytes now = game.read(patch.address, patch.original.size());
        if (now != patch.original && now != patch.patched)
            throw Error(std::string("Unsupported ") + patch.name + " code (older helper? restart the game); no playback patches applied");
    }
    bool changed = apply_movie_messages(game);
    // Background loop last, after its cursor-count guard exists (table order).
    for (const FixedPatch& patch : kPlaybackPatches)
        changed |= apply_code_patch(game, patch.address, game.read(patch.address, patch.original.size()), patch.patched);
    return changed;
}

bool apply_renderer_reset(Game& game) {
    return apply_code_patch(game, kRendererReset.address, kRendererReset.original, kRendererReset.patched);
}
