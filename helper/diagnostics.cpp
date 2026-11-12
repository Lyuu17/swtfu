// Read-only diagnostics (mirrors swtfu/hang_diagnostics.py and frame_pacing.py).
// Never changes focus or game memory.
#include "diagnostics.h"
#include "fps.h"
#include "json.h"
#include <dbghelp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>

static std::wstring timestamped_folder(const wchar_t* kind, DWORD pid) {
    SYSTEMTIME t;
    GetSystemTime(&t);
    wchar_t name[64];
    swprintf_s(name, L"%04d%02d%02d-%02d%02d%02d-%03d-%lu", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, pid);
    std::wstring folder = exe_dir() + L"\\analysis";
    CreateDirectoryW(folder.c_str(), nullptr);
    folder += std::wstring(L"\\") + kind;
    CreateDirectoryW(folder.c_str(), nullptr);
    folder += std::wstring(L"\\") + name;
    if (!CreateDirectoryW(folder.c_str(), nullptr)) fail_win("CreateDirectory");
    return folder;
}

static void write_text(const std::wstring& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}


// ---- hangs

std::string capture_hang(Game& game, const std::string& reason) {
    std::wstring folder = timestamped_folder(L"hangs", game.pid);
    Json meta = Json::object();
    meta.set("pid", Json::num((unsigned long long)game.pid));
    meta.set("reason", Json::str(reason));
    try {
        uint32_t renderer = game.u32(0x13BB5D4);
        meta.set("hwnd", Json::num((unsigned long long)game.u32(renderer + 408)));
        meta.set("windowed", Json::num((unsigned long long)game.u32(renderer + 80)));
        meta.set("reset_pending", Json::num((unsigned long long)game.u8(renderer + 420)));
        meta.set("native_cursor_shown", Json::num((unsigned long long)game.u32(0x10ED0A0)));
        Json sites = Json::object();
        for (auto& site : inspect_fps(game)) sites.set(site.first, Json::str(site.second));
        meta.set("fps_profile_sites", sites);
    } catch (const std::exception& e) {
        meta.set("snapshot_error", Json::str(e.what()));
    }
    std::wstring dump = folder + L"\\game.dmp", part = dump + L".part";
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, game.pid);
    HANDLE file = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = process && file != INVALID_HANDLE_VALUE &&
              MiniDumpWriteDump(process, game.pid, file, (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory), nullptr, nullptr, nullptr);
    std::string error = ok ? "" : win_error("MiniDumpWriteDump");
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (process) CloseHandle(process);
    if (ok && MoveFileW(part.c_str(), dump.c_str())) meta.set("status", Json::str("captured"));
    else { DeleteFileW(part.c_str()); meta.set("status", Json::str("dump failed")); meta.set("dump_error", Json::str(error)); }
    write_text(folder + L"\\snapshot.json", meta.dump() + "\n");
    std::cout << "Hang diagnostics saved: " << utf8(folder) << std::endl;
    return utf8(folder);
}

