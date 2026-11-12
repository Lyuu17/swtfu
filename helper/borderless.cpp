// Borderless fullscreen launcher (mirrors swtfu/borderless.py).
// Never activates the game window or changes the desktop display mode.
#include "borderless.h"
#include "diagnostics.h"
#include "fixes.h"
#include "fps.h"
#include "json.h"
#include <shellapi.h>
#include <tlhelp32.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <thread>

static const char* kAwlHash = "a1bb7219476245aa9a5ceac92448133f1a805dee947de6603f52a5e6f8de77fe";

static std::string read_file(const std::wstring& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error("Cannot read " + utf8(path));
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

static void write_file(const std::wstring& path, const std::string& data) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || !stream.write(data.data(), data.size())) throw Error("Cannot write " + utf8(path));
}

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static std::wstring state_path() { return exe_dir() + L"\\borderless-state.json"; }

static void save_state(const Json& state) {
    std::wstring temp = state_path() + L".tmp";
    write_file(temp, state.dump() + "\n");
    if (!MoveFileExW(temp.c_str(), state_path().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) fail_win("MoveFileEx");
}

// ---- active configuration file

static std::wstring local_appdata() {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (!n || n >= MAX_PATH) throw Error("LOCALAPPDATA is not set");
    return buffer;
}

static std::wstring active_config(Game& game) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, game.pid);
    if (snapshot == INVALID_HANDLE_VALUE) fail_win("CreateToolhelp32Snapshot");
    MODULEENTRY32W entry = {sizeof(entry)};
    uint32_t base = 0;
    std::wstring dll;
    for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry))
        if (!_wcsicmp(entry.szModule, L"AWL_Release.dll")) { base = (uint32_t)(uintptr_t)entry.modBaseAddr; dll = entry.szExePath; break; }
    CloseHandle(snapshot);
    if (!base) throw Error("AWL_Release.dll is not loaded");
    if (sha256_file(dll) != kAwlHash) throw Error("Unsupported AWL library; cannot identify the active configuration safely");
    // MSVC std::wstring: buffer/pointer at +4, length at +20, capacity at +24.
    auto wide_string = [&](uint32_t offset) {
        Bytes obj = game.read(base + offset, 28);
        uint32_t length, capacity, pointer;
        memcpy(&length, obj.data() + 20, 4);
        memcpy(&capacity, obj.data() + 24, 4);
        if (!length || length > 200 || capacity < length) throw Error("Unexpected AWL configuration path metadata");
        if (capacity >= 8) memcpy(&pointer, obj.data() + 4, 4); else pointer = base + offset + 4;
        Bytes raw = game.read(pointer, 2 * length);
        std::wstring text((const wchar_t*)raw.data(), length);
        if (text.find_first_of(L"\\/:") != std::wstring::npos || text.find(L'\0') != std::wstring::npos || text == L"." || text == L"..")
            throw Error("Unexpected configuration folder name");
        return text;
    };
    std::wstring company = wide_string(0x54D40 + 60), title = wide_string(0x54D40 + 88);
    if (company != L"Aspyr" || title.compare(0, 10, L"Star Wars ")) throw Error("Unexpected game configuration location");
    return local_appdata() + L"\\" + company + L"\\" + title + L"\\Config.xml";
}

// ---- VideoWindowed key (regex edit keeps the file's formatting byte-for-byte)
// ponytail: no full XML parser; a root check + exact entry regex like the Python version's edit path.

static const std::regex kEntry("<s\\b[^>]*\\bid\\s*=\\s*([\"'])VideoWindowed\\1[^>]*>([\\s\\S]*?)</s\\s*>");

