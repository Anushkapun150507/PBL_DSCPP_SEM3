// auth_store.h -- UNIGO accounts: sign up (name + roll number + password) and log in (roll number + password)
// on the start page. The login is used by the whole site; Lost & Found relies on it for posts and claims.
//
// What it does:
//   * checks the roll-number FORMAT (rule kept in data/lostfound/config.json, e.g. exactly 7 digits).
//     It cannot prove the person really is that student -- that would need the university's email or ERP.
//   * stores only a random salt + a PBKDF2-HMAC-SHA256 hash of each password (crypto.h), never the password
//   * gives each login a random session token, sent to the browser as an HttpOnly cookie. The sessions file
//     keeps only a SHA-256 fingerprint of each token, so a copy of the file can't be used to log in,
//     and logins survive a server restart.
//   * blocks a roll number for 15 minutes after 5 wrong passwords
//   * creates the collection-desk admin from the config file (demo setup: change that password for real use)
#pragma once
#include <ctime>
#include <deque>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "crypto.h"
#include "file_utils.h"
#include "json_reader.h"
#include "json_writer.h"
#include "text_utils.h"

namespace unigo {

struct LfUser {
    std::string id;           // roll number (students) or the admin's username
    std::string name;         // the name shown in "Hi, ___" (never shown to other students)
    std::string role;         // "student" or "admin"
    std::string salt, hash;   // hex
    int iterations = 0;
    std::string createdAt;
};

// Settings read from data/lostfound/config.json (all optional; these are the defaults).
struct LfConfig {
    int rollDigits = 7;                    // a roll number is exactly this many digits
    int postDays = 30;                     // posts are archived after this many days
    std::string adminUser = "desk";        // the collection-desk login
    std::string adminPassword;             // empty = no admin account
    std::string collectionPoint = "CSIT Block, First Floor, near LT 1";
    std::string collectionPlaceId = "csit-block";   // a place id on the campus map ("Show on map")
};

class AuthStore {
public:
    static const int kIterations = 60000;           // PBKDF2 rounds: about 0.1 s per login on a laptop
    static const int kMaxFails = 5, kLockSeconds = 15 * 60;
    static const long long kSessionSeconds = 7 * 24 * 3600;

    AuthStore(std::string usersPath, std::string configPath, std::string sessionsPath = "")
        : usersPath_(std::move(usersPath)), configPath_(std::move(configPath)), sessionsPath_(std::move(sessionsPath)) {}

    // Reads the config and the users file, and makes sure the admin account matches the config.
    void load() {
        readConfig();
        users_.clear();
        std::string text;
        if (readWholeFile(usersPath_, text)) {
            try {
                JsonValue root = JsonParser::parse(text);
                const JsonValue* list = root.isObject() ? root.get("users") : nullptr;
                if (list && list->isArray())
                    for (const JsonValue& u : list->items()) {
                        LfUser x;
                        x.id = str(u, "id"); x.name = str(u, "name"); x.role = str(u, "role"); x.salt = str(u, "salt"); x.hash = str(u, "hash");
                        x.createdAt = str(u, "createdAt");
                        const JsonValue* it = u.get("iterations");
                        x.iterations = it && it->isNumber() ? (int)it->asNumber() : 0;
                        if (!x.id.empty() && !x.salt.empty() && !x.hash.empty() && x.iterations > 0) users_[x.id] = x;
                    }
            } catch (const std::exception& e) {
                std::cerr << "Error reading " << usersPath_ << ": " << e.what() << std::endl;
            }
        }
        if (!config_.adminPassword.empty()) {
            auto it = users_.find(config_.adminUser);
            if (it == users_.end() || it->second.role != "admin" || !verify(it->second, config_.adminPassword)) {
                users_[config_.adminUser] = makeUser(config_.adminUser, config_.adminPassword, "admin");
                users_[config_.adminUser].name = "Collection desk";
                save();
                std::cout << "Lost & Found: admin account '" << config_.adminUser << "' set from config.json" << std::endl;
            }
        }
        loadSessions();
        std::cout << "Accounts: " << users_.size() << " (" << sessions_.size() << " logged in)" << std::endl;
    }

    const LfConfig& config() const { return config_; }

