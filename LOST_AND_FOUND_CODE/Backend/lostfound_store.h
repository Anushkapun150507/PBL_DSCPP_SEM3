// lostfound_store.h -- Lost & Found posts, photos and claims.
//
//   * LfPost   -- one "I lost ..." or "I found ..." post (category, description, where, when, photos)
//   * LfClaim  -- "This is mine": a student's claim on a found item, decided by the collection desk
//   * LostFoundStore -- keeps them in data/lostfound/posts.json and claims.json (one record per line,
//     saved atomically) and the photos in data/lostfound/photos/<random>.jpg
//   * possibleMatches() -- scores found items for a lost post (category, shared words via SearchIndex,
//     place on the campus graph, date). The computer only SUGGESTS; the desk decides ownership.
//
// Privacy: a post's owner (roll number) and the finder's private note are never sent to other students.
#pragma once
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "campus_map.h"
#include "crypto.h"
#include "file_utils.h"
#include "json_reader.h"
#include "json_writer.h"
#include "search_index.h"
#include "text_utils.h"

namespace unigo {

// ---------- fixed choices ----------
struct LfCategory { const char* key; const char* label; };
inline const std::vector<LfCategory>& lfCategories() {
    static const std::vector<LfCategory> c = {
        {"phone", "Phone"}, {"electronics", "Other electronics"}, {"bottle", "Bottle"}, {"bag", "Bag / pouch"},
        {"wallet", "Wallet"}, {"idcard", "ID card"}, {"keys", "Keys"}, {"books", "Books / notes"},
        {"clothing", "Clothing"}, {"jewellery", "Jewellery / watch"}, {"other", "Other"}};
    return c;
}
inline const char* lfCategoryLabel(const std::string& key) {
    for (const LfCategory& c : lfCategories()) if (key == c.key) return c.label;
    return "";
}
// Items that hold personal data or money: the page warns the finder to hand them in.
inline bool lfSensitive(const std::string& key) { return key == "phone" || key == "wallet" || key == "idcard"; }
inline const std::vector<std::string>& lfDayParts() {
    static const std::vector<std::string> d = {"morning", "afternoon", "evening"};
    return d;
}

// Limits (characters are UTF-8 characters).
const size_t kLostDescMin = 10, kLostDescMax = 500, kFoundDescMin = 3, kFoundDescMax = 300;
const size_t kPlaceTextMin = 3, kPlaceTextMax = 80, kNoteMax = 200, kClaimMin = 10, kClaimMax = 500;
const size_t kPhotoMaxBytes = 600 * 1024, kMaxPhotos = 2;
const int kClaimsPerDay = 5, kReportsToHide = 3;

struct LfPost {
    long long id = 0;
    std::string type;                     // "lost" or "found"
    std::string category, description;
    std::vector<std::string> photos;      // photo ids (32 hex characters)
    std::string placeId, placeText;       // a campus place id, or the student's own words
    bool placeUnknown = false;            // "I don't remember" (lost posts only)
    std::string date, dayPart;            // "2026-11-09", "morning|afternoon|evening"
    bool timeUnknown = false;             // lost posts only
    std::string privateNote;              // found posts: for the collection desk only
    std::string owner;                    // roll number of the poster -- never shown to other students
    std::string status = "open";          // open | pending | approved | returned | closed
    bool hidden = false;                  // hidden by the desk (or by reports)
    std::vector<std::string> reportedBy;  // roll numbers that reported this post
    long long createdAt = 0, closedAt = 0;
    std::string closedReason, returnedTo;
};

struct LfClaim {
    long long id = 0, postId = 0;
    std::string user, description;
    std::string status = "pending";       // pending | approved | atdesk | rejected | returned
    long long createdAt = 0, decidedAt = 0;
    long long lostPostId = 0;             // the claimant's own lost post this claim came from (0 = not known)
};

struct LfMatch { size_t post; int score; std::vector<std::string> reasons; std::string warning; };
const long long kMatchGoodDays = 2, kMatchMaxDays = 7;   // found 0-2 days after = likely; 3-7 = warn; later = no match

// ---------- dates ----------
inline std::string lfDateOf(long long t) {
    std::time_t tt = (std::time_t)t;
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", std::localtime(&tt));
    return b;
}
// "2026-11-09" -> days since 1970-01-01, or -1 if it isn't a real date. (Howard Hinnant's days_from_civil.)
inline long long lfDayNumber(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return -1;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) if (s[i] < '0' || s[i] > '9') return -1;
    int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (y < 2000 || m < 1 || m > 12 || d < 1 || d > days[m - 1] + (m == 2 && leap ? 1 : 0)) return -1;
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// ---------- checking a new post ----------
struct LfPhotoUpload { std::string bytes; };

// Decodes "data:image/jpeg;base64,..." and checks it really is a JPEG of an allowed size.
inline std::string lfDecodePhoto(const JsonValue& v, std::string& bytes) {
    if (!v.isString()) return "Each photo must be an image.";
    const std::string prefix = "data:image/jpeg;base64,";
    const std::string& s = v.asString();
    if (s.compare(0, prefix.size(), prefix) != 0) return "Photos must be JPEG images.";
    if (s.size() - prefix.size() > (kPhotoMaxBytes * 4) / 3 + 8) return "Each photo must be under 600 KB.";
    if (!base64Decode(s.substr(prefix.size()), bytes)) return "A photo could not be read.";
    if (bytes.size() > kPhotoMaxBytes) return "Each photo must be under 600 KB.";
    // A JPEG file starts with FF D8 FF and ends with FF D9 -- check the bytes, not just the name.
    if (bytes.size() < 4 || (unsigned char)bytes[0] != 0xFF || (unsigned char)bytes[1] != 0xD8 || (unsigned char)bytes[2] != 0xFF ||
        (unsigned char)bytes[bytes.size() - 2] != 0xFF || (unsigned char)bytes[bytes.size() - 1] != 0xD9)
        return "A photo is not a valid JPEG image.";
    return "";
}

// Builds a post from the posted JSON. `photos` receives the decoded images. On a problem, `field` names it.
inline bool lfPostFromJson(const JsonValue& v, const CampusMap& map, long long now, LfPost& p,
                           std::vector<LfPhotoUpload>& photos, std::string& field, std::string& message) {
    auto fail = [&](const char* f, const std::string& m) { field = f; message = m; return false; };
    auto text = [&](const char* key) { const JsonValue* x = v.get(key); return x && x->isString() ? x->asString() : std::string(); };
    auto flag = [&](const char* key) { const JsonValue* x = v.get(key); return x && x->type() == JsonValue::Bool && x->asBool(); };
    if (!v.isObject()) return fail("post", "The post must be a JSON object.");

    p.type = text("type");
    if (p.type != "lost" && p.type != "found") return fail("type", "Choose whether you lost or found something.");
    bool lost = p.type == "lost";
    p.category = text("category");
    if (!*lfCategoryLabel(p.category)) return fail("category", "Please choose a category.");

    std::string err = lost ? cleanFreeText(text("description"), kLostDescMin, kLostDescMax, "description", false, p.description)
                           : cleanFreeText(text("description"), kFoundDescMin, kFoundDescMax, "description", false, p.description);
    if (!err.empty()) return fail("description", err);

    photos.clear();
    const JsonValue* ph = v.get("photos");
    if (ph && !ph->isArray()) return fail("photos", "Photos must be a list.");
    size_t count = ph ? ph->items().size() : 0;
    if (count > kMaxPhotos) return fail("photos", "Add at most 2 photos.");
    if (!lost && count == 0) return fail("photos", "Please add at least one photo of the item you found.");
    for (size_t i = 0; i < count; ++i) {
        LfPhotoUpload up;
        err = lfDecodePhoto(ph->items()[i], up.bytes);
        if (!err.empty()) return fail("photos", err);
        photos.push_back(up);
    }

    // Where: a campus place, the student's own words, or (lost only) "I don't remember".
    p.placeUnknown = lost && flag("placeUnknown");
    p.placeId = p.placeUnknown ? "" : text("placeId");
    p.placeText.clear();
    if (!p.placeUnknown) {
        if (!p.placeId.empty()) {
            if (!map.placeById(p.placeId)) return fail("place", "That place is not on the campus map. Type the place in your own words instead.");
        } else {
            err = cleanFreeText(text("placeText"), kPlaceTextMin, kPlaceTextMax, "place", false, p.placeText);
            if (!err.empty()) return fail("place", lost ? "Choose a place, type where it was, or tick \"I don't remember\"." : "Please say where you found it.");
        }
    }

    // When: a date (not in the future, not older than a year) and a part of the day, or (lost only) "I don't remember".
    p.timeUnknown = lost && flag("timeUnknown");
    if (!p.timeUnknown) {
        p.date = text("date");
        long long day = lfDayNumber(p.date), today = lfDayNumber(lfDateOf(now));
        if (day < 0) return fail("date", "Please choose a valid date.");
        if (day > today) return fail("date", "The date can't be in the future.");
        if (day < today - 365) return fail("date", "The date must be within the last year.");
        p.dayPart = text("dayPart");
        if (std::find(lfDayParts().begin(), lfDayParts().end(), p.dayPart) == lfDayParts().end())
            return fail("dayPart", "Please choose morning, afternoon or evening.");
    } else { p.date.clear(); p.dayPart.clear(); }

    p.privateNote.clear();
    if (!lost) {
        err = cleanFreeText(text("privateNote"), 0, kNoteMax, "note", true, p.privateNote);
        if (!err.empty()) return fail("privateNote", err);
    }
    return true;
}

// ---------- the store ----------
class LostFoundStore {
public:
    explicit LostFoundStore(std::string dir, int postDays = 30)
        : dir_(std::move(dir)), postDays_(postDays) {}

