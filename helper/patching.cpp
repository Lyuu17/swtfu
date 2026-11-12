#include "patching.h"
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <set>
#include <thread>

static_assert(sizeof(void*) == 4, "Build x86: the game is 32-bit and thread contexts are read as x86");

static double now_seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

FrozenThreads::FrozenThreads(Game& game, const Ranges& ranges) : game_(game) {
    // Merge overlapping ranges once; check_quiet tests membership.
    Ranges sorted = ranges;
    std::sort(sorted.begin(), sorted.end());
    for (auto& range : sorted) {
        uint32_t end = range.first + range.second;
        if (!ranges_.empty() && range.first <= ranges_.back().first + ranges_.back().second)
            ranges_.back().second = std::max(ranges_.back().first + ranges_.back().second, end) - ranges_.back().first;
        else
            ranges_.push_back(range);
    }
    std::set<DWORD> suspended;
    double deadline = now_seconds() + 2;
    try {
        // Threads can be created while we suspend; repeat until a snapshot adds none.
        for (int round = 0; round < 8; ++round) {
            std::vector<DWORD> pending;
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) fail_win("CreateToolhelp32Snapshot");
            THREADENTRY32 entry = {sizeof(entry)};
            for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
                if (entry.th32OwnerProcessID == game.pid && !suspended.count(entry.th32ThreadID))
                    pending.push_back(entry.th32ThreadID);
            CloseHandle(snapshot);
            if (pending.empty()) {
                if (handles_.empty()) throw Error("Game exited before the patch could be applied");
                check_quiet();
                return;
            }
            for (DWORD id : pending) {
                if (now_seconds() > deadline) throw Error("Could not suspend game threads promptly; no code changed");
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, id);
                if (!thread) {
                    if (GetLastError() == ERROR_INVALID_PARAMETER) continue;  // exited after the snapshot
                    fail_win("OpenThread");
                }
                if (SuspendThread(thread) == (DWORD)-1) {
                    std::string message = win_error("SuspendThread");
                    CloseHandle(thread);
                    throw Error(message);
                }
                handles_.push_back(thread);
                suspended.insert(id);
            }
        }
        throw Error("Game threads did not settle; no code changed");
    } catch (...) {
        resume();
        throw;
    }
}

FrozenThreads::~FrozenThreads() {
    try { resume(); } catch (...) {}
}

void FrozenThreads::resume() {
    bool failed = false;
    for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) {
        if (ResumeThread(*it) == (DWORD)-1) failed = true;
        CloseHandle(*it);
    }
    handles_.clear();
    if (failed) throw Error("Could not resume all game threads; restart the game");
}

void FrozenThreads::check_quiet() {
    // Suspension alone is not enough: a thread may be stopped inside the old
    // bytes or hold a return address into them. Scan EIP and the whole stack.
    auto contains = [&](uint32_t value) {
        for (auto& range : ranges_)
            if (value >= range.first && value < range.first + range.second) return true;
        return false;
    };
    for (HANDLE thread : handles_) {
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(thread, &context)) fail_win("GetThreadContext");
        if (contains(context.Eip) || !context.Esp) throw CodeBusy();
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQueryEx(game_.handle, (LPCVOID)(uintptr_t)context.Esp, &region, sizeof(region))) fail_win("VirtualQueryEx");
        uint32_t size = (uint32_t)((uintptr_t)region.BaseAddress + region.RegionSize - context.Esp);
        if (region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !size || size > 8u << 20)
            throw Error("Cannot verify the active thread stack; no code changed");
        Bytes stack = game_.read(context.Esp, size - size % 4);
        for (size_t offset = 0; offset < stack.size(); offset += 4) {
            uint32_t value;
            memcpy(&value, stack.data() + offset, 4);
            if (contains(value)) throw CodeBusy();
        }
    }
}

static void write_code(Game& game, uint32_t address, const Bytes& data) {
    game.write_raw(address, data);
    if (!FlushInstructionCache(game.handle, (LPCVOID)(uintptr_t)address, data.size())) fail_win("FlushInstructionCache");
}

bool apply_code_patch(Game& game, uint32_t address, const Bytes& original, const Bytes& patched) {
    if (original.empty() || original.size() != patched.size()) throw Error("Invalid fixed-size runtime patch");
    Bytes current = game.read(address, original.size());
    if (current == patched) return false;
    if (current != original) throw Error("Code at " + hex(address) + " differs from the supported build; no code changed");
    double deadline = now_seconds() + 5;
    for (;;) {
        try {
            FrozenThreads frozen(game, Ranges{{address, (uint32_t)original.size()}});
            if (game.read(address, original.size()) != original)
                throw Error("Code changed while preparing the patch; no code changed");
            DWORD protection;
            if (!VirtualProtectEx(game.handle, (LPVOID)(uintptr_t)address, original.size(), PAGE_EXECUTE_READWRITE, &protection))
                fail_win("VirtualProtectEx");
            try {
                try { write_code(game, address, patched); }
                catch (...) { write_code(game, address, original); throw; }
            } catch (...) {
                DWORD ignored;
                VirtualProtectEx(game.handle, (LPVOID)(uintptr_t)address, original.size(), protection, &ignored);
                throw;
            }
            DWORD ignored;
            if (!VirtualProtectEx(game.handle, (LPVOID)(uintptr_t)address, original.size(), protection, &ignored))
                throw Error("Could not restore code-page protection; restart the game");
            return true;
        } catch (const CodeBusy&) {
            if (now_seconds() >= deadline) throw Error("Code is currently in use at " + hex(address) + "; retry the helper");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

uint32_t publish_helper_page(Game& game, const std::function<Bytes(uint32_t)>& build) {
    void* page = VirtualAllocEx(game.handle, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!page) fail_win("VirtualAllocEx");
    uint32_t address = (uint32_t)(uintptr_t)page;
    try {
        Bytes code = build(address);
        if (code.size() > 4096) throw Error("Helper code exceeds one page");
        game.write_raw(address, code);
        DWORD old;
        if (!VirtualProtectEx(game.handle, page, 4096, PAGE_EXECUTE_READ, &old)) fail_win("VirtualProtectEx");
        if (!FlushInstructionCache(game.handle, page, 4096)) fail_win("FlushInstructionCache");
        return address;
    } catch (...) {
        VirtualFreeEx(game.handle, page, 0, MEM_RELEASE);
        throw;
    }
}

Bytes rel32(uint8_t opcode, uint32_t site, uint32_t target, size_t size) {
    uint32_t relative = target - site - 5;
    Bytes code(1, (char)opcode);
    code.append((const char*)&relative, 4);
    code.resize(size, '\x90');
    return code;
}

uint32_t rel32_target(uint32_t site, const Bytes& current) {
    int32_t relative;
    memcpy(&relative, current.data() + 1, 4);
    return site + 5 + (uint32_t)relative;
}
