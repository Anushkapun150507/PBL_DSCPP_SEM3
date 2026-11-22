// campus_map.h -- the campus as a GRAPH: junctions are nodes, road/path pieces are weighted edges.
// Routes are found with Dijkstra's shortest-path algorithm. Places (buildings, gates, parks) are
// searchable with the shared SearchIndex. Data comes from data/campus_map.json.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "json_reader.h"
#include "search_index.h"
#include "text_utils.h"

namespace unigo {

struct MapPlace {
    std::string id, name, type;
    std::vector<std::string> aliases, keywords;
    int node = -1;            // graph node where routes to/from this place start or end (its door)
    double x = 0, y = 0;      // where the marker is drawn
};

struct MapRoute {
    bool found = false;
    std::vector<int> nodes;               // node ids from start to end
    double length = 0;                    // in map units (the map is a schematic, not to scale)
    std::vector<std::string> via;         // named roads used, in order ("Gate No. 2 Road", ...)
};

class CampusMap {
public:
    // Builds the graph and the place index from the parsed JSON. Throws on missing or broken data.
    void load(const JsonValue& root) {
        const JsonValue* nodes = root.get("nodes");
        const JsonValue* roads = root.get("roads");
        const JsonValue* places = root.get("places");
        if (!nodes || !nodes->isObject() || !roads || !roads->isArray() || !places || !places->isArray())
            throw std::runtime_error("campus map needs \"nodes\", \"roads\" and \"places\"");
        if (const JsonValue* meta = root.get("meta")) {
            if (const JsonValue* w = meta->get("width")) width_ = w->asNumber();
            if (const JsonValue* h = meta->get("height")) height_ = h->asNumber();
        }

        for (const auto& kv : nodes->members()) {                         // junctions
            const auto& xy = kv.second.items();
            if (xy.size() != 2) throw std::runtime_error("node " + kv.first + " needs [x, y]");
            addNode(kv.first, xy[0].asNumber(), xy[1].asNumber());
        }
        for (const JsonValue& r : roads->items()) {                       // roads -> edges
            const JsonValue* name = r.get("name");
            const JsonValue* kind = r.get("kind");
            const JsonValue* path = r.get("path");
            if (!path || !path->isArray()) throw std::runtime_error("every road needs a \"path\"");
            int road = (int)roadNames_.size();
            roadNames_.push_back(name ? name->asString() : "");
            roadKinds_.push_back(kind ? kind->asString() : "road");
            const auto& pts = path->items();
            for (size_t i = 1; i < pts.size(); ++i)
                addEdge(nodeId(pts[i - 1].asString()), nodeId(pts[i].asString()), road);
        }
        for (const JsonValue& p : places->items()) addPlace(p);
        if (places_.empty()) throw std::runtime_error("campus map has no places");
    }

    // ---- places ----
    const std::vector<MapPlace>& places() const { return places_; }
    const MapPlace* placeById(const std::string& id) const {
        auto it = placeById_.find(id);
        return it == placeById_.end() ? nullptr : &places_[it->second];
    }
    std::vector<int> searchPlaces(const std::string& q) const { return index_.search(q); }
    std::vector<int> didYouMean(const std::string& q, size_t k = 5) const { return index_.didYouMean(q, k); }
    bool isExactName(int place, const std::string& q) const { return index_.isExactName(place, q); }

    // Place whose name or alias equals `name` (ignoring case, spaces, dots, hyphens), or -1.
    // Used to link a classroom's building name to its building on the map.
    int placeByName(const std::string& name) const {
        std::string key = compactKey(normalize(name));
        if (key.empty()) return -1;
        for (size_t i = 0; i < places_.size(); ++i) {
            if (compactKey(normalize(places_[i].name)) == key) return (int)i;
            for (const auto& a : places_[i].aliases) if (compactKey(normalize(a)) == key) return (int)i;
        }
        return -1;
    }

    // ---- graph ----
    size_t nodeCount() const { return nx_.size(); }
    int nodeIndex(const std::string& key) const { auto it = nodeByKey_.find(key); return it == nodeByKey_.end() ? -1 : it->second; }
    double nodeX(int n) const { return nx_[n]; }
    double nodeY(int n) const { return ny_[n]; }
    double width() const { return width_; }
    double height() const { return height_; }

    // Nearest graph node to a point (for "I am here" clicks on the map). Linear scan, ~150 nodes.
    int nearestNode(double x, double y) const {
        int best = -1;
        double bestD = std::numeric_limits<double>::max();
        for (size_t i = 0; i < nx_.size(); ++i) {
            double d = std::hypot(nx_[i] - x, ny_[i] - y);
            if (d < bestD) { bestD = d; best = (int)i; }
        }
        return best;
    }

    // Nodes and road pieces a search must avoid (used by Yen's algorithm below).
    struct Blocked {
        std::vector<char> node;                           // node[v] = 1 -> v may not be used
        std::set<std::pair<int, int>> edge;               // {min(u,v), max(u,v)} -> that road piece may not be used
    };

    MapRoute shortestPath(int from, int to) const { return dijkstra(from, to, nullptr); }