std::string watch_for_hang(Game& game, int seconds) {
    HWND hwnd = (HWND)(uintptr_t)game.u32(game.u32(0x13BB5D4) + 408);
    DWORD owner = 0, thread = GetWindowThreadProcessId(hwnd, &owner);
    if (!thread || owner != game.pid) throw Error("Game window is not accessible; run the helper in your normal session");
    std::cout << "Watching for a hang for up to " << seconds << " seconds; you may minimize this window." << std::endl;
    std::cout << "If clicking back into the game freezes it, leave it running briefly for automatic capture." << std::endl;
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::seconds(seconds);
    clock::time_point healthy{}, blocked{};
    bool armed = false;
    while (clock::now() < deadline) {
        if (!game.running()) return "game exited";
        DWORD current = 0;
        if (GetWindowThreadProcessId(hwnd, &current) != thread || current != game.pid) return "game window changed";
        DWORD_PTR result;
        bool responsive = SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result) != 0;
        auto now = clock::now();
        if (responsive) {
            if (healthy == clock::time_point{}) healthy = now;
            blocked = {};
            if (now - healthy >= std::chrono::seconds(2)) armed = true;
        } else {
            healthy = {};
            if (blocked == clock::time_point{}) blocked = now;
            if (now - blocked >= std::chrono::seconds(armed ? 5 : 8))
                return "captured: " + capture_hang(game, armed ? "Previously responsive window stopped processing WM_NULL for five seconds"
                                                               : "Window never became responsive during eight seconds of startup monitoring");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return "watch timed out without capture";
}

// ---- frame pacing

struct Row { double seconds; uint32_t frame; float game_seconds, real_seconds; uint32_t mode, cinematic; float dt, real_dt, cap;
             uint32_t pending, joining, job; double read_seconds; };

// Intervals only between consecutive single-step counter changes: a starved
// poller that misses updates is not evidence of one long game frame.
PacingSummary summarize_pacing(const std::vector<std::pair<double, uint32_t>>& rows) {
    PacingSummary s;
    for (size_t i = 1, prev = 0; i < rows.size(); ++i) {
        uint32_t delta = rows[i].second - rows[prev].second;  // uint32 wrap is natural
        if (!delta) continue;
        if (delta >= 0x80000000u) { ++s.resets; prev = i; continue; }  // counter reset
        s.advanced += delta;
        if (delta == 1) s.intervals_ms.push_back((rows[i].first - rows[prev].first) * 1000);
        else s.missed += delta - 1;
        prev = i;
    }
    std::sort(s.intervals_ms.begin(), s.intervals_ms.end());
    return s;
}

double PacingSummary::percentile(double fraction) const {
    if (intervals_ms.empty()) return 0;
    size_t rank = (size_t)std::ceil(fraction * intervals_ms.size());
    return intervals_ms[std::min(intervals_ms.size(), std::max<size_t>(rank, 1)) - 1];
}

int record_pacing(Game& game, double seconds, double poll_ms) {
    std::wstring folder = timestamped_folder(L"pacing", game.pid);
    std::cout << "Recording native update timing for " << seconds << " seconds; switch to the game and reproduce the judder." << std::endl;
    std::vector<Row> rows;
    std::string status = "complete";
    auto start = std::chrono::steady_clock::now();
    auto since = [&](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(t - start).count(); };
    try {
        while (since(std::chrono::steady_clock::now()) < seconds) {
            auto tick = std::chrono::steady_clock::now();
            Bytes clocks = game.read(0x124045C, 20), dts = game.read(0x10D4008, 8), flags = game.read(game.context + 0xB2, 2);
            Row r = {};
            r.seconds = since(tick);
            memcpy(&r.real_seconds, clocks.data(), 4);
            memcpy(&r.game_seconds, clocks.data() + 4, 4);
            memcpy(&r.frame, clocks.data() + 8, 4);
            memcpy(&r.mode, clocks.data() + 16, 4);
            memcpy(&r.dt, dts.data(), 4);
            memcpy(&r.real_dt, dts.data() + 4, 4);
            r.cinematic = game.u8(0x12A1EF8);
            r.cap = game.f32(game.context + 0x150);
            r.pending = (uint8_t)flags[0];
            r.joining = (uint8_t)flags[1];
            r.job = game.u32(0x1240478);
            r.read_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - tick).count();
            if (!std::isfinite(r.game_seconds) || !std::isfinite(r.real_seconds) || !std::isfinite(r.dt) || !std::isfinite(r.real_dt) || !std::isfinite(r.cap))
                throw Error("Non-finite clock observation; capture stopped");
            rows.push_back(r);
            if (game.u32(0x1240440) != game.context) throw Error("Game context changed; capture stopped");
            std::this_thread::sleep_for(std::chrono::microseconds((long long)(poll_ms * 1000)));
        }
    } catch (const std::exception& e) {
        status = e.what();
    }
    std::ofstream csv(folder + L"\\samples.csv", std::ios::binary);
    csv << "seconds,frame,game_seconds,real_seconds,timing_mode,cinematic_active,game_dt,real_dt,cap,loading_job_pending,loading_job_joining,loading_job_handle,read_seconds\r\n";
    char line[512];
    for (const Row& r : rows) {
        sprintf_s(line, "%.9g,%u,%.9g,%.9g,%u,%u,%.9g,%.9g,%.9g,%u,%u,%u,%.9g\r\n", r.seconds, r.frame, r.game_seconds, r.real_seconds,
                  r.mode, r.cinematic, r.dt, r.real_dt, r.cap, r.pending, r.joining, r.job, r.read_seconds);
        csv << line;
    }
    std::vector<std::pair<double, uint32_t>> samples;
    for (const Row& r : rows) samples.push_back({r.seconds, r.frame});
    PacingSummary s = summarize_pacing(samples);
    Json summary = Json::object();
    summary.set("status", Json::str(status));
    summary.set("samples", Json::num((unsigned long long)rows.size()));
    summary.set("advanced_frames", Json::num(s.advanced));
    summary.set("missed_counter_transitions", Json::num(s.missed));
    summary.set("counter_resets", Json::num(s.resets));
    summary.set("single_frame_intervals", Json::num((unsigned long long)s.intervals_ms.size()));
    summary.set("median_observed_ms", Json::real(s.percentile(.5)));
    summary.set("p95_observed_ms", Json::real(s.percentile(.95)));
    summary.set("maximum_observed_ms", Json::real(s.intervals_ms.empty() ? 0 : s.intervals_ms.back()));
    summary.set("scope", Json::str("Sampled accepted-update counter only; not GPU/movie presentation. Polling uncertainty applies."));
    write_text(folder + L"\\summary.json", summary.dump() + "\n");
    std::cout << summary.dump() << "\nOutput: " << utf8(folder) << std::endl;
    return 0;
}
