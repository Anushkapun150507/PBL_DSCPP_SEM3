// lostfound_handlers.h -- the Lost & Found API (posts, matches, claims, the collection desk, photos).
// Sign up / log in are in auth_handlers.h; every Lost & Found handler uses that login.
//
//   GET  /api/lostfound/meta                   categories, collection point, rules, public counts
//   GET  /api/lostfound/posts?type=found|lost&category=&q=&place=   POST /api/lostfound/posts (with photos)
//   GET  /api/lostfound/mine                    my posts + my claims
//   POST /api/lostfound/close                   close my own post
//   GET  /api/lostfound/matches?post=<id>       possible matches for my lost post
//   POST /api/lostfound/claims                  "This is mine"
//   POST /api/lostfound/decide                  finder approves/rejects a claim; owner marks "I got it back"
//   POST /api/lostfound/report                  report a post
//   GET  /api/lostfound/admin   POST /api/lostfound/admin/decide | /api/lostfound/admin/hide   (desk only)
//   GET  /api/lostfound/photo?id=<32 hex>
//
// Every address needs a login (except meta). Writes accept JSON only: browsers can't send
// JSON to another site's server without asking first (a CORS preflight we never approve), and the session
// cookie is SameSite=Strict, so other websites can't act for a logged-in student.
#pragma once
#include <string>
#include <vector>
#include "auth_handlers.h"
#include "campus_store.h"
#include "lostfound_store.h"

namespace unigo {

// Shared by every Lost & Found handler: the login and JSON helpers (from AuthHandlerBase) plus the stores.
class LfHandlerBase : public AuthHandlerBase {
public:
    LfHandlerBase(AuthStore& auth, LostFoundStore& store, CampusStore& campus) : AuthHandlerBase(auth), store_(store), campus_(campus) {}

protected:
    static HttpResponse needLogin() { return HttpResponse::error(401, "login_required", "Please log in to use Lost & Found."); }

    // One post as JSON. Other students never get the owner's roll number or the private note.
    void writePost(JsonWriter& j, const LfPost& p, const LfUser* viewer, long long matchCount = -1) {
        const CampusMap& map = campus_.map();
        bool mine = viewer && viewer->id == p.owner, admin = viewer && viewer->role == "admin";
        long long t = now();
        j.beginObject().key("id").num(p.id).key("type").str(p.type).key("category").str(p.category)
            .key("categoryLabel").str(lfCategoryLabel(p.category)).key("sensitive").boolean(lfSensitive(p.category))
            .key("description").str(p.description).key("photos").beginArray();
        for (const std::string& ph : p.photos) j.str("/api/lostfound/photo?id=" + ph);
        j.endArray().key("place").beginObject();
        if (p.placeUnknown) j.key("unknown").boolean(true);
        else if (!p.placeId.empty()) {
            const MapPlace* mp = map.placeById(p.placeId);
            j.key("id").str(p.placeId).key("name").str(mp ? mp->name : p.placeId);
        } else j.key("text").str(p.placeText);
        j.endObject().key("date").str(p.date).key("dayPart").str(p.dayPart).key("timeUnknown").boolean(p.timeUnknown)
            .key("status").str(store_.effectiveStatus(p, t)).key("postedOn").str(lfDateOf(p.createdAt))
            .key("expiresOn").str(lfDateOf(p.createdAt + (long long)store_.postDays() * 86400)).key("mine").boolean(mine);
        if (p.type == "found")
            j.key("collectionPoint").beginObject().key("name").str(auth_.config().collectionPoint)
                .key("placeId").str(auth_.config().collectionPlaceId).endObject();
        if (mine || admin) j.key("privateNote").str(p.privateNote).key("closedReason").str(p.closedReason);
        if (admin) j.key("owner").str(p.owner).key("hidden").boolean(p.hidden).key("reports").num((long long)p.reportedBy.size())
                     .key("returnedTo").str(p.returnedTo);
        if (matchCount >= 0) j.key("matchCount").num(matchCount);
        j.endObject();
    }

