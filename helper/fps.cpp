// 60 FPS profile: fixed byte patches + helper hook groups, applied as one
// transaction with exact rollback (mirrors swtfu/fps/profile.py).
#include "fps.h"
#include "patching.h"
#include "tables.h"
#include <chrono>
#include <thread>

static const uint32_t kLimitOffset = 0x150;

// ---- hook groups (swtfu/fps/{physics_smoothing,camera_smoothing,input_timing,update_rate}.py)

struct GroupState { uint32_t root = 0; std::vector<Bytes> currents; };

static GroupState validate(Game& game, const HookGroup& group) {
    for (int i = 0; i < group.source_count; ++i) {
        const SourceCheck& source = group.sources[i];
        if (game.read(source.address, source.original.size()) != source.original)
            throw Error(std::string("Unsupported ") + group.name + " source at " + hex(source.address));
    }
    GroupState state;
    for (int i = 0; i < group.count; ++i) {
        const HookSite& site = group.sites[i];
        Bytes current = game.read(site.address, site.original.size());
        state.currents.push_back(current);
        if (current == site.original) continue;
        if ((uint8_t)current[0] != 0xE8) throw Error(std::string("Unsupported ") + group.name + " code at " + hex(site.address));
        uint32_t candidate = rel32_target(site.address, current) - group.stride * i;
        if (state.root && candidate != state.root) throw Error(std::string(group.name) + " hooks refer to different helpers");
        if (current != rel32(0xE8, site.address, candidate + group.stride * i, site.original.size())
            || game.read(candidate, group.code.size()) != group.code)
            throw Error(std::string("Unrecognized ") + group.name + " helper");
        state.root = candidate;
    }
    return state;
}

static bool set_enabled(Game& game, const HookGroup& group, bool enabled) {
    GroupState state = validate(game, group);
    if (enabled && !state.root) state.root = publish_helper_page(game, group.code);
    bool changed = false;
    for (int i = 0; i < group.count; ++i) {
        const HookSite& site = group.sites[i];
        Bytes target = enabled ? rel32(0xE8, site.address, state.root + group.stride * i, site.original.size()) : site.original;
        changed |= apply_code_patch(game, site.address, state.currents[i], target);
    }
    return changed;
}

// ---- profile

struct Snapshot {
    std::vector<Bytes> fixed;
    std::vector<GroupState> groups;
    float limit;
    uint32_t cinematic;
    float saved;  // valid when cinematic != 0
};

static Snapshot snapshot(Game& game) {
    Snapshot s;
    for (const HookGroup* group : kFpsHelpers) s.groups.push_back(validate(game, *group));
    s.limit = game.f32(game.context + kLimitOffset);
    for (const FixedPatch& patch : kFpsPatches) {
        Bytes code = game.read(patch.address, patch.original.size());
        if (code != patch.original && code != patch.patched)
            throw Error(std::string("Unsupported ") + patch.name + " code; no FPS changes applied");
        s.fixed.push_back(code);
    }
    s.cinematic = game.u32(kCinematicState);
    s.saved = s.cinematic ? game.f32(s.cinematic + kSavedLimitOffset) : 0;
    if ((s.limit != 30 && s.limit != 60) || (s.cinematic && s.saved != 30 && s.saved != 60))
        throw Error("Unexpected frame-limit or cinematic state; no FPS changes applied");
    return s;
}

static bool apply_frozen(Game& game, int fps) {
    Snapshot before = snapshot(game);
    bool changed = false;
    try {
        for (size_t i = 0; i < std::size(kFpsPatches); ++i) {
            const FixedPatch& patch = kFpsPatches[i];
            changed |= apply_code_patch(game, patch.address, before.fixed[i], fps == 60 ? patch.patched : patch.original);
        }
        for (const HookGroup* group : kFpsHelpers) changed |= set_enabled(game, *group, fps == 60);
        if (before.cinematic && before.saved != fps) {
            game.write_f32(before.cinematic + kSavedLimitOffset, (float)fps);
            changed = true;
        }
        if (before.limit != fps) {
            if (game.u32(0x1240440) != game.context) throw Error("Game context changed; retry from the main menu");
            game.write_f32(game.context + kLimitOffset, (float)fps);
            changed = true;
        }
        return changed;
    } catch (const std::exception& error) {
        // Threads are still frozen: put every site back exactly as it was.
        std::string failures;
        for (size_t g = std::size(kFpsHelpers); g-- > 0;) {
            const HookGroup& group = *kFpsHelpers[g];
            try {
                validate(game, group);
                for (int i = group.count; i-- > 0;) {
                    Bytes now = game.read(group.sites[i].address, group.sites[i].original.size());
                    if (now != before.groups[g].currents[i])
                        apply_code_patch(game, group.sites[i].address, now, before.groups[g].currents[i]);
                }
            } catch (const std::exception& e) { failures += std::string("; ") + e.what(); }
        }
        for (size_t i = std::size(kFpsPatches); i-- > 0;) {
            const FixedPatch& patch = kFpsPatches[i];
            try {
                Bytes now = game.read(patch.address, patch.original.size());
                if (now != patch.original && now != patch.patched) throw Error("Unrecognized code during rollback at " + hex(patch.address));
                if (now != before.fixed[i]) apply_code_patch(game, patch.address, now, before.fixed[i]);
            } catch (const std::exception& e) { failures += std::string("; ") + e.what(); }
        }
        try {
            if (game.f32(game.context + kLimitOffset) != before.limit) game.write_f32(game.context + kLimitOffset, before.limit);
            if (before.cinematic && game.f32(before.cinematic + kSavedLimitOffset) != before.saved)
                game.write_f32(before.cinematic + kSavedLimitOffset, before.saved);
        } catch (const std::exception& e) { failures += std::string("; ") + e.what(); }
        if (!failures.empty())
            throw Error(std::string("FPS profile failed: ") + error.what() + "; rollback incomplete" + failures + ". Restart the game.");
        throw Error(std::string("FPS profile failed; prior profile restored: ") + error.what());
    }
}

bool set_fps_profile(Game& game, int fps) {
    snapshot(game);  // fast read-only rejection before suspending threads
    Ranges ranges;
    for (const FixedPatch& patch : kFpsPatches) ranges.push_back({patch.address, (uint32_t)patch.original.size()});
    for (const HookGroup* group : kFpsHelpers)
        for (int i = 0; i < group->count; ++i) ranges.push_back({group->sites[i].address, (uint32_t)group->sites[i].original.size()});
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        try {
            FrozenThreads frozen(game, ranges);
            return apply_frozen(game, fps);
        } catch (const CodeBusy&) {
            if (std::chrono::steady_clock::now() >= deadline) throw Error("FPS code is in use; no profile changes applied. Retry.");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

std::vector<std::pair<std::string, std::string>> inspect_fps(Game& game) {
    std::vector<std::pair<std::string, std::string>> result;
    result.push_back({"native_fps_limit", std::to_string((int)game.f32(game.context + kLimitOffset))});
    for (const FixedPatch& patch : kFpsPatches) {
        Bytes current = game.read(patch.address, patch.original.size());
        result.push_back({patch.name, current == patch.patched ? "60" : current == patch.original ? "30" : "unsupported"});
    }
    for (const HookGroup* group : kFpsHelpers) {
        try {
            GroupState state = validate(game, *group);
            for (int i = 0; i < group->count; ++i)
                result.push_back({group->sites[i].name, state.currents[i] == group->sites[i].original ? "30" : "60"});
        } catch (const std::exception&) {
            result.push_back({group->name, "unsupported"});
        }
    }
    return result;
}
