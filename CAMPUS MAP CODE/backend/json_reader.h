// json_reader.h -- a small JSON parser (recursive descent). Reads data/campus_map.json and
// data/classrooms.json into a tree of JsonValue objects. Standard library only, C++14.
#pragma once
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace unigo {

// One JSON value. JSON is a tree: objects and arrays contain more values.
class JsonValue {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Type type() const { return type_; }
    bool isNull() const { return type_ == Null; }
    bool isNumber() const { return type_ == Number; }
    bool isString() const { return type_ == String; }
    bool isArray() const { return type_ == Array; }
    bool isObject() const { return type_ == Object; }

    bool asBool() const { return b_; }
    double asNumber() const { return n_; }
    const std::string& asString() const { return s_; }
    const std::vector<JsonValue>& items() const { return arr_; }                       // array elements
    const std::vector<std::pair<std::string, JsonValue>>& members() const { return obj_; } // object members, in file order

    // Object lookup; returns nullptr when the key is missing (or this isn't an object).
    const JsonValue* get(const std::string& key) const {
        for (const auto& kv : obj_) if (kv.first == key) return &kv.second;
        return nullptr;
    }

    // Text form of a scalar: strings as-is, numbers without a trailing ".0", booleans as true/false.
    std::string toText() const {
        if (type_ == String) return s_;
        if (type_ == Bool) return b_ ? "true" : "false";
        if (type_ == Number) {
            if (n_ == (long long)n_) return std::to_string((long long)n_);
            std::ostringstream o; o << n_; return o.str();
        }
        return "";
    }

private:
    friend class JsonParser;
    Type type_ = Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<JsonValue> arr_;
    std::vector<std::pair<std::string, JsonValue>> obj_;
};

// Recursive descent: one function per JSON grammar rule (value, object, array, string, number).
class JsonParser {
public:
    static JsonValue parse(const std::string& text) {
        JsonParser p(text);
        p.skipSpace();
        JsonValue v = p.parseValue(0);
        p.skipSpace();
        if (p.pos_ != text.size()) p.fail("unexpected text after the JSON value");
        return v;
    }

    static JsonValue parseFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open " + path);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
            text.erase(0, 3);                                   // skip a UTF-8 BOM (Notepad adds one)
        try { return parse(text); }
        catch (const std::exception& e) { throw std::runtime_error(path + ": " + e.what()); }
    }

private:
    explicit JsonParser(const std::string& t) : t_(t) {}

    [[noreturn]] void fail(const std::string& msg) const {
        size_t line = 1;
        for (size_t i = 0; i < pos_ && i < t_.size(); ++i) if (t_[i] == '\n') ++line;
        throw std::runtime_error("JSON error on line " + std::to_string(line) + ": " + msg);
    }
    void skipSpace() { while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r')) ++pos_; }
    char peek() const { return pos_ < t_.size() ? t_[pos_] : '\0'; }
    void expect(char c) { if (peek() != c) fail(std::string("expected '") + c + "'"); ++pos_; }
    bool consume(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (t_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    JsonValue parseValue(int depth) {
        if (depth > 64) fail("nested too deeply");               // protects the stack from hostile files
        JsonValue v;
        char c = peek();
        if (c == '{') { v.type_ = JsonValue::Object; parseObject(v, depth); }
        else if (c == '[') { v.type_ = JsonValue::Array; parseArray(v, depth); }
        else if (c == '"') { v.type_ = JsonValue::String; v.s_ = parseString(); }
        else if (c == '-' || (c >= '0' && c <= '9')) { v.type_ = JsonValue::Number; v.n_ = parseNumber(); }
        else if (consume("true")) { v.type_ = JsonValue::Bool; v.b_ = true; }
        else if (consume("false")) { v.type_ = JsonValue::Bool; v.b_ = false; }
        else if (consume("null")) { v.type_ = JsonValue::Null; }
        else fail("expected a value");
        return v;
    }

    void parseObject(JsonValue& v, int depth) {
        expect('{');
        skipSpace();
        if (peek() == '}') { ++pos_; return; }
        while (true) {
            skipSpace();
            if (peek() != '"') fail("expected a \"key\"");
            std::string key = parseString();
            skipSpace();
            expect(':');
            skipSpace();
            v.obj_.emplace_back(key, parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') { ++pos_; continue; }
            expect('}');
            return;
        }
    }

    void parseArray(JsonValue& v, int depth) {
        expect('[');
        skipSpace();
        if (peek() == ']') { ++pos_; return; }
        while (true) {
            skipSpace();
            v.arr_.push_back(parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') { ++pos_; continue; }
            expect(']');
            return;
        }
    }

    double parseNumber() {
        const char* start = t_.c_str() + pos_;
        char* end = nullptr;
        double d = std::strtod(start, &end);
        if (end == start) fail("bad number");
        pos_ += (size_t)(end - start);
        return d;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }

    unsigned parseHex4() {
        if (pos_ + 4 > t_.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= t_.size()) fail("unterminated string");
            char c = t_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= t_.size()) fail("unterminated string");
            char e = t_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && t_.compare(pos_, 2, "\\u") == 0) {   // surrogate pair
                        pos_ += 2;
                        unsigned lo = parseHex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape in string");
            }
        }
    }

    const std::string& t_;
    size_t pos_ = 0;
};

} // namespace unigo
