// file_utils.h -- small file helpers shared by the stores that WRITE data (Reviews, Lost & Found).
//   * FileStamp        -- "has this file changed?" (modification time + size), to reload after hand edits
//   * writeFileAtomic  -- writes a temporary file first and then swaps it in, so a crash while saving
//                         never leaves a half-written file
//   * makeDir / readWholeFile / removeFile
#pragma once
#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  #include <direct.h>
#endif
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace unigo {

struct FileStamp {
    long long mtime = 0, size = -1;
    bool missing = false;
    bool operator==(const FileStamp& o) const { return mtime == o.mtime && size == o.size && missing == o.missing; }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
    static FileStamp of(const std::string& path) {
        FileStamp s;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) { s.missing = true; return s; }
        s.mtime = (long long)st.st_mtime;
        s.size = (long long)st.st_size;
        return s;
    }
    static FileStamp never() { FileStamp s; s.size = -2; return s; }   // differs from every real stamp
};

inline bool writeFileAtomic(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { std::cerr << "Cannot write " << tmp << std::endl; return false; }
        f << content;
        if (!f.good()) { std::cerr << "Error writing " << tmp << std::endl; return false; }
    }
#ifdef _WIN32
    // OneDrive or an antivirus can lock a file for a moment right after it changes, so try a few times.
    bool ok = false;
    for (int attempt = 0; attempt < 10 && !ok; ++attempt) {
        ok = MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok) Sleep(50);
    }
#else
    bool ok = std::rename(tmp.c_str(), path.c_str()) == 0;
#endif
    if (!ok) { std::cerr << "Cannot replace " << path << std::endl; std::remove(tmp.c_str()); }
    return ok;
}

inline bool readWholeFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

inline bool makeDir(const std::string& path) {        // true if it exists afterwards
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
}

inline void removeFile(const std::string& path) { std::remove(path.c_str()); }

} // namespace unigo
