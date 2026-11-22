// search_index.h -- the ONE search engine shared by every UNIGO feature
// (Bus Finder, Campus Map search and Classroom Finder).
//
// Each item (a bus stop, a building, a classroom) is added with:
//   * names    -- its name plus any aliases ("Main Admin Block", "B.Tech Block")
//   * keywords -- extra words that describe it ("computer", "lab", "printing")
// search() then returns every matching item, best first, with no duplicates.
#pragma once
#include <algorithm>
#include <cctype>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "text_utils.h"
#include "trie.h"

namespace unigo {

// "CR-123", "cr 123" and "CR123" all become "cr123": lets users ignore spaces, dots and hyphens.
inline std::string compactKey(const std::string& normalized) {
    std::string out;
    for (char c : normalized) if (c != ' ' && c != '-' && c != '.' && c != '_' && c != '/') out += c;
    return out;
}

// "Sound key": Dehradun place names are Hindi words written in English letters, and people spell them
// in many ways -- Garhi / Gadi, Chowk / Chauk / Chok, Nala Paani / Nalapani, Pul / Pool. Both the names and
// the query are turned into the same simplified spelling before comparing:
//   rh, dh -> d    bh, kh, gh, ph, th, sh, jh -> b, k, g, f, t, s, j    ch -> c    ce, ci -> s, si
//   ("Prince" is said "prins")    ck, q -> k    v -> w
//   ow, ou, au, aw -> o    aa -> a    ee -> i    oo -> u    "alk" -> "ok" ("chalk", a common way to type chowk)
//   doubled letters -> one,  spaces/dots/hyphens removed
// So "gadi chok" and "Garhi Chowk" both become "gadicok". It is only used AFTER the normal search,
// so exact spellings still come first.
inline std::string soundKey(const std::string& normalized) {
    std::string s;
    for (char c : normalized) if (std::isalnum((unsigned char)c)) s += c;   // letters and digits only
    static const std::pair<const char*, const char*> rules[] = {
        {"alk", "ok"}, {"rh", "d"}, {"dh", "d"}, {"bh", "b"}, {"kh", "k"}, {"gh", "g"}, {"ph", "f"}, {"th", "t"},
        {"sh", "s"}, {"jh", "j"}, {"ch", "c"}, {"ce", "s"}, {"ci", "si"}, {"ck", "k"}, {"q", "k"}, {"v", "w"},
        {"ow", "o"}, {"ou", "o"}, {"au", "o"}, {"aw", "o"}, {"aa", "a"}, {"ee", "i"}, {"oo", "u"},
    };
    for (const auto& r : rules) {
        std::string from = r.first, to = r.second, out;
        for (size_t i = 0; i < s.size();) {
            if (s.compare(i, from.size(), from) == 0) { out += to; i += from.size(); }
            else out += s[i++];
        }
        s = out;
    }
    std::string out;
    for (char c : s) if (out.empty() || out.back() != c) out += c;          // "ll" -> "l"
    return out;
}

class SearchIndex {
public:
    // `item` is the caller's own id for the thing (usually its position in the caller's vector).
    void add(int item, const std::vector<std::string>& names, const std::vector<std::string>& keywords = {}) {
        for (const std::string& raw : names) {
            std::string key = normalize(raw);
            if (key.empty()) continue;
            int k = (int)nameKeys_.size();
            nameKeys_.push_back(key);
            compactKeys_.push_back(compactKey(key));
            soundKeys_.push_back(soundKey(key));
            std::vector<std::string> ws;
            for (const std::string& w : splitWords(key)) ws.push_back(soundKey(w));
            soundWords_.push_back(ws);
            nameOwner_.push_back(item);
            exact_[key].push_back(k);                        // hash table: exact name -> name keys
            exactCompact_[compactKeys_.back()].push_back(k); // hash table: "cr123" -> name keys
            trie_.insert(key, k);                            // trie: "pri" -> names starting with it
            compactTrie_.insert(compactKeys_.back(), k);     // trie: "cr1" -> "cr 123"
        }
        for (const std::string& raw : keywords) {
            std::string key = normalize(raw);
            if (key.empty()) continue;
            keywordKeys_.push_back(key);
            keywordOwner_.push_back(item);
        }
    }

    // Every matching item, best match first, no duplicates:
    //   1. exact name or alias          (hash tables, O(1) average)
    //   2. name starts with the query   (tries, O(query length + matches))
    //   3. name contains the query      (linear scan -- "chowk" finds "Prince Chowk")
    //   4. a keyword contains the query ("printing" finds the room whose purpose mentions it)
    // Steps 1-3 also try the "compact" form, so "cr123" matches "CR 123".
    std::vector<int> search(const std::string& query) const {
        std::string q = normalize(query), qc = compactKey(q);
        std::vector<int> out;
        if (q.empty()) return out;
        std::unordered_set<int> seen;                        // an item found by step 1 is not added again
        auto pushName = [&](int k) { int item = nameOwner_[k]; if (seen.insert(item).second) out.push_back(item); };

        auto e = exact_.find(q);
        if (e != exact_.end()) for (int k : e->second) pushName(k);
        if (!qc.empty()) {
            auto ec = exactCompact_.find(qc);
            if (ec != exactCompact_.end()) for (int k : ec->second) pushName(k);
        }
        for (int k : trie_.withPrefix(q)) pushName(k);
        if (!qc.empty()) for (int k : compactTrie_.withPrefix(qc)) pushName(k);
        for (int k = 0; k < (int)nameKeys_.size(); ++k)
            if (nameKeys_[k].find(q) != std::string::npos ||
                (!qc.empty() && compactKeys_[k].find(qc) != std::string::npos)) pushName(k);
        for (int k = 0; k < (int)keywordKeys_.size(); ++k)
            if (keywordKeys_[k].find(q) != std::string::npos) {
                int item = keywordOwner_[k];
                if (seen.insert(item).second) out.push_back(item);
            }
        // 5. the same SOUND, another spelling ("gadi" finds "Garhi Cantt", "chauk" finds every Chowk)
        std::string qs = soundKey(q);
        if (qs.size() >= 3)
            for (int k = 0; k < (int)soundKeys_.size(); ++k)
                if (soundKeys_[k].find(qs) != std::string::npos) pushName(k);
        return out;
    }

