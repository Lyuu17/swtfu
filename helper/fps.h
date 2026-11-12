#pragma once
#include "game.h"
#include <utility>

bool set_fps_profile(Game& game, int fps);  // 30 or 60; true if anything changed
std::vector<std::pair<std::string, std::string>> inspect_fps(Game& game);

// Install (or remove) one helper hook group; true if anything changed.
bool set_hook_group(Game& game, const HookGroup& group, bool enabled);
