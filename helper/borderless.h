#pragma once
#include "game.h"

int borderless_apply(bool fps60, bool watch_hangs);
int borderless_restore();
int borderless_inspect();

// VideoWindowed key in the game's Config.xml; edits keep every other byte.
bool config_value(const std::string& text, std::string& value);  // false = key absent
std::string edit_config(const std::string& data, const std::string& value, bool remove);
