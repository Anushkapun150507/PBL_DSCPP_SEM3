// campus_handlers.h -- API for the Campus Map and the Classroom Finder.
// Each class inherits RequestHandler and overrides handle(), exactly like the Bus Finder handlers.
//
//   GET /api/map                     -> the whole map (junctions, roads, places) for drawing
//   GET /api/places/search?q=        -> matching buildings/places + "did you mean"  (Find a Route tool)
//   GET /api/places/suggest?q=       -> up to 8 places for autocomplete
//   GET /api/route?from=&to=         -> up to 4 different routes (Dijkstra + Yen's k-shortest paths)
//   GET /api/rooms/search?q=         -> matching rooms + "did you mean"  (Find a Classroom / Lab tool)
//   GET /api/rooms/suggest?q=        -> up to 8 rooms for autocomplete
//   GET /api/rooms/in?place=         -> rooms inside one building
//   GET /api/rooms/meta              -> is the classroom data loaded, and how many rooms
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>
#include "campus_store.h"
#include "http.h"
#include "json_writer.h"

namespace unigo {

// Same validation as the Bus Finder, shared by both campus searches.
inline bool campusQueryError(const std::string& raw, HttpResponse& out) {
    if (normalize(raw).empty()) { out = HttpResponse::error(400, "empty_query", "Please enter a name to search."); return true; }
    if (!isValidQuery(raw)) {
        out = HttpResponse::error(400, "invalid_query",
            "Use letters, numbers, spaces and simple symbols like - . / ( ) # only (max 60 characters).");
        return true;
    }
    return false;
}

inline void writePlace(JsonWriter& j, const MapPlace& p, int roomCount) {
    j.beginObject()
        .key("id").str(p.id).key("name").str(p.name).key("type").str(p.type)
        .key("x").num((long long)std::lround(p.x)).key("y").num((long long)std::lround(p.y))
        .key("rooms").num(roomCount)
        .endObject();
}

inline void writeRoom(JsonWriter& j, const Room& r, const CampusMap& map) {
    j.beginObject()
        .key("id").str(r.id).key("name").str(r.name).key("building").str(r.building).key("floor").str(r.floor)
        .key("category").str(r.category).key("purpose").str(r.purpose)
        .key("status").str(r.status).key("verified").boolean(r.verified);
    j.key("keywords").beginArray();
    for (const auto& k : r.keywords) j.str(k);
    j.endArray().key("placeId");
    if (r.place >= 0) j.str(map.places()[r.place].id); else j.null();
    j.key("placeName");
    if (r.place >= 0) j.str(map.places()[r.place].name); else j.null();
    j.endObject();
}

class MapDataHandler : public RequestHandler {
public:
    explicit MapDataHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest&) override { return HttpResponse::json(store_.mapText()); }
private:
    CampusStore& store_;
};

class PlaceSearchHandler : public RequestHandler {
public:
    explicit PlaceSearchHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest& req) override {
        const std::string raw = req.param("q");
        HttpResponse err;
        if (campusQueryError(raw, err)) return err;
        const CampusMap& map = store_.map();
        const RoomDatabase& rooms = store_.rooms();
        std::vector<int> found = map.searchPlaces(raw);
        JsonWriter j;
        j.beginObject().key("query").str(trim(raw)).key("count").num((long long)found.size()).key("results").beginArray();
        for (int i : found) {
            j.beginObject().key("exactMatch").boolean(map.isExactName(i, raw)).key("place");
            writePlace(j, map.places()[i], (int)rooms.roomsInPlace(i).size());
            j.endObject();
        }
        j.endArray().key("didYouMean").beginArray();
        if (found.empty()) for (int i : map.didYouMean(raw)) writePlace(j, map.places()[i], (int)rooms.roomsInPlace(i).size());
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
private:
    CampusStore& store_;
};