    void setPostDays(int d) { postDays_ = d; }
    int postDays() const { return postDays_; }

    bool load() {
        if (!makeDir(dir_) || !makeDir(dir_ + "/photos")) { std::cerr << "Cannot create " << dir_ << std::endl; return false; }
        posts_.clear(); claims_.clear();
        std::string text;
        try {
            if (readWholeFile(dir_ + "/posts.json", text)) {
                const JsonValue root = JsonParser::parse(text);
                const JsonValue* list = root.get("posts");
                if (list && list->isArray()) for (const JsonValue& v : list->items()) posts_.push_back(postFromFile(v));
            }
            if (readWholeFile(dir_ + "/claims.json", text)) {
                const JsonValue root = JsonParser::parse(text);
                const JsonValue* list = root.get("claims");
                if (list && list->isArray()) for (const JsonValue& v : list->items()) claims_.push_back(claimFromFile(v));
            }
        } catch (const std::exception& e) {
            std::cerr << "Error reading Lost & Found data: " << e.what() << std::endl;
            return false;
        }
        std::cout << "Lost & Found: " << posts_.size() << " posts, " << claims_.size() << " claims" << std::endl;
        return true;
    }

    const std::vector<LfPost>& posts() const { return posts_; }
    const std::vector<LfClaim>& claims() const { return claims_; }
    const LfPost* post(long long id) const { for (const LfPost& p : posts_) if (p.id == id) return &p; return nullptr; }

