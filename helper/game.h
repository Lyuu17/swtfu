// Attach to a running supported game (TFU1 or TFU2; mirrors swtfu/*/game.py).
#pragma once
#include "common.h"

std::string sha256_file(const std::wstring& path);
std::wstring exe_dir();  // folder holding SWTFU-Helper.exe (state/log files go here)

// Identity of one supported game build; records live in tfu1.h / tfu2.h.
struct GameInfo {
    const char* name;         // "tfu1" / "tfu2"
    const wchar_t* exe;       // process image name
    const char* sha256;       // of the executable on disk
    Bytes signature;          // first code bytes at 0x401000 after SteamStub decryption
    uint32_t app_global;      // address of the gApp pointer, 0 = not mapped yet
};
extern const GameInfo* const kGames[2];

struct Found { DWORD pid; const GameInfo* info; };
Found find_game();  // throws kNotRunning when no supported game runs
extern const char* const kNotRunning;

struct Game {
    HANDLE handle = nullptr;
    DWORD pid = 0;
    const GameInfo* info = nullptr;
    uint64_t creation_time = 0;
    std::wstring path;
    uint32_t context = 0;  // [info->app_global], 0 when not mapped

    Game(const Found& found, bool writable);
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    bool is(const char* name) const { return !strcmp(info->name, name); }
    void require(const char* name, const char* feature) const;  // throws "<feature> is not supported for <game> yet"

    Bytes read(uint32_t address, size_t size) const;
    uint8_t u8(uint32_t address) const { return (uint8_t)read(address, 1)[0]; }
    uint32_t u32(uint32_t address) const;
    float f32(uint32_t address) const;
    void write_raw(uint32_t address, const Bytes& data);  // plain data write + verify
    void write_f32(uint32_t address, float value);
    bool running() const;
};
