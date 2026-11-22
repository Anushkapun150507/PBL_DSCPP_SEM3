// campus_store.h -- keeps the CampusMap and RoomDatabase in sync with their JSON files.
// Like DataStore for the bus CSV: a file is reloaded only when it changes, the new data is fully
// built before it replaces the old, and a broken edit never wipes out data already being served.
#pragma once
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "campus_map.h"
#include "json_reader.h"
#include "room_database.h"

namespace unigo {

class CampusStore {
public:
    // roomsPaths: candidate classroom files, first existing one wins (e.g. classrooms.csv, classrooms.json).
    CampusStore(std::string mapPath, std::vector<std::string> roomsPaths) : mapPath_(std::move(mapPath)), roomsPaths_(std::move(roomsPaths)) {}

    // Returns false if the map itself could not be loaded (the server can't start without it).
    bool refresh(bool force) {
        Stamp m = stampOf(mapPath_);
        bool mapChanged = force || (!m.missing && !(m == mapStamp_));
        if (mapChanged) {
            try {
                std::ifstream f(mapPath_, std::ios::binary);
                std::stringstream ss;
                ss << f.rdbuf();
                CampusMap fresh;                                  // build the new copy first ...
                fresh.load(JsonParser::parseFile(mapPath_));
                map_ = std::move(fresh);                          // ... then swap it in
                mapText_ = ss.str();
                mapLoaded_ = true;
                std::cout << "Loaded campus map: " << map_.places().size() << " places, "
                          << map_.nodeCount() << " graph nodes" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error loading campus map: " << e.what() << std::endl;
                if (!mapLoaded_) { mapStamp_ = m; return false; }
            }
            mapStamp_ = m;
        }
        std::string path = roomsPaths_.empty() ? std::string() : roomsPaths_.front();
        for (const std::string& p : roomsPaths_) if (!stampOf(p).missing) { path = p; break; }
        Stamp r = stampOf(path);
        if (mapChanged || path != roomsPath_ || !(r == roomsStamp_)) { roomsPath_ = path; loadRooms(r); }   // links depend on the map
        return mapLoaded_;
    }

    const CampusMap& map() { refresh(false); return map_; }
    const RoomDatabase& rooms() { refresh(false); return rooms_; }
    const std::string& mapText() { refresh(false); return mapText_; }
    bool roomsLoaded() const { return roomsLoaded_; }
    const std::string& roomsStatus() const { return roomsStatus_; }

private:
    struct Stamp {
        long long mtime = 0, size = -1;
        bool missing = false;
        bool operator==(const Stamp& o) const { return mtime == o.mtime && size == o.size && missing == o.missing; }
    };
    static Stamp stampOf(const std::string& path) {
        Stamp s;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) { s.missing = true; return s; }
        s.mtime = (long long)st.st_mtime;
        s.size = (long long)st.st_size;
        return s;
    }

    void loadRooms(const Stamp& r) {
        roomsStamp_ = r;
        if (r.missing) {
            rooms_ = RoomDatabase();
            roomsLoaded_ = false;
            roomsStatus_ = "Classroom data has not been added yet (no classrooms.csv or classrooms.json in the data folder).";
            std::cout << roomsStatus_ << std::endl;
            return;
        }
        try {
            RoomDatabase fresh;
            bool csv = roomsPath_.size() >= 4 && normalize(roomsPath_.substr(roomsPath_.size() - 4)) == ".csv";
            if (csv) fresh.loadCsv(roomsPath_, map_);
            else fresh.loadJson(JsonParser::parseFile(roomsPath_), map_);
            rooms_ = std::move(fresh);
            roomsLoaded_ = true;
            roomsStatus_ = "Loaded " + std::to_string(rooms_.rooms().size()) + " rooms";
            if (rooms_.skippedEntries()) roomsStatus_ += " (skipped " + std::to_string(rooms_.skippedEntries()) + " incomplete entries)";
            std::cout << roomsStatus_ << " from " << roomsPath_ << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error loading classrooms: " << e.what()
                      << (roomsLoaded_ ? " -- keeping the previous data" : "") << std::endl;
            if (!roomsLoaded_) roomsStatus_ = std::string("Classroom data could not be read: ") + e.what();
        }
    }

    std::string mapPath_, roomsPath_;
    std::vector<std::string> roomsPaths_;
    CampusMap map_;
    RoomDatabase rooms_;
    std::string mapText_, roomsStatus_ = "Classroom data has not been loaded.";
    bool mapLoaded_ = false, roomsLoaded_ = false;
    Stamp mapStamp_, roomsStamp_;
};

} // namespace unigo