// Returns false when the key is absent.
bool config_value(const std::string& text, std::string& value) {
    if (!std::regex_search(text, std::regex("^(\\xEF\\xBB\\xBF)?\\s*(<\\?xml[^>]*\\?>\\s*)?(<!--[\\s\\S]*?-->\\s*)*<r[\\s>]")))
        throw Error("Unexpected configuration root");
    auto begin = std::sregex_iterator(text.begin(), text.end(), kEntry);
    size_t count = std::distance(begin, std::sregex_iterator());
    if (count > 1) throw Error("Duplicate VideoWindowed keys; configuration left unchanged");
    if (!count) return false;
    value = (*begin)[2].str();
    return true;
}

// value empty + remove=true deletes the key.
std::string edit_config(const std::string& data, const std::string& value, bool remove) {
    std::string old;
    bool present = config_value(data, old);
    if (remove ? !present : (present && old == value)) return data;
    std::string text = data;
    std::smatch match;
    if (present && std::regex_search(data, match, kEntry)) {
        size_t start = match.position(0), end = start + match.length(0);
        if (remove) {
            size_t line_start = text.rfind('\n', start == 0 ? 0 : start - 1);
            line_start = line_start == std::string::npos ? 0 : line_start + 1;
            size_t line_end = text.find('\n', end);
            auto blank = [](const std::string& s) { return s.find_first_not_of(" \t\r") == std::string::npos; };
            if (line_end != std::string::npos && blank(text.substr(line_start, start - line_start)) && blank(text.substr(end, line_end - end)))
                text.erase(line_start, line_end + 1 - line_start);
            else
                text.erase(start, end - start);
        } else {
            size_t inner = match.position(2);
            text.replace(inner, match.length(2), value);
        }
    } else if (!remove) {
        size_t close = text.rfind("</r>");
        if (close == std::string::npos) throw Error("Unsupported configuration format");
        std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        text.insert(close, "\t<s id=\"VideoWindowed\">" + value + "</s>" + newline);
    }
    std::string check;
    bool now = config_value(text, check);
    if (remove ? now : (!now || check != value)) throw Error("Configuration edit did not verify");
    return text;
}

static void write_config(const std::wstring& path, const std::string& data) {
    std::wstring allowed = local_appdata() + L"\\Aspyr\\";
    if (_wcsnicmp(path.c_str(), allowed.c_str(), allowed.size()) || path.size() < 11 || _wcsicmp(path.c_str() + path.size() - 11, L"\\Config.xml"))
        throw Error("Refusing to change a file outside the game's Aspyr configuration folder");
    std::wstring temp = path.substr(0, path.size() - 10) + L"Config.swtfu-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    write_file(temp, data);
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::string message = win_error("MoveFileEx");
        DeleteFileW(temp.c_str());
        throw Error(message);
    }
}

static void prepare_config(const std::wstring& path, Json& state) {
    std::string original = read_file(path), value;
    bool present = config_value(original, value);
    if (present && value != "0" && value != "1") throw Error("Unexpected VideoWindowed value; configuration left unchanged");
    if (!state.has("config")) {
        Json config = Json::object();
        config.set("path", Json::str(utf8(path)));
        config.set("original_value", present ? Json::str(value) : Json());
        state.set("config", config);
        write_file(exe_dir() + L"\\Config.before-borderless.xml", original);
        save_state(state);
    } else if (_wcsicmp(widen(state["config"]["path"].text).c_str(), path.c_str())) {
        throw Error("A different configuration already has a backup; restore it first");
    }
    std::string updated = edit_config(original, "1", false);
    if (updated != original) write_config(path, updated);
    std::string check;
    if (!config_value(read_file(path), check) || check != "1") throw Error("Could not verify native windowed-mode configuration");
}

// ---- game window

struct Display { HWND hwnd; bool windowed; uint32_t device; int width, height; };

static Display game_display(Game& game) {
    uint32_t renderer = game.u32(0x13BB5D4);
    if (renderer < 0x10000) throw Error("Renderer is not initialized");
    return {(HWND)(uintptr_t)game.u32(renderer + 408), game.u32(renderer + 80) != 0, game.u32(renderer + 44),
            (int)game.u32(renderer + 48), (int)game.u32(renderer + 52)};
}

