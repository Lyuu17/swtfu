// Shared types for SWTFU-Helper. Bytes are std::string (binary-safe).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

typedef std::string Bytes;
#define B(literal) Bytes(literal, sizeof(literal) - 1)

struct FixedPatch { const char* name; uint32_t address; Bytes original, patched; };
struct HookSite { const char* name; uint32_t address; Bytes original; };
struct SubtitleSite { const char* name; uint32_t address; Bytes original; uint32_t offset; uint8_t opcode; };
struct SourceCheck { uint32_t address; Bytes original; };
// A set of call sites redirected to helpers that live in one allocated page,
// `stride` bytes apart (helper i = root + stride * i). Mirrors swtfu/fps/*.py.
struct HookGroup {
    const char* name; const HookSite* sites; int count;
    Bytes code; uint32_t stride; const SourceCheck* sources; int source_count;
};

// One game's 60 FPS profile: fixed byte patches, helper hook groups, where the
// cap float lives ([app] + limit_offset) and where cutscenes save it
// ([cinematic_state] + saved_offset, 0 = no active cutscene).
struct FpsProfile {
    const FixedPatch* patches; int patch_count;
    const HookGroup* const* helpers; int helper_count;
    uint32_t limit_offset, cinematic_state, saved_offset;
};

struct Error : std::runtime_error {
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};
// The requested game is not running (callers that can start it catch this).
struct NotRunning : Error { using Error::Error; };
// A thread is executing (or will return into) code we want to replace; retry.
struct CodeBusy : Error { CodeBusy() : Error("code busy") {} };

std::string hex(uint32_t value);
std::string utf8(const std::wstring& text);
std::wstring widen(const std::string& text);
std::string win_error(const char* what);  // "what: <GetLastError message>"
[[noreturn]] void fail_win(const char* what);
