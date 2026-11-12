// `SWTFU-Helper selftest`: offline checks of the logic that touches user files
// or summarizes data (no game needed). Run after every build.
#include "borderless.h"
#include "diagnostics.h"
#include <cmath>
#include <iostream>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; } } while (0)

static bool rejects(const std::string& data) {
    try { edit_config(data, "1", false); return false; } catch (const Error&) { return true; }
}

static std::string value_of(const std::string& data) {
    std::string value;
    return config_value(data, value) ? value : "<absent>";
}

static void config_tests() {
    // Missing key round trip preserves the entire file.
    std::string original = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\r\n<r>\r\n\t<s id=\"VideoWidth\">1920</s>\r\n\t<s id=\"Jump\">DIK_SPACE</s>\r\n</r>\r\n";
    std::string updated = edit_config(original, "1", false);
    CHECK(value_of(updated) == "1");
    CHECK(edit_config(updated, "", true) == original);
    // Existing key keeps other settings, comments and quoting.
    original = "<r>\n<!-- keep -->\n<s id='VideoWindowed'>0</s>\n<s id='ClothLevel'>1</s>\n</r>";
    std::string expected = original;
    expected.replace(expected.find(">0</s>"), 6, ">1</s>");
    CHECK(edit_config(original, "1", false) == expected);
    CHECK(edit_config(edit_config(original, "1", false), "0", false) == original);
    // Restore keeps changes the user made meanwhile.
    original = "<r>\n<s id=\"VideoWindowed\">0</s>\n<s id=\"VideoWidth\">1920</s>\n</r>";
    updated = edit_config(original, "1", false);
    updated.replace(updated.find(">1920<"), 6, ">2560<");
    std::string restored = edit_config(updated, "0", false);
    CHECK(restored.find(">2560<") != std::string::npos && value_of(restored) == "0");
    // Inline removal keeps the adjacent entry.
    CHECK(edit_config("<r><s id=\"VideoWindowed\">1</s><s id=\"Jump\">DIK_SPACE</s></r>", "", true) == "<r><s id=\"Jump\">DIK_SPACE</s></r>");
    // Rejections: duplicate key, unexpected root.
    CHECK(rejects("<r><s id=\"VideoWindowed\">1</s><s id=\"VideoWindowed\">0</s></r>"));
    CHECK(rejects("<settings><s id=\"VideoWindowed\">0</s></settings>"));
    // Unchanged key is byte-identical.
    original = "<r>\n  <s id=\"VideoWindowed\">1</s>\n</r>\n";
    CHECK(edit_config(original, "1", false) == original);
}

static void pacing_tests() {
    typedef std::vector<std::pair<double, uint32_t>> Rows;
    // Repeated counter samples keep the first observation as the interval anchor.
    PacingSummary s = summarize_pacing(Rows{{0, 5}, {.010, 5}, {.016, 6}, {.020, 6}, {.032, 7}});
    CHECK(s.intervals_ms.size() == 2 && std::fabs(s.percentile(.5) - 16) < 1e-9);
    // A starved observer missing many frames is not one long game frame.
    s = summarize_pacing(Rows{{0, 10}, {.500, 40}, {.516, 41}});
    CHECK(s.missed == 29 && s.intervals_ms.size() == 1 && std::fabs(s.intervals_ms.back() - 16) < 1e-9);
    // uint32 wrap is distinct from a counter reset.
    s = summarize_pacing(Rows{{0, 0xFFFFFFFFu}, {.016, 0}, {.032, 1}});
    CHECK(s.advanced == 2 && s.resets == 0);
    s = summarize_pacing(Rows{{0, 50}, {.016, 0}, {.032, 1}});
    CHECK(s.resets == 1 && s.advanced == 1);
    CHECK(summarize_pacing(Rows{}).intervals_ms.empty());
}

int run_selftest() {
    config_tests();
    pacing_tests();
    std::cout << (failures ? "selftest FAILED" : "selftest passed: config edits, pacing summary") << "\n";
    return failures ? 1 : 0;
}
