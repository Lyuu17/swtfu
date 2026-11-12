#include "game.h"
#include <bcrypt.h>
#include <tlhelp32.h>
#include <cstdio>
#include <fstream>
#include <iterator>

static const char* kExpectedHash = "1c156705548fce9e33fa3423e75b5cbe36248319b96f4b4850bf2e8125692032";
static const Bytes kCodeSignature = B("\x6a\x00\x6a\x00\xe8\x07\x02\x00\x00\x83\xc4\x08\xc2\x10\x00\xcc");
const char* const kNotRunning = "Launch SWTFU through Steam first";

std::string hex(uint32_t value) {
    char text[16];
    sprintf_s(text, "0x%X", value);
    return text;
}

std::string win_error(const char* what) {
    DWORD code = GetLastError();
    char text[512] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, text, sizeof(text), nullptr);
    std::string message = std::string(what) + ": " + text;
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
    return message + " (" + std::to_string(code) + ")";
}

void fail_win(const char* what) { throw Error(win_error(what)); }

std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string sha256_file(const std::wstring& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error("Cannot read " + utf8(path));
    std::string data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    unsigned char digest[32];
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), digest, 32))
        throw Error("SHA-256 failed");
    std::string text;
    char part[3];
    for (unsigned char byte : digest) { sprintf_s(part, "%02x", byte); text += part; }
    return text;
}

std::wstring exe_dir() {
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer, length);
    return path.substr(0, path.find_last_of(L'\\'));
}

Game::Game(DWORD pid_, bool writable) : pid(pid_) {
    // QUERY_LIMITED_INFORMATION | VM_READ, plus VM_OPERATION | VM_WRITE | QUERY_INFORMATION.
    handle = OpenProcess(0x1010 | (writable ? 0x428 : 0), FALSE, pid);
    if (!handle) fail_win("OpenProcess");
    try {
        FILETIME times[4];
        if (!GetProcessTimes(handle, &times[0], &times[1], &times[2], &times[3])) fail_win("GetProcessTimes");
        creation_time = ((uint64_t)times[0].dwHighDateTime << 32) | times[0].dwLowDateTime;
        std::vector<wchar_t> name(32768);
        DWORD length = (DWORD)name.size();
        if (!QueryFullProcessImageNameW(handle, 0, name.data(), &length)) fail_win("QueryFullProcessImageNameW");
        path.assign(name.data(), length);
        if (sha256_file(path) != kExpectedHash) throw Error("Unsupported executable hash; no changes made");
        if (read(0x401000, 16) != kCodeSignature)
            throw Error("Game code is not initialized or differs from the supported build");
        context = u32(0x1240440);
        if (context < 0x10000) throw Error("Game is not initialized yet; leave it at the main menu");
    } catch (...) {
        CloseHandle(handle);
        throw;
    }
}

Game::~Game() { if (handle) CloseHandle(handle); }

Bytes Game::read(uint32_t address, size_t size) const {
    Bytes data(size, '\0');
    SIZE_T count = 0;
    if (!ReadProcessMemory(handle, (LPCVOID)(uintptr_t)address, &data[0], size, &count)) fail_win("ReadProcessMemory");
    if (count != size) throw Error("Incomplete process-memory read");
    return data;
}

uint32_t Game::u32(uint32_t address) const {
    uint32_t value;
    memcpy(&value, read(address, 4).data(), 4);
    return value;
}

float Game::f32(uint32_t address) const {
    float value;
    memcpy(&value, read(address, 4).data(), 4);
    return value;
}

void Game::write_raw(uint32_t address, const Bytes& data) {
    SIZE_T count = 0;
    if (!WriteProcessMemory(handle, (LPVOID)(uintptr_t)address, data.data(), data.size(), &count)) fail_win("WriteProcessMemory");
    if (count != data.size() || read(address, data.size()) != data) throw Error("Memory write did not verify at " + hex(address));
}

void Game::write_f32(uint32_t address, float value) {
    write_raw(address, Bytes((const char*)&value, 4));
}

bool Game::running() const {
    DWORD code = 0;
    if (!GetExitCodeProcess(handle, &code)) fail_win("GetExitCodeProcess");
    return code == STILL_ACTIVE;
}

DWORD find_game() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) fail_win("CreateToolhelp32Snapshot");
    std::vector<DWORD> pids;
    PROCESSENTRY32W entry = {sizeof(entry)};
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        if (!_wcsicmp(entry.szExeFile, L"swtfu.exe")) pids.push_back(entry.th32ProcessID);
    CloseHandle(snapshot);
    if (pids.size() != 1) throw Error(pids.empty() ? kNotRunning : "Multiple games found; specify --pid");
    return pids[0];
}