    // open / pending / approved posts become "expired" after postDays (archived, not deleted).
    std::string effectiveStatus(const LfPost& p, long long now) const {
        bool active = p.status == "open" || p.status == "pending" || p.status == "approved";
        if (active && now - p.createdAt > (long long)postDays_ * 86400) return "expired";
        return p.status;
    }
    bool isActive(const LfPost& p, long long now) const {
        std::string s = effectiveStatus(p, now);
        return !p.hidden && (s == "open" || s == "pending" || s == "approved");
    }

    // Saves the photos, then the post. Returns the new post's id, or 0 on failure.
    long long create(LfPost p, const std::vector<LfPhotoUpload>& photos, const std::string& owner, long long now) {
        std::vector<std::string> written;
        for (const LfPhotoUpload& up : photos) {
            std::string id = toHex(secureRandom(16));                   // unguessable file name
            if (!writeFileAtomic(photoPath(id), up.bytes)) { for (auto& w : written) removeFile(photoPath(w)); return 0; }
            written.push_back(id);
        }
        p.photos = written;
        p.id = nextId(posts_);
        p.owner = owner;
        p.status = "open";
        p.createdAt = now;
        posts_.push_back(p);
        if (!savePosts()) { posts_.pop_back(); for (auto& w : written) removeFile(photoPath(w)); return 0; }
        return p.id;
    }

    // The poster closes their own post. Returns an error message or "".
    std::string close(long long id, const std::string& user, const std::string& reason, long long now) {
        LfPost* p = find(id);
        if (!p || p->owner != user) return "You can only close your own posts.";
        if (!isActive(*p, now)) return "This post is no longer open.";
        if (p->status == "approved") return "You approved a claim for this item. Hand it in at the desk; the owner collects it there.";
        p->status = "closed"; p->closedAt = now; p->closedReason = reason;
        for (LfClaim& c : claims_) if (c.postId == id && c.status == "pending") { c.status = "rejected"; c.decidedAt = now; }
        return savePosts() && saveClaims() ? "" : "Could not save. Please try again.";
    }