    // ---------- rules ----------
    // Name: letters from any language, spaces, dots, apostrophes and hyphens; 2-30 characters. Cleaned into `out`.
    static std::string nameProblem(const std::string& raw, std::string& out) {
        std::string s;
        for (char ch : trim(raw)) {
            if (ch == ' ' || ch == '\t') { if (!s.empty() && s.back() != ' ') s += ' '; }
            else s += ch;
        }
        size_t chars;
        if (s.empty()) return "Please enter your name.";
        if (!utf8Length(s, chars)) return "Your name contains characters we can't read.";
        if (chars < 2) return "Your name needs at least 2 characters.";
        if (chars > 30) return "Your name can have at most 30 characters.";
        for (unsigned char c : s)
            if (c < 0x80 && !std::isalpha(c) && c != ' ' && c != '.' && c != '\'' && c != '-')
                return "Please use letters only. Spaces, dots, apostrophes and hyphens are fine.";
        out = s;
        return "";
    }
    std::string rollProblem(const std::string& roll) const {
        if (roll.empty()) return "Please enter your roll number.";
        if ((int)roll.size() != config_.rollDigits) return "A roll number has exactly " + std::to_string(config_.rollDigits) + " digits.";
        for (char c : roll) if (c < '0' || c > '9') return "A roll number has digits only.";
        return "";
    }
    static std::string passwordProblem(const std::string& pw, const std::string& roll) {
        if (pw.size() < 8) return "Use at least 8 characters for your password.";
        if (pw.size() > 64) return "Use at most 64 characters for your password.";
        bool letter = false, digit = false;
        for (unsigned char c : pw) {
            if (c < 0x20 || c == 0x7F) return "The password contains characters that aren't allowed.";
            if (std::isalpha(c)) letter = true;
            if (std::isdigit(c)) digit = true;
        }
        if (!letter || !digit) return "Use at least one letter and one number in your password.";
        if (normalize(pw).find(roll) != std::string::npos && !roll.empty()) return "Don't use your roll number in your password.";
        return "";
    }

    // ---------- sign up / log in / sessions ----------
    enum class Result { Ok, Exists, Wrong, Locked, SaveFailed };

    Result signup(const std::string& roll, const std::string& password, const std::string& name = "") {
        if (users_.count(roll)) return Result::Exists;
        users_[roll] = makeUser(roll, password, "student");
        users_[roll].name = name;
        if (!save()) { users_.erase(roll); return Result::SaveFailed; }
        return Result::Ok;
    }

    // `waitMinutes` says how long a locked account must wait.
    Result login(const std::string& id, const std::string& password, long long now, int& waitMinutes) {
        std::deque<long long>& fails = fails_[id];
        while (!fails.empty() && fails.front() <= now - kLockSeconds) fails.pop_front();   // forget old failures
        if ((int)fails.size() >= kMaxFails) {
            waitMinutes = (int)((fails.front() + kLockSeconds - now + 59) / 60);
            return Result::Locked;
        }
        auto it = users_.find(id);
        // Even for an unknown id we spend the same time hashing, so timing doesn't reveal which ids exist.
        bool ok = it != users_.end() ? verify(it->second, password)
                                     : (pbkdf2Sha256(password, "unknown-user", kIterations, 32), false);
        if (!ok) { fails.push_back(now); return Result::Wrong; }
        fails.clear();
        return Result::Ok;
    }

    std::string createSession(const std::string& id, long long now) {
        for (auto it = sessions_.begin(); it != sessions_.end();)            // tidy up expired sessions
            it = it->second.expires <= now ? sessions_.erase(it) : std::next(it);
        std::string token = toHex(secureRandom(32));                          // 256 random bits, sent to the browser
        sessions_[fingerprint(token)] = Session{id, now + kSessionSeconds};   // only its fingerprint is kept
        saveSessions();
        return token;
    }
    // The logged-in user for a cookie token, or nullptr.
    const LfUser* userForToken(const std::string& token, long long now) const {
        if (!isHex(token, 64)) return nullptr;
        auto s = sessions_.find(fingerprint(token));
        if (s == sessions_.end() || s->second.expires <= now) return nullptr;
        auto u = users_.find(s->second.userId);
        return u == users_.end() ? nullptr : &u->second;
    }
    void logout(const std::string& token) {
        if (isHex(token, 64) && sessions_.erase(fingerprint(token))) saveSessions();
    }
    // Saves the name of an existing account (accounts made before names were asked have none).
    bool setName(const std::string& id, const std::string& name) {
        auto u = users_.find(id);
        if (u == users_.end()) return false;
        std::string old = u->second.name;
        u->second.name = name;
        if (!save()) { u->second.name = old; return false; }
        return true;
    }
    const LfUser* findUser(const std::string& id) const { auto u = users_.find(id); return u == users_.end() ? nullptr : &u->second; }
    size_t userCount() const { return users_.size(); }

private:
    struct Session { std::string userId; long long expires; };

    static std::string fingerprint(const std::string& token) { return toHex(Sha256::hash(token)); }

