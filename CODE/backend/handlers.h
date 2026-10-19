// handlers.h -- one class per URL. Each inherits RequestHandler and overrides handle().
#pragma once
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "data_store.h"
#include "http.h"
#include "json_writer.h"

namespace unigo {

inline int nowMinutes() {
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    return lt->tm_hour * 60 + lt->tm_min;
}

// GET /api/search?q=<destination>  ->  every matching stop with its boarding point and timings.
// When nothing matches, "didYouMean" lists the closest real place names (typo help).
class SearchHandler : public RequestHandler {
public:
    explicit SearchHandler(DataStore& store) : store_(store) {}

    HttpResponse handle(const HttpRequest& req) override {
        const std::string raw = req.param("q");
        const std::string q = normalize(raw);
        if (q.empty()) return HttpResponse::error(400, "empty_query", "Please enter a destination to search.");
        if (!isValidQuery(raw))
            return HttpResponse::error(400, "invalid_query",
                "Destination names can only contain letters, numbers and basic punctuation (max 60 characters).");

        const BusDatabase& db = store_.db();
        std::vector<const BusStop*> results = db.search(q);
        const int now = nowMinutes();

        JsonWriter j;
        j.beginObject().key("query").str(trim(raw)).key("count").num((long long)results.size());
        j.key("results").beginArray();
        for (const BusStop* s : results) writeStop(j, *s, q, now);
        j.endArray();
        j.key("didYouMean").beginArray();
        if (results.empty()) for (const std::string& name : db.didYouMean(q)) j.str(name);
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }

private:
    static void writeStop(JsonWriter& j, const BusStop& s, const std::string& normQuery, int now) {
        static const char* LABELS[BusStop::SHIFTS] = {"1st Shift", "2nd Shift", "3rd Shift"};
        const int next = s.nextShift(now);
        j.beginObject()
            .key("place").str(s.place())
            .key("departureFrom").str(s.departFrom())
            .key("exactMatch").boolean(normalize(s.place()) == normQuery)
            .key("shifts").beginArray();
        for (int i = 0; i < BusStop::SHIFTS; ++i) {
            const bool missing = s.shift(i).empty();
            j.beginObject()
                .key("label").str(LABELS[i])
                .key("time").str(missing ? "Not available" : s.shift(i))
                .key("runs").boolean(s.runsIn(i))
                .key("next").boolean(i == next)
                .endObject();
        }
        j.endArray().key("nextBus");
        if (next >= 0) j.str(s.shift(next)); else j.null();
        j.endObject();
    }

    DataStore& store_;   // composition by reference: the handler uses the store, it doesn't own it
};

// GET /api/suggest?q=<text>  ->  up to 8 place names for the autocomplete dropdown.
class SuggestHandler : public RequestHandler {
public:
    explicit SuggestHandler(DataStore& store) : store_(store) {}

    HttpResponse handle(const HttpRequest& req) override {
        const std::string raw = req.param("q");
        JsonWriter j;
        j.beginObject().key("suggestions").beginArray();
        if (!normalize(raw).empty() && isValidQuery(raw)) {
            std::vector<const BusStop*> res = store_.db().search(raw);
            for (size_t i = 0; i < res.size() && i < 8; ++i) j.str(res[i]->place());
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }

private:
    DataStore& store_;
};

// GET /api/meta  ->  how many places are loaded and the list of departure points.
class MetaHandler : public RequestHandler {
public:
    explicit MetaHandler(DataStore& store) : store_(store) {}

    HttpResponse handle(const HttpRequest&) override {
        const BusDatabase& db = store_.db();
        JsonWriter j;
        j.beginObject().key("places").num((long long)db.size()).key("departurePoints").beginArray();
        for (const std::string& p : db.departurePoints()) j.str(p);
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }

private:
    DataStore& store_;
};

// Everything else: files from the frontend folder (index.html, css/style.css, js/app.js).
class StaticFileHandler : public RequestHandler {
public:
    explicit StaticFileHandler(std::string root) : root_(std::move(root)) {}

    HttpResponse handle(const HttpRequest& req) override {
        std::string path = req.path == "/" ? "/index.html" : req.path;
        // Only plain paths inside the frontend folder: no "..", backslashes or drive letters.
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos ||
            path.find(':') != std::string::npos)
            return HttpResponse::error(400, "bad_path", "Invalid path.");
        std::ifstream f(root_ + path, std::ios::binary);
        if (!f) return HttpResponse::error(404, "not_found", "Page not found.");
        std::stringstream ss;
        ss << f.rdbuf();
        HttpResponse r;
        r.contentType = typeFor(path);
        r.body = ss.str();
        return r;
    }

private:
    static std::string typeFor(const std::string& path) {
        static const std::unordered_map<std::string, std::string> TYPES = {
            {".html", "text/html; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
            {".js", "application/javascript; charset=utf-8"}, {".svg", "image/svg+xml"},
            {".png", "image/png"}, {".ico", "image/x-icon"}};
        size_t dot = path.rfind('.');
        auto it = dot == std::string::npos ? TYPES.end() : TYPES.find(path.substr(dot));
        return it == TYPES.end() ? "application/octet-stream" : it->second;
    }

    std::string root_;
};

} // namespace unigo