    LostFoundStore& store_;
    CampusStore& campus_;
};

// ---------- choices, rules and public counts ----------
class LfMetaHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    HttpResponse handle(const HttpRequest&) override {
        const LfConfig& c = auth_.config();
        LostFoundStore::Stats s = store_.stats(now());
        JsonWriter j;
        j.beginObject().key("categories").beginArray();
        for (const LfCategory& cat : lfCategories())
            j.beginObject().key("key").str(cat.key).key("label").str(cat.label).key("sensitive").boolean(lfSensitive(cat.key)).endObject();
        j.endArray().key("dayParts").beginArray();
        for (const std::string& d : lfDayParts()) j.str(d);
        j.endArray().key("collectionPoint").beginObject().key("name").str(c.collectionPoint).key("placeId").str(c.collectionPlaceId).endObject()
            .key("rollDigits").num(c.rollDigits).key("postDays").num(c.postDays).key("today").str(lfDateOf(now()))
            .key("limits").beginObject().key("lostDescMin").num((long long)kLostDescMin).key("lostDescMax").num((long long)kLostDescMax)
            .key("foundDescMin").num((long long)kFoundDescMin).key("foundDescMax").num((long long)kFoundDescMax)
            .key("noteMax").num((long long)kNoteMax).key("claimMin").num((long long)kClaimMin).key("claimMax").num((long long)kClaimMax)
            .key("maxPhotos").num((long long)kMaxPhotos).key("photoMaxBytes").num((long long)kPhotoMaxBytes).endObject()
            .key("counts").beginObject().key("returned").num(s.returned).key("waiting").num(s.waiting).endObject()
            .endObject();
        return HttpResponse::json(j.text());
    }
};

// ---------- posts: browse and create ----------
class LfPostsHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    bool allows(const std::string& m) const override { return m == "GET" || m == "POST"; }
    size_t maxBody() const override { return 2 * 1024 * 1024; }     // two photos of up to 600 KB (as base64) + text

    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        return req.method == "POST" ? create(req, *u) : list(req, u);
    }

private:
    HttpResponse list(const HttpRequest& req, const LfUser* u) {
        std::string type = req.param("type").empty() ? "found" : req.param("type");
        std::string category = req.param("category"), place = req.param("place"), q = req.param("q");
        if (type != "found" && type != "lost") return HttpResponse::error(400, "bad_type", "type must be found or lost.");
        if (!category.empty() && !*lfCategoryLabel(category)) return HttpResponse::error(400, "bad_category", "Unknown category.");
        if (q.size() > 60) return HttpResponse::error(400, "bad_query", "Search for at most 60 characters.");
        const CampusMap& map = campus_.map();
        long long t = now();
        std::vector<size_t> rows;
        for (size_t i = store_.posts().size(); i-- > 0;) {                // newest first
            const LfPost& p = store_.posts()[i];
            if (p.type != type || !store_.isActive(p, t)) continue;
            if (!category.empty() && p.category != category) continue;
            if (!place.empty() && p.placeId != place) continue;
            rows.push_back(i);
        }
        if (!trim(q).empty()) {                                          // typo-tolerant search over the text
            SearchIndex ix;
            for (size_t k = 0; k < rows.size(); ++k) {
                const LfPost& p = store_.posts()[rows[k]];
                const MapPlace* mp = map.placeById(p.placeId);
                std::vector<std::string> names = LostFoundStore::wordsOf(p.description);
                names.push_back(lfCategoryLabel(p.category));
                ix.add((int)k, names, {p.description, mp ? mp->name : p.placeText});
            }
            std::vector<int> hit = ix.search(q);
            if (hit.empty()) hit = ix.didYouMean(q, rows.size());
            std::vector<size_t> kept;
            for (int k : hit) kept.push_back(rows[(size_t)k]);
            rows = kept;
        }
        JsonWriter j;
        j.beginObject().key("count").num((long long)rows.size()).key("posts").beginArray();
        for (size_t k = 0; k < rows.size() && k < 60; ++k) writePost(j, store_.posts()[rows[k]], u);
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }

    HttpResponse create(const HttpRequest& req, const LfUser& u) {
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        LfPost p;
        std::vector<LfPhotoUpload> photos;
        std::string field, message;
        if (!lfPostFromJson(b, campus_.map(), now(), p, photos, field, message)) return fieldError(field, message);
        long long id = store_.create(p, photos, u.id, now());
        if (!id) return HttpResponse::error(500, "save_failed", "Your post could not be saved. Please try again.");
        JsonWriter j;
        j.beginObject().key("post");
        writePost(j, *store_.post(id), &u);
        j.endObject();
        return HttpResponse::json(j.text(), 201);
    }
};