    // "Did you mean?": up to k items whose name is closest to the query. Each candidate gets a score:
    //   * spelling: the name, compared whole and word by word, by edit distance ("clok" -> "Clock Tower"),
    //     about 1 typo per 3 letters; a closer name scores more (0 typos = 1 point, at the limit = a little);
    //   * sound: the same comparison on sound keys ("gadi" -> "Garhi Cantt"); short words must sound
    //     exactly the same, so "chok" never matches "clok";
    //   * words (queries of 2+ words): each query word that sounds like a word of the name adds
    //     1 / (how many items have that word). A rare word counts more: in "gadi chowk", "gadi" fits 1 stop
    //     (+1) but "chowk" fits 17 (+1/17 each), so Garhi Cantt comes before Kargi Chowk.
    // Items scoring 0 are not suggested (no wild guesses). Sorting n candidates: O(n log n).
    std::vector<int> didYouMean(const std::string& query, size_t k = 5) const {
        std::string q = normalize(query), qc = compactKey(q), qs = soundKey(q);
        std::vector<int> out;
        if (q.empty()) return out;
        if (qs.size() < 3) qs.clear();                       // "zzzzzz" -> "z": too short to compare by sound
        auto wordLimit = [](const std::string& w) { return w.size() <= 4 ? 0 : (int)w.size() / 3; };
        const int limit = std::max(1, (int)qc.size() / 3);   // spelling: letters only, spaces don't earn typos
        const int soundLimit = std::max(1, (int)qs.size() / 5);   // sound keys are shorter, so stricter
                                                                  // ("princecok" must not reach "princeton")
        std::vector<std::string> qWords;                     // sound keys of the query's words (3+ letters)
        for (const std::string& w : splitWords(q)) { std::string s = soundKey(w); if (s.size() >= 3) qWords.push_back(s); }
        std::vector<std::unordered_set<int>> wordHits(qWords.size());   // query word -> items that have it
        std::unordered_map<int, double> closeness;           // item -> best spelling/sound closeness (0..1]
        for (size_t i = 0; i < nameKeys_.size(); ++i) {
            double c = 0;
            int d = std::min(editDistance(q, nameKeys_[i]), editDistance(qc, compactKeys_[i]));
            for (const std::string& w : splitWords(nameKeys_[i])) d = std::min(d, editDistance(q, w));
            if (d <= limit) c = (double)(limit + 1 - d) / (limit + 1);
            if (!qs.empty()) {
                int s = editDistance(qs, soundKeys_[i]);
                for (const std::string& w : soundWords_[i]) { int e = editDistance(qs, w); if (e <= wordLimit(qs)) s = std::min(s, e); }
                if (s <= soundLimit) c = std::max(c, (double)(soundLimit + 1 - s) / (soundLimit + 1));
            }
            if (c > 0) { double& b = closeness[nameOwner_[i]]; b = std::max(b, c); }
            if (qWords.size() > 1)
                for (size_t j = 0; j < qWords.size(); ++j)
                    for (const std::string& w : soundWords_[i])
                        if (editDistance(qWords[j], w) <= wordLimit(qWords[j])) { wordHits[j].insert(nameOwner_[i]); break; }
        }
        std::unordered_map<int, double> score(closeness.begin(), closeness.end());
        for (const auto& hits : wordHits) for (int item : hits) score[item] += 1.0 / hits.size();
        std::vector<std::pair<double, int>> ranked;          // (-score, item): sort puts the best first
        for (const auto& kv : score) ranked.emplace_back(-kv.second, kv.first);
        std::sort(ranked.begin(), ranked.end());             // ties: lower item id (file order) first
        for (size_t i = 0; i < ranked.size() && out.size() < k; ++i) out.push_back(ranked[i].second);
        return out;
    }

    // True when the query equals one of this item's names (used to label "Exact match").
    bool isExactName(int item, const std::string& query) const {
        std::string q = normalize(query), qc = compactKey(q);
        for (size_t i = 0; i < nameKeys_.size(); ++i)
            if (nameOwner_[i] == item && (nameKeys_[i] == q || (!qc.empty() && compactKeys_[i] == qc))) return true;
        return false;
    }

private:
    std::vector<std::string> nameKeys_, compactKeys_, keywordKeys_, soundKeys_;
    std::vector<std::vector<std::string>> soundWords_;      // each name's words, as sound keys
    std::vector<int> nameOwner_, keywordOwner_;              // which item each key belongs to
    std::unordered_map<std::string, std::vector<int>> exact_, exactCompact_;
    Trie trie_, compactTrie_;
};

} // namespace unigo
