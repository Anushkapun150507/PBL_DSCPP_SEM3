// http.h -- a minimal HTTP/1.1 server: just enough for a local website (GET requests only).
#pragma once
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include "json_writer.h"

#ifdef _WIN32
  #include <winsock2.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
  #define BAD_SOCKET INVALID_SOCKET
  #ifndef SO_EXCLUSIVEADDRUSE
    #define SO_EXCLUSIVEADDRUSE ((int)(~SO_REUSEADDR))
  #endif
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef int sock_t;
  #define CLOSESOCK close
  #define BAD_SOCKET (-1)
#endif

namespace unigo {

// ---------- request / response ----------
inline std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2])) {
            o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}

struct HttpRequest {
    std::string method, path;
    std::unordered_map<std::string, std::string> query;     // ?q=isbt -> {"q": "isbt"}

    std::string param(const std::string& name) const {
        auto it = query.find(name);
        return it == query.end() ? "" : it->second;
    }

    // Parses the first line, e.g. "GET /api/search?q=isbt HTTP/1.1". Returns false if malformed.
    static bool parse(const std::string& raw, HttpRequest& out) {
        std::istringstream line(raw.substr(0, raw.find("\r\n")));
        std::string target;
        if (!(line >> out.method >> target) || target.empty() || target[0] != '/') return false;
        size_t qm = target.find('?');
        out.path = target.substr(0, qm);
        if (qm == std::string::npos) return true;
        std::istringstream qs(target.substr(qm + 1));
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            size_t eq = pair.find('=');
            std::string k = urlDecode(pair.substr(0, eq));
            if (!out.query.count(k)) out.query[k] = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
        }
        return true;
    }
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;

    static HttpResponse json(const std::string& body, int status = 200) {
        HttpResponse r;
        r.status = status;
        r.contentType = "application/json; charset=utf-8";
        r.body = body;
        return r;
    }
    static HttpResponse error(int status, const std::string& code, const std::string& message) {
        JsonWriter j;
        j.beginObject().key("error").str(code).key("message").str(message).endObject();
        return json(j.text(), status);
    }

    std::string serialize() const {
        std::ostringstream out;
        out << "HTTP/1.1 " << status << " " << reason(status) << "\r\nContent-Type: " << contentType
            << "\r\nContent-Length: " << body.size() << "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n" << body;
        return out.str();
    }

private:
    static const char* reason(int s) {
        switch (s) {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            default:  return "Internal Server Error";
        }
    }
};

// ---------- abstraction: every URL is served by some kind of RequestHandler ----------
class RequestHandler {
public:
    virtual ~RequestHandler() = default;
    virtual HttpResponse handle(const HttpRequest& req) = 0;   // pure virtual: subclasses must implement it
};

// ---------- RAII wrappers: resources are released in destructors, even on early return ----------
class Socket {
public:
    Socket() = default;
    explicit Socket(sock_t s) : s_(s) {}
    ~Socket() { close(); }
    Socket(const Socket&) = delete;                 // a socket has exactly one owner ...
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& o) noexcept : s_(o.s_) { o.s_ = BAD_SOCKET; }   // ... but ownership can move
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) { close(); s_ = o.s_; o.s_ = BAD_SOCKET; }
        return *this;
    }
    bool valid() const { return s_ != BAD_SOCKET; }
    sock_t get() const { return s_; }
    void close() { if (valid()) { CLOSESOCK(s_); s_ = BAD_SOCKET; } }

private:
    sock_t s_ = BAD_SOCKET;
};

class NetworkSession {               // Windows needs WSAStartup/WSACleanup around all socket use
public:
#ifdef _WIN32
    NetworkSession() { WSADATA w; ok_ = WSAStartup(MAKEWORD(2, 2), &w) == 0; }
    ~NetworkSession() { if (ok_) WSACleanup(); }
#else
    NetworkSession() : ok_(true) {}
#endif
    NetworkSession(const NetworkSession&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

// ---------- the server ----------
class HttpServer {
public:
    // Exact-path routes, e.g. "/api/search". Stored in a hash table: O(1) lookup per request.
    void addRoute(const std::string& path, std::unique_ptr<RequestHandler> handler) {
        routes_[path] = std::move(handler);
    }
    // Used for any non-API path (the website files).
    void setFallback(std::unique_ptr<RequestHandler> handler) { fallback_ = std::move(handler); }

    bool listen(const std::string& host, int port, std::string& error) {
        listener_ = Socket(socket(AF_INET, SOCK_STREAM, 0));
        if (!listener_.valid()) { error = "could not create a socket"; return false; }
        int yes = 1;
#ifdef _WIN32
        // On Windows SO_REUSEADDR would let a second server silently share the port; forbid that.
        setsockopt(listener_.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&yes, sizeof yes);
#else
        setsockopt(listener_.get(), SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
#endif
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)port);
        addr.sin_addr.s_addr = inet_addr(host.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE) { error = "invalid host address " + host; return false; }
        if (bind(listener_.get(), (sockaddr*)&addr, sizeof addr) != 0 || ::listen(listener_.get(), 16) != 0) {
            error = "port " + std::to_string(port) + " is busy -- is UNIGO already running?";
            return false;
        }
        return true;
    }

    // Single-threaded loop: requests are handled one at a time, which is plenty for a local demo.
    void run() {
        while (true) {
            Socket client(accept(listener_.get(), nullptr, nullptr));
            if (client.valid()) serve(client);
        }   // `client` is closed automatically here (RAII)
    }

    // Public so tests can call it without a network connection.
    HttpResponse dispatch(const HttpRequest& req) {
        if (req.method != "GET") return HttpResponse::error(405, "method_not_allowed", "Only GET is supported.");
        auto it = routes_.find(req.path);
        if (it != routes_.end()) return it->second->handle(req);          // polymorphic call
        if (req.path.rfind("/api/", 0) == 0 || !fallback_)
            return HttpResponse::error(404, "not_found", "Unknown API endpoint.");
        return fallback_->handle(req);
    }

private:
    void serve(Socket& client) {
        // Browsers open idle "preconnect" sockets that never send a request. Because this server is
        // single-threaded, one of those would block every other visitor, so give up after 2 seconds.
#ifdef _WIN32
        DWORD timeout = 2000;
#else
        timeval timeout; timeout.tv_sec = 2; timeout.tv_usec = 0;
#endif
        setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);

        std::string raw;
        char buf[4096];
        while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 16384) {
            int n = (int)recv(client.get(), buf, sizeof buf, 0);
            if (n <= 0) break;
            raw.append(buf, (size_t)n);
        }
        if (raw.empty()) return;                                          // idle connection: nothing to answer
        HttpRequest req;
        HttpResponse res = HttpRequest::parse(raw, req)
                               ? dispatch(req)
                               : HttpResponse::error(400, "bad_request", "Malformed request.");
        std::string data = res.serialize();
        for (size_t sent = 0; sent < data.size();) {
            int n = (int)send(client.get(), data.data() + sent, (int)(data.size() - sent), 0);
            if (n <= 0) break;
            sent += (size_t)n;
        }
    }

    std::unordered_map<std::string, std::unique_ptr<RequestHandler>> routes_;
    std::unique_ptr<RequestHandler> fallback_;
    Socket listener_;
};

} // namespace unigo