// ---------- my posts and my claims ----------
class LfMineHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        JsonWriter j;
        j.beginObject().key("posts").beginArray();
        long long t = now();
        for (size_t i = store_.posts().size(); i-- > 0;) {
            const LfPost& p = store_.posts()[i];
            if (p.owner != u->id) continue;
            bool lookFor = p.type == "lost" && store_.effectiveStatus(p, t) == "open";   // count matches so the page can say "2 possible matches"
            writePost(j, p, u, lookFor ? (long long)store_.possibleMatches(p, campus_.map(), t).size() : -1);
        }
        j.endArray().key("claims").beginArray();
        for (size_t i = store_.claims().size(); i-- > 0;) {
            const LfClaim& c = store_.claims()[i];
            if (c.user != u->id) continue;
            const LfPost* p = store_.post(c.postId);
            j.beginObject().key("id").num(c.id).key("status").str(c.status).key("description").str(c.description)
                .key("claimedOn").str(lfDateOf(c.createdAt)).key("post");
            if (p) writePost(j, *p, u); else j.null();
            j.endObject();
        }
        // Claims other students made on items I found. They show the claim text only, never the claimant's roll number.
        j.endArray().key("incoming").beginArray();
        for (size_t i = store_.claims().size(); i-- > 0;) {
            const LfClaim& c = store_.claims()[i];
            const LfPost* p = store_.post(c.postId);
            if (!p || p->owner != u->id) continue;
            j.beginObject().key("id").num(c.id).key("status").str(c.status).key("description").str(c.description)
                .key("claimedOn").str(lfDateOf(c.createdAt)).key("post");
            writePost(j, *p, u);
            j.endObject();
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
};

class LfCloseHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        std::string reason = text(b, "reason");
        if (reason != "found-myself" && reason != "owner-collected" && reason != "no-longer-needed")
            return fieldError("reason", "Choose why you are closing this post.");
        std::string problem = store_.close(number(b, "id"), u->id, reason, now());
        return problem.empty() ? ok() : fieldError("id", problem);
    }
};

// ---------- possible matches for my lost post ----------
class LfMatchesHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        const LfPost* lost = store_.post(std::atoll(req.param("post").c_str()));
        if (!lost || lost->type != "lost" || (lost->owner != u->id && u->role != "admin"))
            return HttpResponse::error(404, "not_found", "That lost post could not be found.");
        std::vector<LfMatch> matches = store_.possibleMatches(*lost, campus_.map(), now());
        JsonWriter j;
        j.beginObject().key("count").num((long long)matches.size()).key("matches").beginArray();
        for (const LfMatch& m : matches) {
            j.beginObject().key("score").num(m.score).key("strength").str(m.score >= 5 && m.warning.empty() ? "strong" : "possible")
                .key("warning").str(m.warning).key("reasons").beginArray();
            for (const std::string& r : m.reasons) j.str(r);
            j.endArray().key("post");
            writePost(j, store_.posts()[m.post], u);
            j.endObject();
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
};

// ---------- claims and reports ----------
class LfClaimHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        if (u->role == "admin") return HttpResponse::error(403, "desk_account", "The desk account can't claim items.");
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        std::string desc;
        std::string problem = cleanFreeText(text(b, "description"), kClaimMin, kClaimMax, "description", false, desc);
        if (!problem.empty()) return fieldError("description", problem);
        long long claimId = 0;
        problem = store_.addClaim(number(b, "postId"), u->id, desc, now(), claimId, number(b, "lostPostId"));
        if (!problem.empty()) return fieldError("postId", problem, problem.find("at most") != std::string::npos ? 429 : 400);
        JsonWriter j;
        j.beginObject().key("ok").boolean(true).key("claimId").num(claimId).key("collectionPoint").str(auth_.config().collectionPoint).endObject();
        return HttpResponse::json(j.text(), 201);
    }
};

class LfReportHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        std::string problem = store_.report(number(b, "postId"), u->id);
        return problem.empty() ? ok() : fieldError("postId", problem);
    }
};

// ---------- the collection desk (admin) ----------
class LfAdminHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        if (u->role != "admin") return HttpResponse::error(403, "desk_only", "Only the collection desk can open this page.");
        long long t = now();
        LostFoundStore::Stats s = store_.stats(t);
        JsonWriter j;
        j.beginObject().key("stats").beginObject().key("found").num(s.found).key("lost").num(s.lost).key("returned").num(s.returned)
            .key("returnedIn30").num(s.returnedIn30).key("waiting").num(s.waiting).key("avgHoursToReturn").decimal(s.avgHours).endObject()
            .key("claims").beginArray();
        for (size_t i = store_.claims().size(); i-- > 0;) {                // open claims first come from the newest
            const LfClaim& c = store_.claims()[i];
            if (c.status != "pending" && c.status != "approved" && c.status != "atdesk") continue;
            const LfPost* p = store_.post(c.postId);
            j.beginObject().key("id").num(c.id).key("status").str(c.status).key("user").str(c.user).key("description").str(c.description)
                .key("claimedOn").str(lfDateOf(c.createdAt)).key("post");
            if (p) writePost(j, *p, u); else j.null();
            j.endObject();
        }
        j.endArray().key("flagged").beginArray();                          // reported or hidden posts
        for (size_t i = store_.posts().size(); i-- > 0;) {
            const LfPost& p = store_.posts()[i];
            if (p.hidden || !p.reportedBy.empty()) writePost(j, p, u);
        }
        j.endArray().key("waiting").beginArray();                          // found items waiting for their owner
        for (size_t i = store_.posts().size(); i-- > 0;) {
            const LfPost& p = store_.posts()[i];
            if (p.type == "found" && store_.isActive(p, t)) writePost(j, p, u);
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
};

class LfDecideHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        long long claimId = number(b, "claimId");
        std::string decision = text(b, "decision");
        std::string problem = store_.mayDecide(claimId, u->id, u->role == "admin", decision);
        if (!problem.empty()) return HttpResponse::error(problem == "Claim not found." ? 404 : 403, "not_allowed", problem);
        problem = store_.decide(claimId, decision, now());
        return problem.empty() ? ok() : fieldError("decision", problem);
    }
};

class LfHideHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    bool allows(const std::string& m) const override { return m == "POST"; }
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        if (u->role != "admin") return HttpResponse::error(403, "desk_only", "Only the collection desk can hide posts.");
        JsonValue b; HttpResponse err;
        if (!readJson(req, b, err)) return err;
        const JsonValue* h = b.get("hidden");
        std::string problem = store_.setHidden(number(b, "postId"), h && h->type() == JsonValue::Bool && h->asBool());
        return problem.empty() ? ok() : fieldError("postId", problem);
    }
};

// ---------- photos ----------
class LfPhotoHandler : public LfHandlerBase {
public:
    using LfHandlerBase::LfHandlerBase;
    HttpResponse handle(const HttpRequest& req) override {
        const LfUser* u = user(req);
        if (!u) return needLogin();
        std::string id = req.param("id");
        if (!isHex(id, 32)) return HttpResponse::error(400, "bad_id", "Invalid photo id.");   // no "../" tricks possible
        const LfPost* p = store_.postWithPhoto(id);
        bool allowed = p && (!p->hidden || p->owner == u->id || u->role == "admin");
        if (!allowed) return HttpResponse::error(404, "not_found", "Photo not found.");
        HttpResponse r;
        if (!readWholeFile(store_.photoPath(id), r.body)) return HttpResponse::error(404, "not_found", "Photo not found.");
        r.contentType = "image/jpeg";
        return r;
    }
};

} // namespace unigo