// TFU2: no mapped renderer struct; use the game's largest visible top-level window.
// Windowed = it still has a caption (exclusive fullscreen and borderless have none).
static HWND main_window(DWORD pid) {
    struct Search { DWORD pid; HWND best; long area; } search = {pid, nullptr, 0};
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        Search& s = *(Search*)param;
        DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        RECT r;
        if (owner == s.pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER) && GetWindowRect(hwnd, &r)) {
            long area = (r.right - r.left) * (r.bottom - r.top);
            if (area > s.area) { s.best = hwnd; s.area = area; }
        }
        return TRUE;
    }, (LPARAM)&search);
    if (!search.best) throw Error("Game window not found");
    return search.best;
}

static Display window_of(Game& game) {
    if (game.is("tfu1")) return game_display(game);
    HWND hwnd = main_window(game.pid);
    RECT r = {};
    GetClientRect(hwnd, &r);
    return {hwnd, (GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION, 1, (int)r.right, (int)r.bottom};
}

struct DpiScope {  // per-monitor-v2 coordinates for this thread only
    DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~DpiScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
};

static void verify_owner(HWND hwnd, DWORD pid) {
    DWORD owner = 0;
    if (!GetWindowThreadProcessId(hwnd, &owner) || owner != pid)
        throw Error("Game window is unavailable from this desktop; run this tool in your normal session");
}

static RECT window_rect(HWND hwnd) {
    RECT r;
    if (!GetWindowRect(hwnd, &r)) fail_win("GetWindowRect");
    return r;
}

static bool same(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }

static void change_window(HWND hwnd, DWORD style, DWORD exstyle, const RECT& r) {
    bool topmost_changed = ((GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != ((exstyle & WS_EX_TOPMOST) != 0);
    for (auto field : {std::make_pair(GWL_STYLE, style), std::make_pair(GWL_EXSTYLE, exstyle)}) {
        SetLastError(0);
        if (!SetWindowLongW(hwnd, field.first, (LONG)field.second) && GetLastError()) fail_win("SetWindowLong");
    }
    // Keep Z order unless topmost changes; SWP_NOACTIVATE keeps focus where the user has it.
    UINT flags = SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER | (topmost_changed ? 0 : SWP_NOZORDER);
    if (!SetWindowPos(hwnd, (exstyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, flags))
        fail_win("SetWindowPos");
    if ((DWORD)GetWindowLongW(hwnd, GWL_STYLE) != style || (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE) != exstyle)
        throw Error("Window-style change did not verify");
}

static Json rect_json(const RECT& r) {
    Json a = Json::array();
    for (long v : {r.left, r.top, r.right, r.bottom}) a.items.push_back(Json::num((long long)v));
    return a;
}

static RECT json_rect(const Json& a) {
    if (a.items.size() != 4) throw Error("Invalid saved window rectangle");
    return {(LONG)a.items[0].i64(), (LONG)a.items[1].i64(), (LONG)a.items[2].i64(), (LONG)a.items[3].i64()};
}

static bool saved_window_matches(const Json& state, Game& game, HWND hwnd) {
    if (!state.has("window")) return false;
    const Json& w = state["window"];
    return w["pid"].u64() == game.pid && w["hwnd"].u64() == (uintptr_t)hwnd && w.has("creation_time") && w["creation_time"].u64() == game.creation_time;
}

static void apply_window(Game& game, Json& state) {
    Display display = window_of(game);
    if (!display.windowed)
        throw Error(game.is("tfu1") ? "D3D is still in exclusive fullscreen; native windowed mode requires a restart"
                                    : "The game window has no title bar: set windowed mode in the game's options (Win.Windowed), or it is already borderless");
    DpiScope dpi;
    verify_owner(display.hwnd, game.pid);
    if (!saved_window_matches(state, game, display.hwnd)) {
        Json w = Json::object();
        w.set("pid", Json::num((unsigned long long)game.pid));
        w.set("creation_time", Json::num((unsigned long long)game.creation_time));
        w.set("hwnd", Json::num((unsigned long long)(uintptr_t)display.hwnd));
        w.set("style", Json::num((unsigned long long)(DWORD)GetWindowLongW(display.hwnd, GWL_STYLE)));
        w.set("exstyle", Json::num((unsigned long long)(DWORD)GetWindowLongW(display.hwnd, GWL_EXSTYLE)));
        w.set("rect", rect_json(window_rect(display.hwnd)));
        state.set("window", w);
        save_state(state);
    }
    const Json& original = state["window"];
    MONITORINFO info = {sizeof(info)};
    HMONITOR monitor = MonitorFromWindow(display.hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) fail_win("GetMonitorInfo");
    DWORD style = (DWORD)original["style"].u64(), exstyle = (DWORD)original["exstyle"].u64();
    try {
        // Drop caption/frame/system-menu bits, add WS_POPUP; drop edge/topmost ex-styles.
        change_window(display.hwnd, (style & ~0x00CF0000u) | WS_POPUP, exstyle & ~0x00020309u, info.rcMonitor);
        if (!same(window_rect(display.hwnd), info.rcMonitor)) throw Error("Window rectangle does not match the monitor");
    } catch (...) {
        change_window(display.hwnd, style, exstyle, json_rect(original["rect"]));
        throw;
    }
}

static void wait_for_exit(Game& game) {
    std::cout << "Close the game normally once. Waiting here; no need to keep it focused. Ctrl+C cancels." << std::endl;
    while (game.running()) sleep_ms(500);
}

static std::unique_ptr<Game> launch_and_wait() {
    ShellExecuteW(nullptr, L"open", L"steam://rungameid/32430", nullptr, nullptr, SW_SHOWNORMAL);
    std::cout << "Waiting for the Steam game to initialize (up to 180 seconds)." << std::endl;
    std::string last = "Game not started";
    for (auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180); std::chrono::steady_clock::now() < deadline; sleep_ms(500)) {
        try {
            std::unique_ptr<Game> game(new Game(find_game(), false));
            Display display = game_display(*game);
            if (display.hwnd && display.device) return game;
        } catch (const std::exception& e) {
            last = e.what();
        }
    }
    throw Error("Game did not initialize: " + last + ". Launch it through Steam and run this command again.");
}

// ---- commands

static Json load_state() {
    DWORD attributes = GetFileAttributesW(state_path().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return Json::object();
    return Json::parse(read_file(state_path()));
}

int borderless_restore() {
    Json state = load_state();
    if (state.keys.empty()) { std::cout << "No borderless changes recorded." << std::endl; return 0; }
    std::unique_ptr<Game> game;
    try { game.reset(new Game(find_game(), false)); }
    catch (const Error& e) { if (std::string(e.what()) != kNotRunning) throw; }
    if (game) {
        Display display = window_of(*game);
        if (saved_window_matches(state, *game, display.hwnd)) {
            DpiScope dpi;
            const Json& w = state["window"];
            verify_owner(display.hwnd, game->pid);
            change_window(display.hwnd, (DWORD)w["style"].u64(), (DWORD)w["exstyle"].u64(), json_rect(w["rect"]));
        }
        // AWL saves its cached XML on exit; restore the key afterwards so it isn't overwritten.
        if (state.has("config")) wait_for_exit(*game);
    }
    if (state.has("config")) {
        const Json& saved = state["config"];
        std::wstring path = widen(saved["path"].text);
        std::string data = read_file(path), value;
        bool present = config_value(data, value);
        bool original_absent = saved["original_value"].type == Json::Null;
        bool ok = (present && value == "1") || (original_absent ? !present : (present && value == saved["original_value"].text));
        if (!ok) throw Error("VideoWindowed was changed elsewhere; leaving it unchanged");
        write_config(path, edit_config(data, original_absent ? "" : saved["original_value"].text, original_absent));
    }
    DeleteFileW(state_path().c_str());
    std::cout << "Borderless configuration restored. Next launch uses the original display mode." << std::endl;
    return 0;
}

int borderless_inspect() {
    Game game(find_game(), false);
    Display d = window_of(game);
    if (game.is("tfu2")) {
        std::cout << "pid " << game.pid << "\ngame tfu2\nwindowed " << d.windowed << "\nsize " << d.width << "x" << d.height << std::endl;
        return 0;
    }
    std::cout << "pid " << game.pid << "\nactive_config " << utf8(active_config(game))
              << "\nwindowed " << d.windowed << "\nsize " << d.width << "x" << d.height << std::endl;
    return 0;
}

int borderless_apply(bool fps60, bool watch_hangs) {
    Json log = Json::object();
    log.set("patch_profile", Json::str("native60-borderless-playback-v16-cpp"));
    std::wstring log_path = exe_dir() + L"\\borderless-last-run.json";
    std::unique_ptr<Game> game;
    try {
        Json state = load_state();
        try { game.reset(new Game(find_game(), false)); }
        catch (const Error& e) {
            if (std::string(e.what()) != kNotRunning) throw;
            game = launch_and_wait();
        }
        log.set("game", Json::str(game->info->name));
        if (game->is("tfu2")) {
            // TFU2 handles resizes itself (its D3D reset accepts a pending resize) and its controller
            // fallback is not sticky, so only the window style changes. Windowed mode is the
            // game's own Win.Windowed option.
            if (fps60) {
                Game writable(Found{game->pid, game->info}, true);
                log.set("fps_limit_changed", Json::boolean(set_fps_profile(writable, 60)));
                std::cout << "TFU2 60 FPS is experimental (docs/tfu2-60fps.md)." << std::endl;
            }
            apply_window(*game, state);
            std::cout << "Borderless applied." << (watch_hangs ? " (Hang watch is TFU1-only.)" : "") << std::endl;
            write_file(log_path, log.dump() + "\n");
            return 0;
        }
        std::wstring config = active_config(*game);
        log.set("pid", Json::num((unsigned long long)game->pid));
        log.set("active_config", Json::str(utf8(config)));
        if (!game_display(*game).windowed) {
            std::cout << "A restart into native windowed mode is required." << std::endl;
            wait_for_exit(*game);
            game.reset();
            prepare_config(config, state);
            game = launch_and_wait();
        }
        {
            // Fix the healthy-device resize path before the border/size change generates WM_SIZE.
            Game writable(Found{game->pid, game->info}, true);
            log.set("controller_fixes_changed", Json::boolean(apply_controller(writable)));  // launch movies are already playing
            log.set("playback_fixes_changed", Json::boolean(apply_playback_fixes(writable)));
            log.set("renderer_reset_fix_changed", Json::boolean(apply_renderer_reset(writable)));
            if (fps60) log.set("fps_limit_changed", Json::boolean(set_fps_profile(writable, 60)));
        }
        apply_window(*game, state);
        std::cout << "Applied. Recording five seconds of read-only timing; no need to focus the game." << std::endl;
        uint32_t frames0 = game->u32(0x1240464);
        float sim0 = game->f32(0x1240460);
        auto start = std::chrono::steady_clock::now();
        sleep_ms(5000);
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        uint32_t frames = game->u32(0x1240464) - frames0;
        log.set("game_updates_per_second", Json::real(frames / elapsed));
        log.set("simulation_seconds", Json::real(game->f32(0x1240460) - sim0));
        printf("Game updates per second: %.1f (native limit %d)\n", frames / elapsed, (int)game->f32(game->context + 0x150));
        std::cout << "Borderless applied." << std::endl;
        write_file(log_path, log.dump() + "\n");
        if (watch_hangs) {
            std::string result = watch_for_hang(*game, 600);
            log.set("hang_watch", Json::str(result));
        }
    } catch (const std::exception& e) {
        log.set("error", Json::str(e.what()));
        write_file(log_path, log.dump() + "\n");
        throw;
    }
    write_file(log_path, log.dump() + "\n");
    return 0;
}
