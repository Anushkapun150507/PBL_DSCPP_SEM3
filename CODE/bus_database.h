// bus_database.h -- owns every BusStop plus the indexes that make searching fast.
#pragma once
#include <algorithm>
#include <fstream>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "bus_stop.h"
#include "text_utils.h"
#include "trie.h"

namespace unigo {

class BusDatabase {
public:
    // Expected columns: Place, Departure From, 1st Shift, 2nd Shift, 3rd Shift (first line = header).
    void loadCsv(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open " + path);
        std::string line;
        std::getline(f, line);                                         // skip the header row
        while (std::getline(f, line)) {
            if (trim(line).empty()) continue;
            std::vector<std::string> c = parseCsvLine(line);
            for (std::string& cell : c) cell = trim(cell);
            if (c.size() < 5 || c[0].empty() || c[1].empty()) { ++skipped_; continue; }   // incomplete row
            add(BusStop(c[0], c[1], {{c[2], c[3], c[4]}}));
        }
        if (stops_.empty()) throw std::runtime_error("No valid rows found in " + path);
    }

    void add(BusStop s) {
        int id = (int)stops_.size();
        std::string key = normalize(s.place());
        byName_[key].push_back(id);                 // index 1: hash table, exact name -> ids
        trie_.insert(key, id);                      // index 2: trie, prefix -> ids
        byDeparture_[s.departFrom()].push_back(id); // index 3: sorted map, departure point -> ids
        keys_.push_back(key);
        stops_.push_back(std::move(s));
    }

    // Returns EVERY matching stop, best match first, with no duplicates:
    //   1. exact name        (hash table, O(1) average)
    //   2. name starts with  (trie, O(query length + matches))
    //   3. name contains     (linear scan, O(n) -- "chowk" finds "Prince Chowk")
    std::vector<const BusStop*> search(const std::string& query) const {
        std::string q = normalize(query);
        std::vector<const BusStop*> out;
        if (q.empty()) return out;
        std::unordered_set<int> seen;               // a stop found by step 1 must not be added again by 2 or 3
        auto push = [&](int id) { if (seen.insert(id).second) out.push_back(&stops_[id]); };

        auto exact = byName_.find(q);
        if (exact != byName_.end()) for (int id : exact->second) push(id);
        for (int id : trie_.withPrefix(q)) push(id);
        for (int id = 0; id < (int)keys_.size(); ++id)
            if (keys_[id].find(q) != std::string::npos) push(id);
        return out;
    }

    // "Did you mean?" for typos: up to k place names closest to the query by edit distance.
    // A name is compared as a whole and word by word, so "clok" still finds "Clock Tower".
    // A max-heap of size k keeps only the k best candidates: O(n log k) instead of sorting all n.
    std::vector<std::string> didYouMean(const std::string& query, size_t k = 5) const {
        std::string q = normalize(query);
        std::vector<std::string> out;
        if (q.empty()) return out;
        const int limit = std::max(1, (int)q.size() / 3);    // allow roughly 1 typo per 3 letters
        std::priority_queue<std::pair<int, std::string>> heap;   // top() = worst of the current best k
        std::unordered_set<std::string> seen;
        for (size_t id = 0; id < keys_.size(); ++id) {
            int d = editDistance(q, keys_[id]);
            for (const std::string& w : splitWords(keys_[id])) d = std::min(d, editDistance(q, w));
            if (d > limit || !seen.insert(keys_[id]).second) continue;
            heap.emplace(d, stops_[id].place());
            if (heap.size() > k) heap.pop();                  // drop the worst one
        }
        for (; !heap.empty(); heap.pop()) out.push_back(heap.top().second);
        std::reverse(out.begin(), out.end());                 // closest first
        return out;
    }

    std::vector<std::string> departurePoints() const {        // already sorted: std::map is ordered
        std::vector<std::string> v;
        for (const auto& kv : byDeparture_) v.push_back(kv.first);
        return v;
    }

    std::vector<const BusStop*> stopsAt(const std::string& departure) const {
        std::vector<const BusStop*> out;
        auto it = byDeparture_.find(departure);
        if (it == byDeparture_.end()) return out;
        for (int id : it->second) out.push_back(&stops_[id]);
        return out;
    }

    size_t size() const { return stops_.size(); }
    int skippedRows() const { return skipped_; }

private:
    std::vector<BusStop> stops_;                                   // the single source of truth
    std::vector<std::string> keys_;                                // normalized names, same order as stops_
    std::unordered_map<std::string, std::vector<int>> byName_;     // vector: duplicate names are kept, not lost
    Trie trie_;
    std::map<std::string, std::vector<int>> byDeparture_;
    int skipped_ = 0;
};

} // namespace unigo
