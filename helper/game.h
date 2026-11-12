// Attach to the running supported SWTFU build (mirrors swtfu/game.py).
#pragma once
#include "common.h"

std::string sha256_file(const std::wstring& path);
std::wstring exe_dir();  // folder holding SWTFU-Helper.exe (state/log files go here)

struct Game {
    HANDLE handle = nullptr;
    DWORD pid = 0;
    uint64_t creation_time = 0;
    std::wstring path;
    uint32_t context = 0;  // gApp = [0x1240440]

    Game(DWORD pid, bool writable);
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    Bytes read(uint32_t address, size_t size) const;
    uint8_t u8(uint32_t address) const { return (uint8_t)read(address, 1)[0]; }
    uint32_t u32(uint32_t address) const;
    float f32(uint32_t address) const;
    void write_raw(uint32_t address, const Bytes& data);  // plain data write + verify
    void write_f32(uint32_t address, float value);
    bool running() const;
};

DWORD find_game();  // throws "Launch SWTFU through Steam first" when absent
extern const char* const kNotRunning;
