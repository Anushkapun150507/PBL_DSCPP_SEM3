// room_database.h -- classrooms, labs, lecture theatres and facilities inside buildings.
// Loaded from data/classrooms.csv (or classrooms.json); searched with the shared SearchIndex;
// each room's building is linked to its building on the campus map so the map can route to it.
#pragma once
#include <fstream>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "campus_map.h"
#include "json_reader.h"
#include "search_index.h"
#include "text_utils.h"

namespace unigo {

struct Room {
    std::string id, name, building, floor, category, purpose, status;
    std::vector<std::string> keywords;
    bool verified = false;     // only true when the data explicitly says "verified"
    int place = -1;            // index of the building on the campus map, -1 if not on the map
};

class RoomDatabase {
public:
    // One record = column/field name -> text. Both the CSV and the JSON loader produce records.
    typedef std::map<std::string, std::string> Record;

    // CSV with a header row, e.g.: id,building,floor,room_or_facility,category,purpose,search_keywords,data_status
    void loadCsv(const std::string& path, const CampusMap& map) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open " + path);
        std::string line;
        if (!std::getline(f, line)) throw std::runtime_error(path + " is empty");
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line.erase(0, 3);   // UTF-8 BOM from Excel
        std::vector<std::string> header = parseCsvLine(line);
        for (std::string& h : header) h = normalize(h);
        while (std::getline(f, line)) {
            if (trim(line).empty()) continue;
            std::vector<std::string> cells = parseCsvLine(line);
            Record rec;
            for (size_t i = 0; i < header.size() && i < cells.size(); ++i) rec[header[i]] = trim(cells[i]);
            addRecord(rec, map);
        }
    }

    // JSON: a list of objects, or an object holding that list (e.g. {"rooms": [...]}).
    void loadJson(const JsonValue& root, const CampusMap& map) {
        const JsonValue* list = &root;
        if (root.isObject()) {
            list = nullptr;
            for (const auto& kv : root.members())
                if (kv.second.isArray()) { list = &kv.second; break; }
        }
        if (!list || !list->isArray()) throw std::runtime_error("expected a list of rooms");
        for (const JsonValue& e : list->items()) {
            if (!e.isObject()) { ++skipped_; continue; }
            Record rec;
            for (const auto& kv : e.members()) {
                std::string text;
                if (kv.second.isArray()) {                      // keyword lists become "a, b, c"
                    for (const JsonValue& s : kv.second.items()) text += (text.empty() ? "" : ", ") + s.toText();
                } else text = kv.second.toText();
                rec[normalize(kv.first)] = trim(text);
            }
            addRecord(rec, map);
        }
    }

    const std::vector<Room>& rooms() const { return rooms_; }
    std::vector<int> search(const std::string& q) const { return index_.search(q); }
    std::vector<int> didYouMean(const std::string& q, size_t k = 5) const { return index_.didYouMean(q, k); }
    bool isExactName(int room, const std::string& q) const { return index_.isExactName(room, q); }
    std::vector<int> roomsInPlace(int place) const {
        auto it = byPlace_.find(place);
        return it == byPlace_.end() ? std::vector<int>() : it->second;
    }
    int skippedEntries() const { return skipped_; }

    // Only an explicit "verified" counts; "User-provided", "Tentative - verify", "not verified" do not.
    static bool isVerifiedText(const std::string& raw) {
        std::string s = normalize(raw);
        if (s.find("not") != std::string::npos || s.find("unverified") != std::string::npos) return false;
        return s == "verified" || s == "true" || s == "yes" || s == "confirmed" || s == "official" ||
               s == "officially verified";
    }

private:
    // First non-empty field among alternative column names (different files spell them differently).
    static std::string field(const Record& r, std::initializer_list<const char*> keys) {
        for (const char* k : keys) {
            auto it = r.find(normalize(k));
            if (it != r.end() && !it->second.empty()) return it->second;
        }
        return "";
    }

    // "CR 1, CR 2, CR 3" lists several rooms in one row: split it so "CR 1" finds exactly CR 1.
    // Only split when every part is short and contains a room number.
    static std::vector<std::string> splitRange(const std::string& name) {
        std::vector<std::string> parts;
        if (name.find(',') == std::string::npos) return {name};
        for (std::string p : parseCsvLine(name)) {
            p = trim(p);
            bool hasDigit = false;
            for (char c : p) if (std::isdigit((unsigned char)c)) hasDigit = true;
            if (p.empty() || p.size() > 20 || !hasDigit) return {name};
            parts.push_back(p);
        }
        return parts;
    }

    void addRecord(const Record& rec, const CampusMap& map) {
        Room base;
        base.id = field(rec, {"id"});
        base.name = field(rec, {"room_or_facility", "name", "room", "room_name", "roomname", "classroom", "facility", "facility_name", "title"});
        base.building = field(rec, {"building", "building_name", "buildingname", "block", "block_name"});
        if (base.name.empty() || base.building.empty()) { ++skipped_; return; }      // incomplete entry
        base.floor = field(rec, {"floor", "floor_number", "floornumber", "level"});
        base.category = field(rec, {"category", "type", "kind"});
        base.purpose = field(rec, {"purpose", "description", "use", "details"});
        base.status = field(rec, {"data_status", "verification_status", "verificationstatus", "verification", "status", "verified"});
        if (base.status == "true") base.status = "Verified";
        if (base.status == "false") base.status = "Not verified";
        base.verified = isVerifiedText(base.status);
        base.place = map.placeByName(base.building);
        std::vector<std::string> keywords;
        for (const std::string& k : parseCsvLine(field(rec, {"search_keywords", "keywords", "searchkeywords", "tags"})))
            if (!trim(k).empty()) keywords.push_back(trim(k));

        std::vector<std::string> names = splitRange(base.name);
        // A note like "Rooms in this numbered range; select the exact room number..." describes the
        // whole row, not one room, so it is not repeated on each room after splitting.
        if (names.size() > 1 && normalize(base.purpose).find("range") != std::string::npos) base.purpose.clear();
        for (size_t i = 0; i < names.size(); ++i) {
            Room r = base;
            r.name = names[i];
            if (names.size() > 1) r.id = base.id + "-" + std::to_string(i + 1);
            for (const std::string& k : keywords) {                // drop the OTHER room numbers of the range
                bool otherRoom = false;
                for (size_t j = 0; j < names.size(); ++j)
                    if (j != i && compactKey(normalize(k)) == compactKey(normalize(names[j]))) otherRoom = true;
                if (!otherRoom) r.keywords.push_back(k);
            }
            add(std::move(r), map);
        }
    }

    void add(Room r, const CampusMap& map) {
        int idx = (int)rooms_.size();
        // Names: the room on its own, and "room + building" so "CR 1 CSIT" also matches.
        std::vector<std::string> names{r.name, r.name + " " + r.building};
        // Keywords: its own keywords plus building, category and purpose words.
        std::vector<std::string> words = r.keywords;
        words.push_back(r.building);
        if (r.place >= 0) {
            const MapPlace& pl = map.places()[r.place];
            words.push_back(pl.name);
            words.insert(words.end(), pl.aliases.begin(), pl.aliases.end());
        }
        if (!r.category.empty()) words.push_back(r.category);
        if (!r.purpose.empty()) words.push_back(r.purpose);
        index_.add(idx, names, words);
        if (r.place >= 0) byPlace_[r.place].push_back(idx);
        rooms_.push_back(std::move(r));
    }

    std::vector<Room> rooms_;
    SearchIndex index_;
    std::unordered_map<int, std::vector<int>> byPlace_;   // map place -> its rooms
    int skipped_ = 0;
};

} // namespace unigo
