// static_files.h -- serves the website's own files (index.html, css/style.css, js/*.js) from the frontend folder.
// Every UNIGO server uses it for any address that is not an /api/... address.
#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "http.h"

namespace unigo {

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