    // Dijkstra's algorithm with a min-heap: O((V + E) log V).
    // dist[v] = shortest known distance from `from`; prev[v] = the node we came from.
    MapRoute dijkstra(int from, int to, const Blocked* blocked) const {
        MapRoute route;
        const size_t n = nx_.size();
        if (from < 0 || to < 0 || (size_t)from >= n || (size_t)to >= n) return route;
        const double INF = std::numeric_limits<double>::infinity();
        std::vector<double> dist(n, INF);
        std::vector<int> prev(n, -1);
        typedef std::pair<double, int> Item;                                       // (distance, node)
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;     // smallest distance on top
        dist[from] = 0;
        heap.emplace(0.0, from);
        while (!heap.empty()) {
            Item top = heap.top();
            heap.pop();
            int u = top.second;
            if (top.first > dist[u]) continue;            // stale entry: u was already settled cheaper
            if (u == to) break;                           // destination settled: its distance is final
            for (const Edge& e : adj_[u]) {
                if (blocked && (blocked->node[e.to] || blocked->edge.count(edgeKey(u, e.to)))) continue;
                double nd = dist[u] + e.w;
                if (nd < dist[e.to]) {                    // relaxation: found a shorter way to e.to
                    dist[e.to] = nd;
                    prev[e.to] = u;
                    heap.emplace(nd, e.to);
                }
            }
        }
        if (dist[to] == INF) return route;                // not connected
        for (int v = to; v != -1; v = prev[v]) route.nodes.push_back(v);   // walk back from the destination
        std::reverse(route.nodes.begin(), route.nodes.end());
        finish(route);
        return route;
    }

    // Up to `want` genuinely DIFFERENT routes, shortest first.
    // 1. Yen's k-shortest-paths algorithm lists the next-shortest loop-free routes, one by one:
    //    for every node of the previous route ("spur node"), it blocks the road pieces already used
    //    after the same beginning and runs Dijkstra again from the spur node.
    // 2. Routes that mostly repeat an already chosen route (>= 70% shared length) are skipped, so the
    //    user sees real alternatives (e.g. "via Gate No. 1" vs "via the subway"), not tiny detours.
    std::vector<MapRoute> alternativeRoutes(int from, int to, size_t want = 4) const {
        std::vector<MapRoute> chosen, A, B;
        MapRoute best = dijkstra(from, to, nullptr);
        if (!best.found) return chosen;
        A.push_back(best);
        const size_t K = 30;                              // how many shortest routes Yen's algorithm may list
        while (A.size() < K) {
            const std::vector<int>& last = A.back().nodes;
            for (size_t i = 0; i + 1 < last.size(); ++i) {
                Blocked bl;
                bl.node.assign(nx_.size(), 0);
                for (const MapRoute& p : A)               // same beginning as the root -> block its next piece
                    if (p.nodes.size() > i + 1 && std::equal(last.begin(), last.begin() + i + 1, p.nodes.begin()))
                        bl.edge.insert(edgeKey(p.nodes[i], p.nodes[i + 1]));
                for (size_t j = 0; j < i; ++j) bl.node[last[j]] = 1;   // keep the route loop-free
                MapRoute spur = dijkstra(last[i], to, &bl);
                if (!spur.found) continue;
                MapRoute total;
                total.nodes.assign(last.begin(), last.begin() + i);   // root path ...
                total.nodes.insert(total.nodes.end(), spur.nodes.begin(), spur.nodes.end());   // ... + spur path
                finish(total);
                bool known = false;
                for (const MapRoute& r : A) if (r.nodes == total.nodes) known = true;
                for (const MapRoute& r : B) if (r.nodes == total.nodes) known = true;
                if (!known) B.push_back(total);
            }
            if (B.empty()) break;                         // no more loop-free routes exist
            auto shortest = std::min_element(B.begin(), B.end(),
                [](const MapRoute& a, const MapRoute& b) { return a.length < b.length; });
            A.push_back(*shortest);
            B.erase(shortest);
        }
        for (const MapRoute& cand : A) {                  // A is already sorted shortest first
            if (chosen.size() >= want) break;
            if (cand.length > best.length * 2.2) break;   // far longer than the best: not a sensible option
            std::vector<std::string> sig = signature(cand);
            std::set<std::string> uniq(sig.begin(), sig.end());
            if (!chosen.empty() && uniq.size() != sig.size()) continue;   // zig-zags back onto a road it already used
            bool different = true;
            for (const MapRoute& c : chosen) {
                double own = cand.length - sharedLength(cand, c);         // length not shared with c
                if (own < 0.3 * cand.length || own < 120) different = false;   // just a small detour
                if (signature(cand) == signature(c)) different = false;  // same named roads: reads the same
            }
            if (different) chosen.push_back(cand);
        }
        return chosen;
    }

    // The named roads a route uses, ignoring generic connectors ("Campus Road", "Subway Path").
    static std::vector<std::string> signature(const MapRoute& r) {
        std::vector<std::string> s;
        for (const auto& v : r.via) if (v != "Campus Road" && v != "Subway Path") s.push_back(v);
        return s;
    }

private:
    struct Edge { int to; double w; int road; };          // road = index into roadNames_, -1 for a door path

