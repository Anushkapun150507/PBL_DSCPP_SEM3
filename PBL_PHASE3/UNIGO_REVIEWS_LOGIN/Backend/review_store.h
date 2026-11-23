// review_store.h -- Reviews & Feedback: the rules for a review, and the file that keeps them.
//
// A review = stars (1-5) + the matching review line ("Pretty good!") + the features the student
// found most useful + an optional improvement suggestion + an anonymous username.
// Reviews are saved in data/reviews.json, one review per line, so a moderator can open the file
// and delete a line to remove a review. Saving writes a temporary file first and then replaces
// the old one, so a crash in the middle of saving never leaves a half-written file.
#pragma once
#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
#endif
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "file_utils.h"
#include "json_reader.h"
#include "json_writer.h"
#include "text_utils.h"

namespace unigo {

// ---------- the fixed choices (the website asks the server for these, so they live in one place) ----------
struct FeatureOption { const char* key; const char* label; };
inline const std::vector<FeatureOption>& featureOptions() {
    static const std::vector<FeatureOption> f = {
        {"map", "University mapping"}, {"bus", "Bus route"}, {"lostfound", "Lost and found"}};
    return f;
}
// The automatic review line for each number of stars (index 1..5).
inline const char* reviewLine(int stars) {
    static const char* lines[] = {"", "Needs improvement", "Okay", "Good", "Pretty good!", "Excellent!"};
    return stars >= 1 && stars <= 5 ? lines[stars] : "";
}

struct Review {
    long long id = 0;
    std::string name;                     // anonymous username, "Anonymous" if none was given
    int stars = 0;
    std::string line;                     // always reviewLine(stars), set by the server
    std::vector<std::string> features;    // keys from featureOptions(), in that order
    std::string suggestion;               // empty = the student did not suggest anything
    std::string postedAt;                 // "2026-11-09 14:05" (server's local time)
};

// ---------- text checks (utf8Length and cleanFreeText are in text_utils.h) ----------
const int kNameMin = 2, kNameMax = 24, kSuggestionMin = 5, kSuggestionMax = 500;

// Cleans and checks the username. Empty -> "Anonymous". Returns an error message, or "" if fine.
inline std::string cleanName(const std::string& raw, std::string& out, bool strict) {
    std::string s;
    for (char ch : trim(raw)) {                                    // collapse runs of spaces
        if (ch == ' ' || ch == '\t') { if (!s.empty() && s.back() != ' ') s += ' '; }
        else s += ch;
    }
    if (s.empty()) { out = "Anonymous"; return ""; }
    size_t chars;
    if (!utf8Length(s, chars)) return "The username contains characters we can't read.";
    if ((int)chars < kNameMin || (int)chars > kNameMax)
        return "The username must be " + std::to_string(kNameMin) + " to " + std::to_string(kNameMax) + " characters long.";
    for (unsigned char c : s)
        if (c < 0x80 && !std::isalnum(c) && c != ' ' && c != '_' && c != '-' && c != '.')
            return "Use letters, numbers, spaces, dots, hyphens or underscores in the username.";
    if (strict) {                                                  // don't let a review look official
        std::string low = normalize(s);
        std::string compact;
        for (char c : low) if (c != ' ' && c != '.' && c != '-' && c != '_') compact += c;
        for (const char* word : {"unigo", "admin", "moderator", "official"})
            if (compact.find(word) != std::string::npos) return "Please choose a different username.";
    }
    out = s;
    return "";
}

// Cleans and checks the improvement suggestion (may be empty). Returns an error message or "".
inline std::string cleanSuggestion(const std::string& raw, std::string& out) {
    return cleanFreeText(raw, kSuggestionMin, kSuggestionMax, "suggestion", true, out);
}

// Builds a Review from posted (or stored) JSON. On a problem, `field` says which part is wrong.
// strict = true for new reviews from the website; false when reading the saved file.
inline bool reviewFromJson(const JsonValue& v, Review& r, std::string& field, std::string& message, bool strict) {
    field.clear(); message.clear();
    if (!v.isObject()) { field = "review"; message = "The review must be a JSON object."; return false; }

    const JsonValue* stars = v.get("stars");
    if (!stars || !stars->isNumber() || stars->asNumber() < 1 || stars->asNumber() > 5 ||
        stars->asNumber() != (double)(int)stars->asNumber()) {             // 1..5, whole numbers only
        field = "stars"; message = "Please choose from 1 to 5 stars."; return false;
    }
    r.stars = (int)stars->asNumber();
    r.line = reviewLine(r.stars);                                  // never trusted from the client

    r.features.clear();
    const JsonValue* feats = v.get("features");
    if (feats && !feats->isArray()) { field = "features"; message = "Features must be a list."; return false; }
    if (feats) {
        for (const JsonValue& f : feats->items()) {
            bool known = false;
            if (f.isString()) for (const FeatureOption& o : featureOptions()) if (f.asString() == o.key) known = true;
            if (!known) { field = "features"; message = "Unknown feature: choose from the listed features."; return false; }
        }
        for (const FeatureOption& o : featureOptions())            // keep the fixed order, drop repeats
            for (const JsonValue& f : feats->items())
                if (f.asString() == o.key) { r.features.push_back(o.key); break; }
    }
    if (strict && r.features.empty()) { field = "features"; message = "Please choose at least one feature you find useful."; return false; }

    const JsonValue* sug = v.get("suggestion");
    if (sug && !sug->isNull() && !sug->isString()) { field = "suggestion"; message = "The suggestion must be text."; return false; }
    std::string err = cleanSuggestion(sug && sug->isString() ? sug->asString() : "", r.suggestion);
    if (!err.empty()) { field = "suggestion"; message = err; return false; }

    const JsonValue* name = v.get("name");
    if (name && !name->isNull() && !name->isString()) { field = "name"; message = "The username must be text."; return false; }
    err = cleanName(name && name->isString() ? name->asString() : "", r.name, strict);
    if (!err.empty()) { field = "name"; message = err; return false; }

    if (!strict) {                                                 // fields only the saved file has
        const JsonValue* id = v.get("id");
        const JsonValue* at = v.get("postedAt");
        r.id = id && id->isNumber() ? (long long)id->asNumber() : 0;
        r.postedAt = at && at->isString() ? at->asString() : "";
    }
    return true;
}

inline void writeReview(JsonWriter& j, const Review& r) {
    j.beginObject().key("id").num(r.id).key("name").str(r.name).key("stars").num(r.stars).key("line").str(r.line)
        .key("features").beginArray();
    for (const std::string& f : r.features) j.str(f);
    j.endArray().key("suggestion").str(r.suggestion).key("postedAt").str(r.postedAt).endObject();
}

// ---------- the store ----------
class ReviewStore {
public:
    enum class AddResult { Added, Duplicate, SaveFailed };