    void loadSessions() {
        sessions_.clear();
        std::string text;
        if (sessionsPath_.empty() || !readWholeFile(sessionsPath_, text)) return;
        try {
            JsonValue root = JsonParser::parse(text);
            const JsonValue* list = root.isObject() ? root.get("sessions") : nullptr;
            long long now = (long long)std::time(nullptr);
            if (list && list->isArray())
                for (const JsonValue& s : list->items()) {
                    const JsonValue* e = s.get("expires");
                    long long expires = e && e->isNumber() ? (long long)e->asNumber() : 0;
                    std::string key = str(s, "key"), user = str(s, "user");
                    if (isHex(key, 64) && users_.count(user) && expires > now) sessions_[key] = Session{user, expires};
                }
        } catch (const std::exception& e) {
            std::cerr << "Error reading " << sessionsPath_ << ": " << e.what() << " -- everyone will need to log in again" << std::endl;
        }
    }
    void saveSessions() const {
        if (sessionsPath_.empty()) return;
        std::ostringstream f;
        f << "{\n\"note\": \"Who is logged in. Only a SHA-256 fingerprint of each login token is kept, so this file can't be used to log in. "
             "Delete the file to log everyone out.\",\n\"sessions\": [\n";
        size_t i = 0;
        for (const auto& kv : sessions_) {
            JsonWriter j;
            j.beginObject().key("key").str(kv.first).key("user").str(kv.second.userId).key("expires").num(kv.second.expires).endObject();
            f << j.text() << (++i < sessions_.size() ? ",\n" : "\n");
        }
        f << "]\n}\n";
        writeFileAtomic(sessionsPath_, f.str());
    }

    static std::string str(const JsonValue& v, const char* key) {
        const JsonValue* x = v.get(key);
        return x && x->isString() ? x->asString() : "";
    }
    static std::string nowText() {
        std::time_t t = std::time(nullptr);
        char b[32];
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&t));
        return b;
    }
    LfUser makeUser(const std::string& id, const std::string& password, const std::string& role) const {
        LfUser u;
        u.id = id; u.role = role; u.iterations = kIterations; u.createdAt = nowText();
        std::string salt = secureRandom(16);                                  // a new salt for every user
        u.salt = toHex(salt);
        u.hash = toHex(pbkdf2Sha256(password, salt, u.iterations, 32));
        return u;
    }
    static bool verify(const LfUser& u, const std::string& password) {
        std::string salt, hash;
        if (!fromHex(u.salt, salt) || !fromHex(u.hash, hash)) return false;
        return constantTimeEquals(pbkdf2Sha256(password, salt, u.iterations, hash.size()), hash);
    }

    void readConfig() {
        std::string text;
        if (!readWholeFile(configPath_, text)) {
            std::cout << "Lost & Found: no " << configPath_ << " -- using default settings, no admin account" << std::endl;
            return;
        }
        try {
            JsonValue c = JsonParser::parse(text);
            const JsonValue* d = c.get("rollNumberDigits");
            if (d && d->isNumber() && d->asNumber() >= 3 && d->asNumber() <= 20) config_.rollDigits = (int)d->asNumber();
            const JsonValue* p = c.get("postDays");
            if (p && p->isNumber() && p->asNumber() >= 1 && p->asNumber() <= 365) config_.postDays = (int)p->asNumber();
            if (const JsonValue* a = c.get("demoAdmin")) {
                std::string user = str(*a, "username"), pw = str(*a, "password");
                bool digitsOnly = !user.empty() && user.find_first_not_of("0123456789") == std::string::npos;
                if (!user.empty() && !digitsOnly) config_.adminUser = user;      // never a roll-number-like name
                config_.adminPassword = pw;
            }
            if (const JsonValue* cp = c.get("collectionPoint")) {
                if (!str(*cp, "name").empty()) config_.collectionPoint = str(*cp, "name");
                config_.collectionPlaceId = str(*cp, "placeId");
            }
        } catch (const std::exception& e) {
            std::cerr << "Error reading " << configPath_ << ": " << e.what() << " -- using defaults" << std::endl;
        }
    }

    bool save() const {
        std::ostringstream f;
        f << "{\n\"note\": \"Lost & Found accounts. Passwords are NOT stored: only a random salt and a PBKDF2-SHA256 hash. "
             "Delete a line to remove an account.\",\n\"users\": [\n";
        size_t i = 0;
        for (const auto& kv : users_) {
            const LfUser& u = kv.second;
            JsonWriter j;
            j.beginObject().key("id").str(u.id).key("name").str(u.name).key("role").str(u.role).key("salt").str(u.salt).key("hash").str(u.hash)
                .key("iterations").num(u.iterations).key("createdAt").str(u.createdAt).endObject();
            f << j.text() << (++i < users_.size() ? ",\n" : "\n");
        }
        f << "]\n}\n";
        return writeFileAtomic(usersPath_, f.str());
    }

    std::string usersPath_, configPath_, sessionsPath_;
    LfConfig config_;
    std::map<std::string, LfUser> users_;                         // sorted by roll number in the file
    std::map<std::string, Session> sessions_;                     // SHA-256(token) -> who + until when
    std::unordered_map<std::string, std::deque<long long>> fails_;   // id -> times of recent wrong passwords
};

} // namespace unigo
