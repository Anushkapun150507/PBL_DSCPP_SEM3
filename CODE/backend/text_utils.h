// text_utils.h -- small text helpers shared by the whole backend (no classes, no state).
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace unigo {

// Removes spaces/tabs/newlines at both ends; keeps the original letter case (for display).
inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Search key form: trimmed, lowercase, inner runs of spaces collapsed ("  Prince   CHOWK" -> "prince chowk").
inline std::string normalize(const std::string& s) {
    std::string out;
    bool space = false;
    for (unsigned char c : trim(s)) {
        if (std::isspace(c)) { space = true; continue; }
        if (space) { out += ' '; space = false; }
        out += (char)std::tolower(c);
    }
    return out;
}

inline std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : s) {
        if (c == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

// Time text -> minutes since midnight, or -1 if it is not a time ("No Service", "", "abc").
// Accepts "1:10 PM", "1:10pm", "13:10" and "13:10:00" (what Excel exports for a time cell).
inline int toMinutes(const std::string& raw) {
    std::string t = normalize(raw);
    int h = 0, m = 0, used = 0;
    if (std::sscanf(t.c_str(), "%d:%d%n", &h, &m, &used) != 2 || m < 0 || m > 59) return -1;
    size_t p = (size_t)used;
    if (p < t.size() && t[p] == ':') {                         // optional ":ss" seconds
        ++p;
        while (p < t.size() && std::isdigit((unsigned char)t[p])) ++p;
    }
    while (p < t.size() && t[p] == ' ') ++p;
    std::string suffix = t.substr(p);
    if (suffix.empty()) return (h >= 0 && h <= 23) ? h * 60 + m : -1;   // 24-hour clock
    if (h < 1 || h > 12) return -1;                                      // 12-hour clock
    if (suffix == "am" || suffix == "a.m.") return (h % 12) * 60 + m;
    if (suffix == "pm" || suffix == "p.m.") return (h % 12 + 12) * 60 + m;
    return -1;
}

// Splits one CSV line, honouring "quoted, fields" and "" escapes (as written by Excel/Python).
inline std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> cells(1);
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cells.back() += '"'; ++i; }
            else if (c == '"') quoted = false;
            else cells.back() += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') cells.emplace_back();
        else cells.back() += c;
    }
    return cells;
}

// Levenshtein edit distance: the fewest single-letter inserts, deletes or replacements that turn
// `a` into `b` ("clok" -> "clock" = 1). Dynamic programming that keeps only two rows of the table:
// O(|a| * |b|) time, O(|b|) memory.
inline int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j) {
            int replace = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, replace});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// A destination may only contain letters, digits, spaces and a few punctuation marks (max 60 chars).
inline bool isValidQuery(const std::string& q) {
    if (q.size() > 60) return false;
    for (unsigned char c : q)
        if (!(std::isalnum(c) || c == ' ' || c == '.' || c == '-' || c == '\'' || c == '(' || c == ')' || c == '/' || c == '&'))
            return false;
    return true;
}

} // namespace unigo
