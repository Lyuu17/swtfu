#include "json.h"

namespace {
struct Parser {
    const std::string& s;
    size_t p = 0;
    void space() { while (p < s.size() && isspace((unsigned char)s[p])) ++p; }
    [[noreturn]] void bad() { throw Error("Invalid JSON state file"); }
    void expect(char c) { space(); if (p >= s.size() || s[p] != c) bad(); ++p; }

    std::string string() {
        expect('"');
        std::string out;
        while (p < s.size() && s[p] != '"') {
            char c = s[p++];
            if (c != '\\') { out += c; continue; }
            if (p >= s.size()) bad();
            char e = s[p++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (p + 4 > s.size()) bad();
                    unsigned code = std::stoul(s.substr(p, 4), nullptr, 16);
                    p += 4;
                    if (code >= 0xD800 && code < 0xDC00 && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        unsigned low = std::stoul(s.substr(p + 2, 4), nullptr, 16);
                        p += 6;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    // UTF-8 encode
                    if (code < 0x80) out += (char)code;
                    else if (code < 0x800) { out += (char)(0xC0 | code >> 6); out += (char)(0x80 | (code & 63)); }
                    else if (code < 0x10000) { out += (char)(0xE0 | code >> 12); out += (char)(0x80 | ((code >> 6) & 63)); out += (char)(0x80 | (code & 63)); }
                    else { out += (char)(0xF0 | code >> 18); out += (char)(0x80 | ((code >> 12) & 63)); out += (char)(0x80 | ((code >> 6) & 63)); out += (char)(0x80 | (code & 63)); }
                    break;
                }
                default: out += e;  // \" \\ \/
            }
        }
        if (p >= s.size()) bad();
        ++p;
        return out;
    }

    Json value() {
        space();
        if (p >= s.size()) bad();
        Json j;
        char c = s[p];
        if (c == '{') {
            j.type = Json::Object; ++p; space();
            if (s[p] == '}') { ++p; return j; }
            for (;;) {
                std::string key = string();
                expect(':');
                j.keys[key] = value();
                space();
                if (s[p] == ',') { ++p; continue; }
                expect('}');
                return j;
            }
        }
        if (c == '[') {
            j.type = Json::Array; ++p; space();
            if (s[p] == ']') { ++p; return j; }
            for (;;) {
                j.items.push_back(value());
                space();
                if (s[p] == ',') { ++p; continue; }
                expect(']');
                return j;
            }
        }
        if (c == '"') { j.type = Json::String; j.text = string(); return j; }
        if (!s.compare(p, 4, "null")) { p += 4; return j; }
        if (!s.compare(p, 4, "true")) { p += 4; return Json::boolean(true); }
        if (!s.compare(p, 5, "false")) { p += 5; return Json::boolean(false); }
        size_t start = p;
        while (p < s.size() && strchr("+-0123456789.eE", s[p])) ++p;
        if (start == p) bad();
        j.type = Json::Number;
        j.text = s.substr(start, p - start);
        return j;
    }
};

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { char t[8]; sprintf_s(t, "\\u%04x", c); out += t; }
        else out += (char)c;
    }
    return out + "\"";
}
}  // namespace

Json Json::parse(const std::string& input) {
    Parser parser{input};
    Json result = parser.value();
    parser.space();
    if (parser.p != input.size()) parser.bad();
    return result;
}

std::string Json::dump(int indent) const {
    std::string pad(indent + 2, ' '), end(indent, ' ');
    switch (type) {
        case Null: return "null";
        case Bool: case Number: return text;
        case String: return quote(text);
        case Array: {
            if (items.empty()) return "[]";
            std::string out = "[";
            for (size_t i = 0; i < items.size(); ++i) out += (i ? ", " : "") + items[i].dump(indent);
            return out + "]";
        }
        case Object: {
            if (keys.empty()) return "{}";
            std::string out = "{\n";
            size_t i = 0;
            for (auto& kv : keys) out += pad + quote(kv.first) + ": " + kv.second.dump(indent + 2) + (++i < keys.size() ? ",\n" : "\n");
            return out + end + "}";
        }
    }
    return "null";
}
