// server.cpp -- UNIGO backend entry point. Wires the pieces together and starts the server.
//
//   GET /api/search?q=<text>   -> all matching stops (+ "did you mean" names when nothing matches)
//   GET /api/suggest?q=<text>  -> up to 8 place names for autocomplete
//   GET /api/meta              -> number of places loaded + departure points
//   anything else              -> static file from the frontend folder
//
// Usage: unigo_server [csv=data/stops.csv] [port=8080] [frontendDir=frontend] [host=127.0.0.1]
// Use host 0.0.0.0 to let phones on the same Wi-Fi open the site.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include "data_store.h"
#include "handlers.h"
#include "http.h"

using namespace unigo;

int main(int argc, char** argv) {
    const std::string csvPath = argc > 1 ? argv[1] : "data/stops.csv";
    const int port = argc > 2 ? std::atoi(argv[2]) : 8080;
    const std::string frontendDir = argc > 3 ? argv[3] : "frontend";
    const std::string host = argc > 4 ? argv[4] : "127.0.0.1";

    if (port < 1 || port > 65535) { std::cerr << "Invalid port number.\n"; return 1; }
    if (!std::ifstream(frontendDir + "/index.html")) {
        std::cerr << "Cannot find " << frontendDir << "/index.html -- run this from the project folder.\n";
        return 1;
    }

    DataStore store(csvPath);
    if (!store.reload(true)) return 1;

    NetworkSession net;
    if (!net.ok()) { std::cerr << "Could not start networking.\n"; return 1; }

    HttpServer server;
    server.addRoute("/api/search", std::unique_ptr<RequestHandler>(new SearchHandler(store)));
    server.addRoute("/api/suggest", std::unique_ptr<RequestHandler>(new SuggestHandler(store)));
    server.addRoute("/api/meta", std::unique_ptr<RequestHandler>(new MetaHandler(store)));
    server.setFallback(std::unique_ptr<RequestHandler>(new StaticFileHandler(frontendDir)));

    std::string error;
    if (!server.listen(host, port, error)) { std::cerr << "Could not start: " << error << "\n"; return 1; }
    std::cout << "UNIGO server running. Open http://localhost:" << port << " in your browser (Ctrl+C to stop)" << std::endl;
    server.run();
}
