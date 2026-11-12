// Minimal JSON for the helper's own small state/log files. Numbers keep their
// raw text so 64-bit values (process creation times) survive round trips.
#pragma once
#include "common.h"
#include <map>
#include <memory>

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    std::string text;                  // Number (raw) / String / Bool ("true"/"false")
    std::vector<Json> items;           // Array
    std::map<std::string, Json> keys;  // Object (sorted keys; fine for our files)

    static Json str(const std::string& s) { Json j; j.type = String; j.text = s; return j; }
    static Json num(long long v) { Json j; j.type = Number; j.text = std::to_string(v); return j; }
    static Json num(unsigned long long v) { Json j; j.type = Number; j.text = std::to_string(v); return j; }
    static Json real(double v) { Json j; j.type = Number; char t[64]; sprintf_s(t, "%.6g", v); j.text = t; return j; }
    static Json boolean(bool v) { Json j; j.type = Bool; j.text = v ? "true" : "false"; return j; }
    static Json object() { Json j; j.type = Object; return j; }
    static Json array() { Json j; j.type = Array; return j; }

    bool has(const std::string& key) const { return type == Object && keys.count(key); }
    const Json& operator[](const std::string& key) const {
        auto it = keys.find(key);
        if (type != Object || it == keys.end()) throw Error("Missing state field '" + key + "'");
        return it->second;
    }
    Json& set(const std::string& key, const Json& value) { type = Object; return keys[key] = value; }
    unsigned long long u64() const { return std::stoull(text); }
    long long i64() const { return std::stoll(text); }

    static Json parse(const std::string& input);
    std::string dump(int indent = 0) const;
};