    explicit ReviewStore(std::string path) : path_(std::move(path)) {}

    // Reloads the file if someone edited it (e.g. a moderator removed a review). A missing file
    // simply means "no reviews yet". A broken file keeps the reviews already in memory.
    void refresh(bool force = false) {
        Stamp s = stampOf(path_);
        if (!force && s == stamp_) return;
        stamp_ = s;
        if (s.missing) { reviews_.clear(); status_ = "No reviews yet."; return; }
        try {
            JsonValue root = JsonParser::parseFile(path_);
            const JsonValue* list = root.isObject() ? root.get("reviews") : nullptr;
            if (!list || !list->isArray()) throw std::runtime_error("expected an object with a \"reviews\" list");
            std::vector<Review> fresh;
            skipped_ = 0;
            for (const JsonValue& v : list->items()) {
                Review r;
                std::string field, message;
                if (reviewFromJson(v, r, field, message, false)) fresh.push_back(r);
                else ++skipped_;
            }
            reviews_ = std::move(fresh);
            status_ = "Loaded " + std::to_string(reviews_.size()) + " reviews";
            if (skipped_) status_ += " (skipped " + std::to_string(skipped_) + " broken entries)";
            std::cout << status_ << " from " << path_ << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error reading " << path_ << ": " << e.what() << " -- keeping the reviews already loaded" << std::endl;
        }
    }

    const std::vector<Review>& reviews() { refresh(); return reviews_; }   // oldest first
    const std::string& status() const { return status_; }
    int skipped() const { return skipped_; }

    // Adds a checked review: gives it an id and a date, then saves the whole list.
    AddResult add(Review r, Review& saved) {
        refresh();
        if (!reviews_.empty()) {                                    // the same review twice in a row = a double click
            const Review& last = reviews_.back();
            if (last.name == r.name && last.stars == r.stars && last.features == r.features && last.suggestion == r.suggestion)
                return AddResult::Duplicate;
        }
        long long maxId = 0;
        for (const Review& x : reviews_) if (x.id > maxId) maxId = x.id;
        r.id = maxId + 1;
        r.postedAt = nowText();
        std::vector<Review> next = reviews_;
        next.push_back(r);
        if (!save(next)) return AddResult::SaveFailed;              // memory stays as it was
        reviews_ = std::move(next);
        stamp_ = stampOf(path_);
        saved = r;
        return AddResult::Added;
    }

private:
    struct Stamp {
        long long mtime = 0, size = -1;
        bool missing = false;
        bool operator==(const Stamp& o) const { return mtime == o.mtime && size == o.size && missing == o.missing; }
    };
    static Stamp stampOf(const std::string& path) {
        Stamp s;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) { s.missing = true; return s; }
        s.mtime = (long long)st.st_mtime;
        s.size = (long long)st.st_size;
        return s;
    }
    static std::string nowText() {
        std::time_t t = std::time(nullptr);
        char b[32];
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&t));
        return b;
    }

    // One review per line, inside {"note": ..., "reviews": [ ... ]}, saved atomically (file_utils.h).
    bool save(const std::vector<Review>& list) {
        std::ostringstream f;
        f << "{\n\"note\": \"UNIGO reviews. One review per line. To remove a review, delete its whole line "
             "(and the comma before it if it was the last one).\",\n\"reviews\": [\n";
        for (size_t i = 0; i < list.size(); ++i) {
            JsonWriter j;
            writeReview(j, list[i]);
            f << j.text() << (i + 1 < list.size() ? ",\n" : "\n");
        }
        f << "]\n}\n";
        return writeFileAtomic(path_, f.str());
    }

    std::string path_;
    std::vector<Review> reviews_;
    std::string status_ = "No reviews yet.";
    int skipped_ = 0;
    Stamp stamp_ = Stamp{0, -2, false};                              // "never looked yet"
};

} // namespace unigo
