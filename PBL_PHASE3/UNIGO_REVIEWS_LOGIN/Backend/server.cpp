// server.cpp -- UNIGO Reviews + Login, on its own. Wires the pieces together and starts the web server.
//
//   /api/auth/*             -> sign up, log in, log out, who am I
//   GET /api/reviews        -> summary + newest reviews + the choices
//   POST /api/reviews       -> post a review (JSON)
//   anything else           -> files from the frontend folder (index.html, css, js)
//
// Usage: backend/unigo_server.exe [port=8080] [frontendDir=frontend] [dataDir=data]
// Then open http://localhost:8080 in the browser.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include "auth_handlers.h"
#include "review_handlers.h"
#include "static_files.h"

using namespace unigo;

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    const std::string frontendDir = argc > 2 ? argv[2] : "frontend";
    const std::string dataDir = std::string(argc > 3 ? argv[3] : "data") + "/";
    if (port < 1 || port > 65535) { std::cerr << "Invalid port number.\n"; return 1; }
    if (!std::ifstream(frontendDir + "/index.html")) {
        std::cerr << "Cannot find " << frontendDir << "/index.html -- run this from the project folder.\n";
        return 1;
    }

    if (!makeDir(dataDir.substr(0, dataDir.size() - 1))) {   // data/ is created if it is missing
        std::cerr << "Cannot create the folder " << dataDir << "\n";
        return 1;
    }
    // Accounts: users.json (only salted password hashes), sessions.json (who is logged in),
    // config.json (optional settings such as the roll-number length). Created when first used.
    AuthStore accounts(dataDir + "users.json", dataDir + "config.json", dataDir + "sessions.json");
    accounts.load();
    // Reviews are saved one per line in data/reviews.json (created by the first review).
    ReviewStore reviews(dataDir + "reviews.json");
    reviews.refresh(true);

    NetworkSession net;
    if (!net.ok()) { std::cerr << "Could not start networking.\n"; return 1; }

    HttpServer server;
    auto h = [](RequestHandler* p) { return std::unique_ptr<RequestHandler>(p); };
    server.addRoute("/api/auth/signup", h(new SignupHandler(accounts)));
    server.addRoute("/api/auth/login", h(new LoginHandler(accounts)));
    server.addRoute("/api/auth/logout", h(new LogoutHandler(accounts)));
    server.addRoute("/api/auth/me", h(new MeHandler(accounts)));
    server.addRoute("/api/auth/name", h(new SetNameHandler(accounts)));
    server.addRoute("/api/reviews", h(new ReviewsHandler(reviews)));
    server.setFallback(h(new StaticFileHandler(frontendDir)));

    std::string error;
    if (!server.listen("127.0.0.1", port, error)) { std::cerr << "Could not start: " << error << "\n"; return 1; }
    std::cout << "UNIGO Reviews + Login running. Open http://localhost:" << port << " in your browser (Ctrl+C to stop)" << std::endl;
    server.run();
}
