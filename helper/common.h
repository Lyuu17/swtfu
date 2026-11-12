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
struct SourceCheck { uint32_t address; Bytes original; };
// A set of call sites redirected to helpers that live in one allocated page,
// `stride` bytes apart (helper i = root + stride * i). Mirrors swtfu/fps/*.py.
struct HookGroup {
    const char* name; const HookSite* sites; int count;
    Bytes code; uint32_t stride; const SourceCheck* sources; int source_count;
};

struct Error : std::runtime_error {
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};
// A thread is executing (or will return into) code we want to replace; retry.
struct CodeBusy : Error { CodeBusy() : Error("code busy") {} };

std::string hex(uint32_t value);
std::string utf8(const std::wstring& text);
std::wstring widen(const std::string& text);
std::string win_error(const char* what);  // "what: <GetLastError message>"
[[noreturn]] void fail_win(const char* what);
