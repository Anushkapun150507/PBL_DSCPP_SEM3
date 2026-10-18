// json_writer.h -- builds JSON text safely (escaping + commas handled for you).
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace unigo {

// Usage: JsonWriter j; j.beginObject().key("count").num(3).key("ok").boolean(true).endObject();
// A stack (vector<bool>) remembers, for each open object/array, whether a comma is needed
// before the next item -- the same idea as matching brackets with a stack.
class JsonWriter {
public:
    JsonWriter& beginObject() { open('{'); return *this; }
    JsonWriter& endObject()   { close('}'); return *this; }
    JsonWriter& beginArray()  { open('['); return *this; }
    JsonWriter& endArray()    { close(']'); return *this; }

    JsonWriter& key(const std::string& k) {
        separator();
        out_ += quote(k);
        out_ += ':';
        afterKey_ = true;
        return *this;
    }
    JsonWriter& str(const std::string& v) { separator(); out_ += quote(v); return *this; }
    JsonWriter& num(long long v)          { separator(); out_ += std::to_string(v); return *this; }
    JsonWriter& boolean(bool v)           { separator(); out_ += v ? "true" : "false"; return *this; }
    JsonWriter& null()                    { separator(); out_ += "null"; return *this; }

    const std::string& text() const { return out_; }

private:
    void open(char c)  { separator(); out_ += c; firstItem_.push_back(true); }
    void close(char c) { out_ += c; if (!firstItem_.empty()) firstItem_.pop_back(); }

    void separator() {
        if (afterKey_) { afterKey_ = false; return; }          // value right after "key": no comma
        if (firstItem_.empty()) return;                         // top level
        if (!firstItem_.back()) out_ += ',';
        firstItem_.back() = false;
    }

    static std::string quote(const std::string& s) {
        std::string o = "\"";
        for (unsigned char c : s) {
            switch (c) {
                case '"':  o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:
                    if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                    else o += (char)c;
            }
        }
        return o + "\"";
    }

    std::string out_;
    std::vector<bool> firstItem_;   // stack: one entry per open { or [
    bool afterKey_ = false;
};

} // namespace unigo