    static std::pair<int, int> edgeKey(int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

    const Edge* findEdge(int a, int b) const {
        for (const Edge& e : adj_[a]) if (e.to == b) return &e;
        return nullptr;
    }

    // Fills length and the list of named roads ("via") from the route's node list.
    void finish(MapRoute& r) const {
        r.found = !r.nodes.empty();
        r.length = 0;
        r.via.clear();
        for (size_t i = 1; i < r.nodes.size(); ++i) {
            const Edge* e = findEdge(r.nodes[i - 1], r.nodes[i]);
            if (!e) continue;
            r.length += e->w;
            if (e->road < 0 || roadNames_[e->road].empty()) continue;
            if (r.via.empty() || r.via.back() != roadNames_[e->road]) r.via.push_back(roadNames_[e->road]);
        }
    }

    // Total length of the road pieces two routes have in common.
    double sharedLength(const MapRoute& a, const MapRoute& b) const {
        std::set<std::pair<int, int>> inB;
        for (size_t i = 1; i < b.nodes.size(); ++i) inB.insert(edgeKey(b.nodes[i - 1], b.nodes[i]));
        double shared = 0;
        for (size_t i = 1; i < a.nodes.size(); ++i)
            if (inB.count(edgeKey(a.nodes[i - 1], a.nodes[i]))) {
                const Edge* e = findEdge(a.nodes[i - 1], a.nodes[i]);
                if (e) shared += e->w;
            }
        return shared;
    }

    int addNode(const std::string& key, double x, double y) {
        if (nodeByKey_.count(key)) throw std::runtime_error("duplicate node " + key);
        int id = (int)nx_.size();
        nodeByKey_[key] = id;
        nx_.push_back(x);
        ny_.push_back(y);
        adj_.emplace_back();
        return id;
    }
    int nodeId(const std::string& key) const {
        auto it = nodeByKey_.find(key);
        if (it == nodeByKey_.end()) throw std::runtime_error("unknown node " + key);
        return it->second;
    }
    void addEdge(int a, int b, int road) {                // undirected: you can walk both ways
        double w = std::hypot(nx_[a] - nx_[b], ny_[a] - ny_[b]);
        adj_[a].push_back({b, w, road});
        adj_[b].push_back({a, w, road});
    }

    static std::vector<std::string> strings(const JsonValue* v) {
        std::vector<std::string> out;
        if (v && v->isArray()) for (const JsonValue& s : v->items()) if (s.isString()) out.push_back(s.asString());
        return out;
    }

    void addPlace(const JsonValue& p) {
        MapPlace pl;
        const JsonValue* id = p.get("id");
        const JsonValue* name = p.get("name");
        const JsonValue* link = p.get("link");
        if (!id || !name || !link) throw std::runtime_error("every place needs id, name and link");
        pl.id = id->asString();
        pl.name = name->asString();
        if (const JsonValue* t = p.get("type")) pl.type = t->asString();
        pl.aliases = strings(p.get("aliases"));
        pl.keywords = strings(p.get("keywords"));
        if (placeById_.count(pl.id)) throw std::runtime_error("duplicate place id " + pl.id);
        int linkNode = nodeId(link->asString());

        const JsonValue* door = p.get("door");
        if (door && door->items().size() == 2) {         // building: a door node joined to its road
            double dx = door->items()[0].asNumber(), dy = door->items()[1].asNumber();
            pl.node = addNode("door:" + pl.id, dx, dy);
            addEdge(pl.node, linkNode, -1);
        } else {
            pl.node = linkNode;                           // e.g. the subway: the place is on the network
        }
        const JsonValue* rect = p.get("rect");
        const JsonValue* marker = p.get("marker");
        if (rect && rect->items().size() == 4) {
            const auto& r = rect->items();
            pl.x = r[0].asNumber() + r[2].asNumber() / 2;
            pl.y = r[1].asNumber() + r[3].asNumber() / 2;
        } else if (marker && marker->items().size() == 2) {
            pl.x = marker->items()[0].asNumber();
            pl.y = marker->items()[1].asNumber();
        } else {
            pl.x = nx_[pl.node];
            pl.y = ny_[pl.node];
        }

        int idx = (int)places_.size();
        std::vector<std::string> names{pl.name};
        names.insert(names.end(), pl.aliases.begin(), pl.aliases.end());
        std::vector<std::string> words = pl.keywords;
        if (!pl.type.empty()) words.push_back(pl.type);
        index_.add(idx, names, words);
        placeById_[pl.id] = idx;
        places_.push_back(pl);
    }

    std::vector<double> nx_, ny_;                         // node coordinates
    std::vector<std::vector<Edge>> adj_;                  // adjacency list: adj_[u] = edges leaving u
    std::unordered_map<std::string, int> nodeByKey_;      // "B3" -> node id
    std::vector<std::string> roadNames_, roadKinds_;
    std::vector<MapPlace> places_;
    std::unordered_map<std::string, int> placeById_;      // "csit-block" -> index in places_
    SearchIndex index_;
    double width_ = 1600, height_ = 900;
};

} // namespace unigo
