#pragma once
#include "game.h"

int borderless_apply(const GameInfo& info, bool fps60, bool watch_hangs);  // starts the game if needed
int borderless_restore(const GameInfo& info);
int borderless_inspect(const GameInfo& info);

// VideoWindowed key in the game's Config.xml; edits keep every other byte.
bool config_value(const std::string& text, std::string& value);  // false = key absent
std::string edit_config(const std::string& data, const std::string& value, bool remove);