class PlaceSuggestHandler : public RequestHandler {
public:
    explicit PlaceSuggestHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest& req) override {
        const std::string raw = req.param("q");
        JsonWriter j;
        j.beginObject().key("suggestions").beginArray();
        if (!normalize(raw).empty() && isValidQuery(raw)) {
            const CampusMap& map = store_.map();
            std::vector<int> found = map.searchPlaces(raw);
            for (size_t i = 0; i < found.size() && i < 8; ++i) {
                const MapPlace& p = map.places()[found[i]];
                j.beginObject().key("id").str(p.id).key("label").str(p.name).key("sub").str(p.type).endObject();
            }
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
private:
    CampusStore& store_;
};

class RouteHandler : public RequestHandler {
public:
    explicit RouteHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest& req) override {
        const CampusMap& map = store_.map();
        End from, to;
        std::string problem;
        if (!resolve(map, req.param("from"), from, problem) || !resolve(map, req.param("to"), to, problem))
            return HttpResponse::error(problem == "missing" ? 400 : 404, "bad_endpoint",
                problem == "missing" ? "Please choose both a starting point and a destination." : problem);

        std::vector<MapRoute> routes = map.alternativeRoutes(from.node, to.node);   // Yen's k-shortest + diversity
        if (routes.empty()) return HttpResponse::error(404, "no_route", "No connected path was found between these two places.");

        JsonWriter j;
        j.beginObject().key("found").boolean(true);
        writeEnd(j, "from", from);
        writeEnd(j, "to", to);
        j.key("routes").beginArray();                              // routes[0] is the shortest
        for (const MapRoute& r : routes) {
            j.beginObject().key("points").beginArray();
            if (from.free) point(j, from.x, from.y);               // the exact spot that was clicked
            for (int n : r.nodes) point(j, map.nodeX(n), map.nodeY(n));
            if (to.free) point(j, to.x, to.y);
            j.endArray();
            j.key("length").num((long long)std::lround(r.length));
            j.key("via").beginArray();
            for (const auto& v : r.via) j.str(v);
            j.endArray().endObject();
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }

private:
    struct End { int node = -1; bool free = false; double x = 0, y = 0; const MapPlace* place = nullptr; };

    // An end is either a place id ("csit-block") or a point on the map ("812.5,430").
    static bool resolve(const CampusMap& map, const std::string& raw, End& e, std::string& problem) {
        std::string s = trim(raw);
        if (s.empty()) { problem = "missing"; return false; }
        double x, y;
        char extra;
        if (std::sscanf(s.c_str(), "%lf,%lf%c", &x, &y, &extra) == 2) {
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > map.width() || y > map.height()) {
                problem = "That point is outside the campus map.";
                return false;
            }
            e.free = true; e.x = x; e.y = y;
            e.node = map.nearestNode(x, y);                        // snap the click onto the road network
            return e.node >= 0;
        }
        e.place = map.placeById(s);
        if (!e.place) { problem = "Unknown place: " + s; return false; }
        e.node = e.place->node;
        e.x = e.place->x; e.y = e.place->y;
        return true;
    }
    static void point(JsonWriter& j, double x, double y) {
        j.beginArray().num((long long)std::lround(x)).num((long long)std::lround(y)).endArray();
    }
    static void writeEnd(JsonWriter& j, const char* key, const End& e) {
        j.key(key).beginObject();
        if (e.place) j.key("id").str(e.place->id).key("name").str(e.place->name);
        else j.key("id").null().key("name").str("Selected point");
        j.key("x").num((long long)std::lround(e.x)).key("y").num((long long)std::lround(e.y)).endObject();
    }

    CampusStore& store_;
};

class RoomSearchHandler : public RequestHandler {
public:
    explicit RoomSearchHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest& req) override {
        const std::string raw = req.param("q");
        HttpResponse err;
        if (campusQueryError(raw, err)) return err;
        const CampusMap& map = store_.map();
        const RoomDatabase& db = store_.rooms();
        JsonWriter j;
        j.beginObject().key("loaded").boolean(store_.roomsLoaded()).key("status").str(store_.roomsStatus());
        std::vector<int> found = db.search(raw);
        j.key("query").str(trim(raw)).key("count").num((long long)found.size()).key("results").beginArray();
        for (int i : found) {
            j.beginObject().key("exactMatch").boolean(db.isExactName(i, raw)).key("room");
            writeRoom(j, db.rooms()[i], map);
            j.endObject();
        }
        j.endArray().key("didYouMean").beginArray();
        if (found.empty()) {
            std::unordered_set<std::string> seen;
            for (int i : db.didYouMean(raw)) {
                const Room& r = db.rooms()[i];
                if (seen.insert(normalize(r.name + "|" + r.building)).second) writeRoom(j, r, map);
            }
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
private:
    CampusStore& store_;
};

class RoomSuggestHandler : public RequestHandler {
public:
    explicit RoomSuggestHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest& req) override {
        const std::string raw = req.param("q");
        JsonWriter j;
        j.beginObject().key("suggestions").beginArray();
        if (!normalize(raw).empty() && isValidQuery(raw)) {
            const RoomDatabase& db = store_.rooms();
            std::vector<int> found = db.search(raw);
            for (size_t i = 0; i < found.size() && i < 8; ++i) {
                const Room& r = db.rooms()[found[i]];
                std::string sub = r.building + (r.floor.empty() ? "" : " · " + r.floor);   // e.g. "CSIT Block · Ground Floor"
                j.beginObject().key("label").str(r.name).key("sub").str(sub).endObject();
            }
        }
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
private:
    CampusStore& store_;
};

// GET /api/rooms/meta -> is the classroom data loaded, and how many rooms.
class RoomsMetaHandler : public RequestHandler {
public:
    explicit RoomsMetaHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest&) override {
        const RoomDatabase& db = store_.rooms();
        JsonWriter j;
        j.beginObject().key("loaded").boolean(store_.roomsLoaded()).key("status").str(store_.roomsStatus())
            .key("count").num((long long)db.rooms().size()).endObject();
        return HttpResponse::json(j.text());
    }
private:
    CampusStore& store_;
};

class RoomsInPlaceHandler : public RequestHandler {
public:
    explicit RoomsInPlaceHandler(CampusStore& s) : store_(s) {}
    HttpResponse handle(const HttpRequest& req) override {
        const CampusMap& map = store_.map();
        const RoomDatabase& db = store_.rooms();
        const MapPlace* p = map.placeById(trim(req.param("place")));
        if (!p) return HttpResponse::error(404, "not_found", "Unknown place.");
        int idx = (int)(p - &map.places()[0]);
        JsonWriter j;
        j.beginObject().key("loaded").boolean(store_.roomsLoaded()).key("place");
        writePlace(j, *p, (int)db.roomsInPlace(idx).size());
        j.key("rooms").beginArray();
        for (int i : db.roomsInPlace(idx)) writeRoom(j, db.rooms()[i], map);
        j.endArray().endObject();
        return HttpResponse::json(j.text());
    }
private:
    CampusStore& store_;
};

} // namespace unigo
