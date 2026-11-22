// server.cpp -- UNIGO Campus Map, on its own. Wires the pieces together and starts the web server.
//
//   GET /api/map            -> the whole map for drawing
//   GET /api/places/search  -> buildings and places (also /api/places/suggest)
//   GET /api/route?from=&to= -> up to 4 routes, shortest first
//   GET /api/rooms/search   -> classrooms and labs (also suggest, in, meta)
//   anything else           -> files from the frontend folder (index.html, css, js)
//
// Usage: backend/unigo_server.exe [port=8080] [frontendDir=frontend] [dataDir=data]
// Then open http://localhost:8080 in the browser.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include "campus_handlers.h"
#include "campus_store.h"
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

    // The campus map (buildings, roads, gates) and the classroom list, reloaded when the files change.
    CampusStore campus(dataDir + "campus_map.json", {dataDir + "classrooms.csv", dataDir + "classrooms.json"});
    if (!campus.refresh(true)) return 1;

    NetworkSession net;
    if (!net.ok()) { std::cerr << "Could not start networking.\n"; return 1; }

    HttpServer server;
    auto h = [](RequestHandler* p) { return std::unique_ptr<RequestHandler>(p); };
    server.addRoute("/api/map", h(new MapDataHandler(campus)));
    server.addRoute("/api/places/search", h(new PlaceSearchHandler(campus)));
    server.addRoute("/api/places/suggest", h(new PlaceSuggestHandler(campus)));
    server.addRoute("/api/route", h(new RouteHandler(campus)));
    server.addRoute("/api/rooms/search", h(new RoomSearchHandler(campus)));
    server.addRoute("/api/rooms/suggest", h(new RoomSuggestHandler(campus)));
    server.addRoute("/api/rooms/in", h(new RoomsInPlaceHandler(campus)));
    server.addRoute("/api/rooms/meta", h(new RoomsMetaHandler(campus)));
    server.setFallback(h(new StaticFileHandler(frontendDir)));

    std::string error;
    if (!server.listen("127.0.0.1", port, error)) { std::cerr << "Could not start: " << error << "\n"; return 1; }
    std::cout << "UNIGO Campus Map running. Open http://localhost:" << port << " in your browser (Ctrl+C to stop)" << std::endl;
    server.run();
}