    // "This is mine". Returns an error message, or "" and sets claimId.
    std::string addClaim(long long postId, const std::string& user, const std::string& description, long long now, long long& claimId,
                         long long lostPostId = 0) {
        LfPost* p = find(postId);
        if (!p || p->type != "found" || p->hidden) return "That item could not be found.";
        if (p->owner == user) return "You posted this item, so you can't claim it.";
        std::string s = effectiveStatus(*p, now);
        if (s != "open" && s != "pending") return "This item is no longer available to claim.";
        int today = 0;
        for (const LfClaim& c : claims_) {
            if (c.user != user) continue;
            if (c.postId == postId && (c.status == "pending" || c.status == "approved" || c.status == "atdesk")) return "You have already claimed this item.";
            if (now - c.createdAt < 86400) ++today;
        }
        if (today >= kClaimsPerDay) return "You can send at most " + std::to_string(kClaimsPerDay) + " claims a day.";
        LfClaim c;
        c.id = nextId(claims_); c.postId = postId; c.user = user; c.description = description; c.createdAt = now;
        const LfPost* lp = post(lostPostId);                   // remember it only if it really is this student's lost post
        c.lostPostId = lp && lp->type == "lost" && lp->owner == user ? lostPostId : 0;
        claims_.push_back(c);
        std::string oldStatus = p->status;
        p->status = "pending";
        if (!saveClaims() || !savePosts()) { claims_.pop_back(); p->status = oldStatus; return "Could not save. Please try again."; }
        claimId = c.id;
        return "";
    }

    // A claim's life:  pending --(finder: approve)--> approved --(finder: atdesk)--> atdesk --(owner/desk: returned)--> returned
    // The finder keeps the item until they approve a claim, and only then hands it in at the CSIT desk, so the
    // desk only ever holds items that already have an approved owner (and gives them only to that roll number).
    // Who may do what:
    //   * the finder (who posted the found item): "approve" / "reject" (compare the claim with the item they hold),
    //     then "atdesk" when they have handed it in;
    //   * the claimant: only "returned" ("I got it back"), once the item is at the desk;
    //   * the collection desk: everything.
    // Returns "" if allowed, else the reason.
    std::string mayDecide(long long claimId, const std::string& user, bool admin, const std::string& decision) const {
        const LfClaim* c = nullptr;
        for (const LfClaim& x : claims_) if (x.id == claimId) c = &x;
        if (!c) return "Claim not found.";
        if (admin) return "";
        const LfPost* p = post(c->postId);
        if (p && p->owner == user && (decision == "approve" || decision == "reject" || decision == "atdesk")) return "";
        if (c->user == user && decision == "returned") return c->status == "atdesk" ? "" : "Wait until the finder has handed the item in at the desk.";
        return "Only the person who found this item (or the collection desk) can decide this claim.";
    }

    // Decides a claim: "approve", "reject", "atdesk" (finder handed it in) or "returned" (owner has it back).
    std::string decide(long long claimId, const std::string& decision, long long now) {
        LfClaim* c = nullptr;
        for (LfClaim& x : claims_) if (x.id == claimId) c = &x;
        if (!c) return "Claim not found.";
        LfPost* p = find(c->postId);
        if (!p) return "The item for this claim no longer exists.";
        if (effectiveStatus(*p, now) == "expired") return "This post has expired.";
        if (decision == "approve") {
            if (c->status != "pending") return "Only a pending claim can be approved.";
            for (const LfClaim& x : claims_)
                if (x.postId == p->id && (x.status == "approved" || x.status == "atdesk")) return "Another claim for this item is already approved.";
            c->status = "approved"; p->status = "approved";
        } else if (decision == "atdesk") {
            if (c->status != "approved") return "Approve the claim first, then hand the item in at the desk.";
            c->status = "atdesk";
        } else if (decision == "reject") {
            if (c->status != "pending" && c->status != "approved" && c->status != "atdesk") return "This claim is already decided.";
            c->status = "rejected";
            bool others = false;
            for (const LfClaim& x : claims_) if (x.postId == p->id && x.id != c->id && x.status == "pending") others = true;
            if (p->status == "pending" || p->status == "approved") p->status = others ? "pending" : "open";
        } else if (decision == "returned") {
            if (c->status != "approved" && c->status != "atdesk") return "Approve the claim first, then mark it returned when the owner collects it.";
            c->status = "returned";
            p->status = "returned"; p->returnedTo = c->user; p->closedAt = now;
            for (LfClaim& x : claims_) if (x.postId == p->id && x.status == "pending") { x.status = "rejected"; x.decidedAt = now; }
            // The owner's lost post that this claim came from (via "possible matches") is done too. No guessing:
            // a claim made without one leaves the owner's lost posts alone.
            LfPost* l = find(c->lostPostId);
            if (l && l->type == "lost" && l->owner == c->user && (l->status == "open" || l->status == "pending")) {
                l->status = "returned"; l->closedAt = now; l->closedReason = "got-it-back";
            }
        } else return "Unknown decision.";
        c->decidedAt = now;
        return saveClaims() && savePosts() ? "" : "Could not save. Please try again.";
    }

