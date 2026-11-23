// review_handlers.h -- the Reviews & Feedback API.
//
//   GET  /api/reviews?limit=10&offset=0  -> summary (average, stars, features) + newest reviews + the choices
//   POST /api/reviews                    -> post a review (JSON body), answers 201 with the saved review
//
// The same address does both, so this handler overrides allows() to accept POST as well as GET.
#pragma once
#include <cstdlib>
#include <string>
#include "http.h"
#include "json_reader.h"
#include "json_writer.h"
#include "review_store.h"

namespace unigo {

class ReviewsHandler : public RequestHandler {
public:
    explicit ReviewsHandler(ReviewStore& store) : store_(store) {}

    bool allows(const std::string& method) const override { return method == "GET" || method == "POST"; }

    HttpResponse handle(const HttpRequest& req) override {
        return req.method == "POST" ? post(req) : list(req);
    }

private:
    // ---------- GET: summary + one page of reviews, newest first ----------
    HttpResponse list(const HttpRequest& req) {
        int limit = 10, offset = 0;
        if (!readInt(req.param("limit"), 1, 50, limit) || !readInt(req.param("offset"), 0, 1000000, offset))
            return HttpResponse::error(400, "invalid_page", "limit must be 1-50 and offset 0 or more.");
        const std::vector<Review>& all = store_.reviews();
        JsonWriter j;
        j.beginObject();
        writeOptions(j);
        writeSummary(j, all);
        j.key("total").num((long long)all.size()).key("offset").num(offset).key("limit").num(limit).key("reviews").beginArray();
        for (int k = 0; k < limit; ++k) {
            long long i = (long long)all.size() - 1 - offset - k;         // walk backwards: newest first
            if (i < 0) break;
            writeReview(j, all[(size_t)i]);
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }

    // ---------- POST: check, save, answer ----------
    HttpResponse post(const HttpRequest& req) {
        // Only JSON is accepted. Browsers can't send JSON to another site's server without asking
        // first (a CORS "preflight", which we never approve), so other websites can't post here.
        if (req.header("content-type").find("application/json") != 0)
            return HttpResponse::error(415, "json_only", "Reviews must be sent as JSON.");
        JsonValue body;
        try { body = JsonParser::parse(req.body); }
        catch (const std::exception&) { return HttpResponse::error(400, "bad_json", "The review could not be read."); }

        Review r;
        std::string field, message;
        if (!reviewFromJson(body, r, field, message, true)) {
            JsonWriter j;
            j.beginObject().key("error").str("invalid").key("field").str(field).key("message").str(message).endObject();
            return HttpResponse::json(j.text(), 400);
        }
        Review saved;
        switch (store_.add(r, saved)) {
            case ReviewStore::AddResult::Duplicate:
                return HttpResponse::error(409, "duplicate", "This review has already been posted.");
            case ReviewStore::AddResult::SaveFailed:
                return HttpResponse::error(500, "save_failed", "Your review could not be saved. Please try again.");
            case ReviewStore::AddResult::Added: break;
        }
        JsonWriter j;
        j.beginObject().key("review");
        writeReview(j, saved);
        writeSummary(j, store_.reviews());
        j.endObject();
        return HttpResponse::json(j.text(), 201);
    }

    static bool readInt(const std::string& s, int lo, int hi, int& out) {
        if (s.empty()) return true;                                     // keep the default
        if (s.size() > 7) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        int v = std::atoi(s.c_str());
        if (v < lo || v > hi) return false;
        out = v;
        return true;
    }

    static void writeOptions(JsonWriter& j) {
        j.key("options").beginObject().key("lines").beginArray();
        for (int s = 1; s <= 5; ++s) j.str(reviewLine(s));
        j.endArray().key("features").beginArray();
        for (const FeatureOption& o : featureOptions()) j.beginObject().key("key").str(o.key).key("label").str(o.label).endObject();
        j.endArray().key("nameMax").num(kNameMax).key("suggestionMax").num(kSuggestionMax).endObject();
    }

    static void writeSummary(JsonWriter& j, const std::vector<Review>& all) {
        long long starCount[6] = {0, 0, 0, 0, 0, 0}, total = 0, suggestions = 0;
        std::vector<long long> featureCount(featureOptions().size(), 0);
        for (const Review& r : all) {
            ++starCount[r.stars];
            total += r.stars;
            if (!r.suggestion.empty()) ++suggestions;
            for (size_t f = 0; f < featureOptions().size(); ++f)
                for (const std::string& k : r.features) if (k == featureOptions()[f].key) ++featureCount[f];
        }
        j.key("summary").beginObject().key("count").num((long long)all.size())
            .key("average").decimal(all.empty() ? 0.0 : (double)total / all.size())
            .key("stars").beginArray();
        for (int s = 1; s <= 5; ++s) j.num(starCount[s]);              // [1-star count, ..., 5-star count]
        j.endArray().key("features").beginObject();
        for (size_t f = 0; f < featureOptions().size(); ++f) j.key(featureOptions()[f].key).num(featureCount[f]);
        j.endObject().key("suggestions").num(suggestions).endObject();
    }

    ReviewStore& store_;
};

} // namespace unigo
