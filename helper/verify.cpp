// `SWTFU-Helper verify <decoded.exe>`: every patch site's original bytes must be at its
// address in the decoded image (tools/decode_exe.py). No game process is touched.
#include "patches.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {

struct Image {
    std::string data;
    uint32_t base = 0;
    struct Section { uint32_t va, size, raw, raw_size; };
    std::vector<Section> sections;

    explicit Image(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw Error("Cannot read " + path);
        data.assign(std::istreambuf_iterator<char>(file), {});
        uint32_t nt = u32(0x3C);
        if (data.size() < nt + 0xF8 || u32(nt) != 0x4550) throw Error("Not a PE image: " + path);
        uint16_t count = (uint16_t)u32(nt + 6), optional = (uint16_t)u32(nt + 20);
        base = u32(nt + 24 + 28);
        for (uint32_t s = nt + 24 + optional, i = 0; i < count; ++i, s += 40)
            sections.push_back({u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16)});
    }
    uint32_t u32(size_t offset) const {
        if (offset + 4 > data.size()) throw Error("Truncated PE image");
        uint32_t value;
        memcpy(&value, data.data() + offset, 4);
        return value;
    }
    Bytes read(uint32_t address, size_t size) const {
        uint32_t rva = address - base;
        for (const Section& s : sections)
            if (rva >= s.va && rva + size <= s.va + std::max(s.size, s.raw_size) && rva - s.va + size <= s.raw_size)
                return data.substr(s.raw + rva - s.va, size);
        return {};
    }
};

struct Check { std::string name; uint32_t address; Bytes original; };

void add_profile(std::vector<Check>& checks, const FpsProfile& profile) {
    for (int i = 0; i < profile.patch_count; ++i)
        checks.push_back({profile.patches[i].name, profile.patches[i].address, profile.patches[i].original});
    for (int g = 0; g < profile.helper_count; ++g) {
        const HookGroup& group = *profile.helpers[g];
        for (int i = 0; i < group.count; ++i)
            checks.push_back({group.sites[i].name, group.sites[i].address, group.sites[i].original});
        for (int i = 0; i < group.source_count; ++i)
            checks.push_back({std::string(group.name) + " source", group.sources[i].address, group.sources[i].original});
    }
}

}  // namespace

int run_verify(const std::string& path) {
    Image image(path);
    std::vector<Check> checks;
    const GameInfo* game = nullptr;
    if (image.read(0x401000, kTfu1Game.signature.size()) == kTfu1Game.signature) {
        game = &kTfu1Game;
        add_profile(checks, kTfu1Fps);
        checks.push_back({"controller_reconnect", kPadSite, kPadOriginal});
        checks.push_back({"launch_movie_skip", kSkipSite, kSkipOriginal});
        checks.push_back({"movie_sleep", kMovieAddress, kMovieOriginal});
        for (uint32_t site : kMovieWaitSites) checks.push_back({"movie_wait", site, kMovieWaitOriginal});
        for (const SubtitleSite& site : kSubtitleSites) checks.push_back({site.name, site.address, site.original});
        for (const FixedPatch& patch : kPlaybackPatches) checks.push_back({patch.name, patch.address, patch.original});
        checks.push_back({kRendererReset.name, kRendererReset.address, kRendererReset.original});
    } else if (image.read(0x401000, kTfu2Game.signature.size()) == kTfu2Game.signature) {
        game = &kTfu2Game;
        add_profile(checks, kTfu2Fps);
        for (int i = 0; i < kTfu2MovieMessages.count; ++i)
            checks.push_back({kTfu2MovieMessages.sites[i].name, kTfu2MovieMessages.sites[i].address, kTfu2MovieMessages.sites[i].original});
    } else {
        throw Error("Not a decoded image of a supported game (code signature at 0x401000 differs)");
    }
    int bad = 0;
    for (const Check& check : checks)
        if (image.read(check.address, check.original.size()) != check.original) {
            std::cout << "FAIL " << check.name << " at " << hex(check.address) << "\n";
            ++bad;
        }
    std::cout << game->name << ": " << checks.size() - bad << "/" << checks.size() << " patch sites match\n";
    return bad ? 1 : 0;
}