    // A student reports a post; after kReportsToHide different reports it is hidden until the desk checks it.
    std::string report(long long postId, const std::string& user) {
        LfPost* p = find(postId);
        if (!p) return "Post not found.";
        if (p->owner == user) return "You can't report your own post.";
        if (std::find(p->reportedBy.begin(), p->reportedBy.end(), user) != p->reportedBy.end()) return "You have already reported this post.";
        p->reportedBy.push_back(user);
        if ((int)p->reportedBy.size() >= kReportsToHide) p->hidden = true;
        return savePosts() ? "" : "Could not save. Please try again.";
    }
    std::string setHidden(long long postId, bool hidden) {
        LfPost* p = find(postId);
        if (!p) return "Post not found.";
        p->hidden = hidden;
        if (!hidden) p->reportedBy.clear();            // the desk checked it
        return savePosts() ? "" : "Could not save. Please try again.";
    }

    // Which post a photo belongs to (photos are served only for posts the viewer may see).
    const LfPost* postWithPhoto(const std::string& photoId) const {
        for (const LfPost& p : posts_) for (const std::string& ph : p.photos) if (ph == photoId) return &p;
        return nullptr;
    }
    std::string photoPath(const std::string& id) const { return dir_ + "/photos/" + id + ".jpg"; }

