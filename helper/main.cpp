// SWTFU-Helper: runtime-only 60 FPS / borderless / controller fixes for the
// supported Steam build of Star Wars: The Force Unleashed. No game file is
// modified; restarting the game removes every change.
#include "borderless.h"
#include "diagnostics.h"
#include "fixes.h"
#include "fps.h"
#include <iostream>

int run_selftest();  // selftest.cpp

static const char* kUsage =
    "Usage: SWTFU-Helper <command>\n"
    "  fps apply | restore | inspect           60 FPS profile (game must be running)\n"
    "  controller [inspect]                    controller reconnect + launch-movie skip\n"
    "  borderless apply [--fps60] [--watch-hangs] | restore | inspect\n"
    "  hang capture | watch                    read-only hang dump into analysis\\hangs\n"
    "  pacing [seconds]                        read-only update pacing into analysis\\pacing\n"
    "  selftest                                offline checks (no game needed)\n";

static int fps(const std::string& action) {
    if (action != "apply" && action != "restore" && action != "inspect") throw Error(kUsage);
    Game game(find_game(), action != "inspect");
    if (action != "inspect") {
        bool changed = set_fps_profile(game, action == "apply" ? 60 : 30);
        std::cout << (changed ? "Changed." : "Already set.") << "\n";
        if (action == "apply" && game.is("tfu2"))
            std::cout << "TFU2 60 FPS is experimental: camera smoothing and Force-grip steering may still be\n"
                         "faster than at 30 (docs/tfu2-60fps.md). Restore 30 FPS.cmd or a restart undoes it.\n";
    }
    for (auto& site : inspect_fps(game)) std::cout << "  " << site.first << ": " << site.second << "\n";
    return 0;
}

static int controller(bool inspect) {
    Game game(find_game(), !inspect);
    game.require("tfu1", "The controller fix");
    if (!inspect) std::cout << (apply_controller(game) ? "Changed." : "Already applied.") << "\n";
    std::cout << "  controller_reconnect_and_launch_skip: " << (controller_installed(game) ? "on" : "off") << "\n";
    return 0;
}

static int run(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    std::string command = a.empty() ? "" : a[0], action = a.size() > 1 ? a[1] : "";
    if (command == "selftest") return run_selftest();
    if (command == "fps") return fps(action);
    if (command == "controller") return controller(action == "inspect");
    if (command == "borderless") {
        bool fps60 = false, watch = false;
        for (size_t i = 2; i < a.size(); ++i) {
            if (a[i] == "--fps60") fps60 = true;
            else if (a[i] == "--watch-hangs") watch = true;
            else throw Error(kUsage);
        }
        if (action == "apply") return borderless_apply(fps60, watch);
        if (action == "restore") return borderless_restore();
        if (action == "inspect") return borderless_inspect();
    }
    if (command == "hang" && (action == "capture" || action == "watch")) {
        Game game(find_game(), false);
        std::cout << (action == "capture" ? capture_hang(game, "Manual hang capture") : watch_for_hang(game, 600)) << "\n";
        return 0;
    }
    if (command == "pacing") {
        double seconds = action.empty() ? 45 : std::stod(action);
        if (seconds < 1 || seconds > 300) throw Error("Use 1..300 seconds");
        Game game(find_game(), false);
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
