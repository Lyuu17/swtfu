#pragma once
#include "game.h"

bool apply_controller(Game& game);        // reconnect + launch-movie skip; idempotent
bool controller_installed(Game& game);
bool apply_subtitle_pause(Game& game);    // subtitle line survives the pause menu; idempotent
bool subtitle_pause_installed(Game& game);
bool apply_playback_fixes(Game& game);    // movie message pump, cursor, resize, background loop
bool apply_renderer_reset(Game& game);
