#pragma once
#include "game.h"
#include <utility>

std::string capture_hang(Game& game, const std::string& reason);  // returns output folder
std::string watch_for_hang(Game& game, int seconds);              // returns outcome text
int record_pacing(Game& game, double seconds, double poll_ms);

struct PacingSummary {
    unsigned long long advanced = 0, missed = 0, resets = 0;
    std::vector<double> intervals_ms;  // sorted single-update intervals
    double percentile(double fraction) const;
};
PacingSummary summarize_pacing(const std::vector<std::pair<double, uint32_t>>& rows);  // (seconds, frame counter)
