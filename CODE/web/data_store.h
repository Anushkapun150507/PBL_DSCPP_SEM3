// data_store.h -- keeps the BusDatabase in sync with the CSV file on disk.
#pragma once
#include <iostream>
#include <string>
#include <sys/stat.h>
#include "bus_database.h"

namespace unigo {

// Edit data/stops.csv while the server runs and the next request sees the new data.
// The file is reloaded only when its modification time or size changes, and a broken edit
// never replaces the data that is already being served.
class DataStore {
public:
    explicit DataStore(std::string csvPath) : path_(std::move(csvPath)) {}

    // Returns false if the file could not be loaded (the previous data, if any, stays in use).
    bool reload(bool force) {
        Stamp now = stampOf(path_);
        if (!force && (now.missing || now == stamp_)) return true;
        try {
            BusDatabase fresh;                    // build the new copy first ...
            fresh.loadCsv(path_);
            db_ = std::move(fresh);               // ... then swap it in
            stamp_ = now;
            std::cout << "Loaded " << db_.size() << " places from " << path_;
            if (db_.skippedRows()) std::cout << " (skipped " << db_.skippedRows() << " incomplete rows)";
            std::cout << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error loading data: " << e.what() << (force ? "" : " -- keeping the previous data") << std::endl;
            stamp_ = now;                         // don't retry the same broken file on every request
            return false;
        }
    }

    const BusDatabase& db() { reload(false); return db_; }

private:
    struct Stamp {
        long long mtime = 0, size = -1;
        bool missing = false;
        bool operator==(const Stamp& o) const { return mtime == o.mtime && size == o.size; }
    };
    static Stamp stampOf(const std::string& path) {
        Stamp s;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) { s.missing = true; return s; }
        s.mtime = (long long)st.st_mtime;
        s.size = (long long)st.st_size;
        return s;
    }

    std::string path_;
    BusDatabase db_;
    Stamp stamp_;
};

} // namespace unigo