    // ---------- possible matches for a lost post ----------
    std::vector<LfMatch> possibleMatches(const LfPost& lost, const CampusMap& map, long long now) const {
        // Candidates: every active found item that isn't already promised to someone. Items of another category
        // are kept only if they share a word with the lost post (the finder may have chosen "Other" for a ring).
        std::vector<size_t> cand;
        for (size_t i = 0; i < posts_.size(); ++i) {
            const LfPost& f = posts_[i];
            if (f.type == "found" && f.owner != lost.owner && isActive(f, now) && f.status != "approved")
                cand.push_back(i);
        }
        // Shared words, typo-tolerant: index each candidate's words in a SearchIndex.
        SearchIndex ix;
        for (size_t k = 0; k < cand.size(); ++k) ix.add((int)k, wordsOf(posts_[cand[k]].description));
        std::map<int, std::vector<std::string>> shared;
        for (const std::string& w : wordsOf(lost.description)) {
            std::set<int> hit;
            for (int k : ix.search(w)) hit.insert(k);
            if (w.size() >= 5) for (int k : ix.didYouMean(w, cand.size())) hit.insert(k);   // "botle" -> "bottle"
            for (int k : hit) shared[k].push_back(w);
        }
        std::vector<LfMatch> out;
        long long lostDay = lost.timeUnknown ? -1 : lfDayNumber(lost.date);
        for (size_t k = 0; k < cand.size(); ++k) {
            const LfPost& f = posts_[cand[k]];
            bool sameCat = f.category == lost.category;
            auto sw = shared.find((int)k);
            if (!sameCat && sw == shared.end()) continue;                // other category and nothing in common
            LfMatch m{cand[k], sameCat ? 1 : 0, {sameCat ? std::string("same category (") + lfCategoryLabel(f.category) + ")"
                                                         : std::string("posted as ") + lfCategoryLabel(f.category)}};
            long long foundDay = lfDayNumber(f.date);
            if (lostDay >= 0 && foundDay >= 0) {
                // Date rules (an item doesn't wait in one place for long):
                //   found BEFORE it was lost      -> impossible, not a match
                //   same day / 1-2 days later     -> likely
                //   3-7 days later                -> still listed, with a warning (the page asks "are you sure?")
                //   more than 7 days later        -> not a match
                long long gap = foundDay - lostDay;
                if (gap < 0 || gap > kMatchMaxDays) continue;
                if (gap <= kMatchGoodDays) {
                    m.score += 2;
                    m.reasons.push_back(gap == 0 ? "found the same day" : "found " + std::to_string(gap) + (gap == 1 ? " day later" : " days later"));
                } else {
                    m.reasons.push_back("found " + std::to_string(gap) + " days later");
                    m.warning = "This was found " + std::to_string(gap) + " days after the day you lost your item. "
                                "Things rarely stay in one place that long, so it may be someone else's.";
                }
            }
            if (sw != shared.end()) {
                size_t n = std::min<size_t>(sw->second.size(), 3);
                m.score += 2 * (int)n;
                std::string words;
                for (size_t i = 0; i < n; ++i) words += (i ? ", \"" : "\"") + sw->second[i] + "\"";
                m.reasons.push_back("similar words: " + words);
            }
            if (!lost.placeId.empty() && !f.placeId.empty()) {
                const MapPlace* a = map.placeById(lost.placeId);
                const MapPlace* b = map.placeById(f.placeId);
                if (lost.placeId == f.placeId) { m.score += 3; m.reasons.push_back(std::string("found at ") + (b ? b->name : "the same place")); }
                else if (a && b) {
                    MapRoute r = map.shortestPath(a->node, b->node);    // walking distance on the campus graph
                    if (r.found && r.length <= 350) { m.score += 2; m.reasons.push_back("found nearby, at " + b->name); }
                }
            }
            out.push_back(m);
        }
        std::sort(out.begin(), out.end(), [&](const LfMatch& x, const LfMatch& y) {
            return x.score != y.score ? x.score > y.score : posts_[x.post].createdAt > posts_[y.post].createdAt;
        });
        return out;
    }

    // Words worth matching: lowercase, letters/digits, 3+ characters, common words left out.
    static std::vector<std::string> wordsOf(const std::string& text) {
        static const std::set<std::string> stop = {"the", "and", "with", "has", "have", "had", "was", "are", "for", "from", "near",
            "this", "that", "its", "it's", "my", "mine", "lost", "found", "item", "one", "some", "very", "color", "colour", "inside", "small", "big"};
        std::vector<std::string> out;
        std::string cur;
        for (char ch : normalize(text) + " ") {
            unsigned char c = (unsigned char)ch;
            if (std::isalnum(c) || c >= 0x80) cur += ch;
            else {
                if (cur.size() >= 3 && !stop.count(cur) && std::find(out.begin(), out.end(), cur) == out.end()) out.push_back(cur);
                cur.clear();
            }
        }
        return out;
    }

    // ---------- success measures (for the desk page and the PBL report) ----------
    struct Stats { int found = 0, lost = 0, returned = 0, returnedIn30 = 0, waiting = 0; double avgHours = 0; };
    Stats stats(long long now) const {
        Stats s;
        double hours = 0;
        for (const LfPost& p : posts_) {
            if (p.type == "lost") { ++s.lost; continue; }
            ++s.found;
            if (p.status == "returned") {
                ++s.returned;
                if (p.closedAt - p.createdAt <= 30LL * 86400) ++s.returnedIn30;
                hours += (p.closedAt - p.createdAt) / 3600.0;
            } else if (isActive(p, now)) ++s.waiting;
        }
        s.avgHours = s.returned ? hours / s.returned : 0;
        return s;
    }

private:
    template <class T> static long long nextId(const std::vector<T>& list) {
        long long m = 0;
        for (const T& x : list) m = std::max(m, x.id);
        return m + 1;
    }
    LfPost* find(long long id) { for (LfPost& p : posts_) if (p.id == id) return &p; return nullptr; }

    static std::string s(const JsonValue& v, const char* k) { const JsonValue* x = v.get(k); return x && x->isString() ? x->asString() : ""; }
    static long long n(const JsonValue& v, const char* k) { const JsonValue* x = v.get(k); return x && x->isNumber() ? (long long)x->asNumber() : 0; }
    static bool b(const JsonValue& v, const char* k) { const JsonValue* x = v.get(k); return x && x->type() == JsonValue::Bool && x->asBool(); }

