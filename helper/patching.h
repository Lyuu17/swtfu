// Safe runtime code patching (mirrors swtfu/patching.py).
#pragma once
#include "game.h"
#include <functional>
#include <utility>

typedef std::vector<std::pair<uint32_t, uint32_t>> Ranges;  // (address, size)

// Suspends every game thread. Throws CodeBusy if any thread is executing in,
// or has a return address into, one of `ranges`. Resumes on destruction.
class FrozenThreads {
public:
    FrozenThreads(Game& game, const Ranges& ranges);
    ~FrozenThreads();
private:
    void resume();
    void check_quiet();
    Game& game_;
    Ranges ranges_;
    std::vector<HANDLE> handles_;
};

// Replace `original` with same-size `patched` at address. Returns false if
// already patched; throws if the bytes are neither. Retries while busy (5 s).
bool apply_code_patch(Game& game, uint32_t address, const Bytes& original, const Bytes& patched);

// Allocate one page in the game, publish code read/execute, return its address.
// `build(address)` produces the code for position-dependent helpers.
// Pages are never freed: a thread may still be returning through a helper.
uint32_t publish_helper_page(Game& game, const std::function<Bytes(uint32_t)>& build);
inline uint32_t publish_helper_page(Game& game, const Bytes& code) {
    return publish_helper_page(game, [&](uint32_t) { return code; });
}

// E8/E9 rel32 to target, padded with NOPs to `size`.
Bytes rel32(uint8_t opcode, uint32_t site, uint32_t target, size_t size);
uint32_t rel32_target(uint32_t site, const Bytes& current);
