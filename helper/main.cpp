// SWTFU-Helper: runtime-only 60 FPS / borderless / controller fixes for the
// supported Steam build of Star Wars: The Force Unleashed. No game file is
// modified; restarting the game removes every change.
#include "borderless.h"
#include "diagnostics.h"
#include "fixes.h"
#include "fps.h"
#include <iostream>

int run_selftest();  // selftest.cpp
int run_verify(const GameInfo& info, const std::string& path);  // verify.cpp

static const char* kUsage =
    "Usage: SWTFU-Helper <tfu1|tfu2> <command>   (the game is always required)\n"
    "  fps apply | restore | inspect           60 FPS profile (game must be running)\n"
    "  controller [inspect]                    tfu1: controller reconnect, launch-movie skip, subtitle after pause\n"
    "  borderless apply [--fps60] [--watch-hangs] | restore | inspect   (apply starts the game if needed)\n"
    "  hang capture | watch                    tfu1: read-only hang dump into analysis\\hangs\n"
    "  pacing [seconds]                        tfu1: read-only update pacing into analysis\\pacing\n"
    "  verify <decoded.exe>                    patch sites match a decoded image (tools/decode_exe.py)\n"
    "Usage: SWTFU-Helper selftest              offline checks (no game needed)\n";

static int fps(const GameInfo& info, const std::string& action) {
    if (action != "apply" && action != "restore" && action != "inspect") throw Error(kUsage);
    Game game(find_game(info), action != "inspect");
    if (action != "inspect") {
        bool changed = set_fps_profile(game, action == "apply" ? 60 : 30);
        std::cout << (changed ? "Changed." : "Already set.") << "\n";
        if (action == "apply" && game.is("tfu2"))
            std::cout << "TFU2 60 FPS is experimental.\n"
                         "TFU2 - Restore 30 FPS.cmd or a restart undoes it.\n";
    }
    for (auto& site : inspect_fps(game)) std::cout << "  " << site.first << ": " << site.second << "\n";
    return 0;
}

static int controller(const GameInfo& info, bool inspect) {
    if (std::string(info.name) == "tfu2") {  // TFU2's XInput fallback flag is reset every frame (sub_62D250)
        std::cout << "TFU2 already handles controller reconnects; nothing to apply.\n";
        return 0;
    }
    Game game(find_game(info), !inspect);
    if (!inspect) {
        bool changed = apply_controller(game);
        changed |= apply_subtitle_pause(game);
        std::cout << (changed ? "Changed." : "Already applied.") << "\n";
    }
    std::cout << "  controller_reconnect_and_launch_skip: " << (controller_installed(game) ? "on" : "off") << "\n";
    std::cout << "  subtitle_kept_across_pause: " << (subtitle_pause_installed(game) ? "on" : "off") << "\n";
    return 0;
}

static int run(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.size() == 1 && a[0] == "selftest") return run_selftest();
    if (a.size() < 2) { std::cerr << kUsage; return 2; }
    const GameInfo& info = game_named(a[0]);
    std::string command = a[1], action = a.size() > 2 ? a[2] : "";
    if (command == "verify" && a.size() == 3) return run_verify(info, a[2]);
    if (command == "fps") return fps(info, action);
    if (command == "controller") return controller(info, action == "inspect");
    if (command == "borderless") {
        bool fps60 = false, watch = false;
        for (size_t i = 3; i < a.size(); ++i) {
            if (a[i] == "--fps60") fps60 = true;
            else if (a[i] == "--watch-hangs") watch = true;
            else throw Error(kUsage);
        }
        if (action == "apply") return borderless_apply(info, fps60, watch);
        if (action == "restore") return borderless_restore(info);
        if (action == "inspect") return borderless_inspect(info);
    }
    if (command == "hang" && (action == "capture" || action == "watch")) {
        Game game(find_game(info), false);
        std::cout << (action == "capture" ? capture_hang(game, "Manual hang capture") : watch_for_hang(game, 600)) << "\n";
        return 0;
    }
    if (command == "pacing") {
        double seconds = action.empty() ? 45 : std::stod(action);
        if (seconds < 1 || seconds > 300) throw Error("Use 1..300 seconds");
        Game game(find_game(info), false);
        game.require("tfu1", "Pacing capture");
        return record_pacing(game, seconds, 2);
    }
    std::cerr << kUsage;
    return 2;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