    static LfPost postFromFile(const JsonValue& v) {
        LfPost p;
        p.id = n(v, "id"); p.type = s(v, "type"); p.category = s(v, "category"); p.description = s(v, "description");
        if (const JsonValue* ph = v.get("photos")) for (const JsonValue& x : ph->items()) if (x.isString() && isHex(x.asString(), 32)) p.photos.push_back(x.asString());
        p.placeId = s(v, "placeId"); p.placeText = s(v, "placeText"); p.placeUnknown = b(v, "placeUnknown");
        p.date = s(v, "date"); p.dayPart = s(v, "dayPart"); p.timeUnknown = b(v, "timeUnknown");
        p.privateNote = s(v, "privateNote"); p.owner = s(v, "owner"); p.status = s(v, "status"); p.hidden = b(v, "hidden");
        if (const JsonValue* r = v.get("reportedBy")) for (const JsonValue& x : r->items()) if (x.isString()) p.reportedBy.push_back(x.asString());
        p.createdAt = n(v, "createdAt"); p.closedAt = n(v, "closedAt"); p.closedReason = s(v, "closedReason"); p.returnedTo = s(v, "returnedTo");
        if (p.status.empty()) p.status = "open";
        return p;
    }
    static LfClaim claimFromFile(const JsonValue& v) {
        LfClaim c;
        c.id = n(v, "id"); c.postId = n(v, "postId"); c.user = s(v, "user"); c.description = s(v, "description");
        c.status = s(v, "status"); c.createdAt = n(v, "createdAt"); c.decidedAt = n(v, "decidedAt");
        c.lostPostId = n(v, "lostPostId");
        if (c.status.empty()) c.status = "pending";
        return c;
    }

    bool savePosts() const {
        std::ostringstream f;
        f << "{\n\"note\": \"Lost & Found posts, one per line. Times are seconds since 1970. To remove a post, delete its line "
             "(and the comma before it if it was the last one) and its photos in photos/.\",\n\"posts\": [\n";
        for (size_t i = 0; i < posts_.size(); ++i) {
            const LfPost& p = posts_[i];
            JsonWriter j;
            j.beginObject().key("id").num(p.id).key("type").str(p.type).key("category").str(p.category).key("description").str(p.description)
                .key("photos").beginArray();
            for (const std::string& ph : p.photos) j.str(ph);
            j.endArray().key("placeId").str(p.placeId).key("placeText").str(p.placeText).key("placeUnknown").boolean(p.placeUnknown)
                .key("date").str(p.date).key("dayPart").str(p.dayPart).key("timeUnknown").boolean(p.timeUnknown)
                .key("privateNote").str(p.privateNote).key("owner").str(p.owner).key("status").str(p.status).key("hidden").boolean(p.hidden)
                .key("reportedBy").beginArray();
            for (const std::string& r : p.reportedBy) j.str(r);
            j.endArray().key("createdAt").num(p.createdAt).key("closedAt").num(p.closedAt).key("closedReason").str(p.closedReason)
                .key("returnedTo").str(p.returnedTo).endObject();
            f << j.text() << (i + 1 < posts_.size() ? ",\n" : "\n");
        }
        f << "]\n}\n";
        return writeFileAtomic(dir_ + "/posts.json", f.str());
    }
    bool saveClaims() const {
        std::ostringstream f;
        f << "{\n\"note\": \"Lost & Found claims, one per line.\",\n\"claims\": [\n";
        for (size_t i = 0; i < claims_.size(); ++i) {
            const LfClaim& c = claims_[i];
            JsonWriter j;
            j.beginObject().key("id").num(c.id).key("postId").num(c.postId).key("user").str(c.user).key("description").str(c.description)
                .key("status").str(c.status).key("createdAt").num(c.createdAt).key("decidedAt").num(c.decidedAt)
                .key("lostPostId").num(c.lostPostId).endObject();
            f << j.text() << (i + 1 < claims_.size() ? ",\n" : "\n");
        }
        f << "]\n}\n";
        return writeFileAtomic(dir_ + "/claims.json", f.str());
    }

    std::string dir_;
    int postDays_;
    std::vector<LfPost> posts_;
    std::vector<LfClaim> claims_;
};

} // namespace unigo
