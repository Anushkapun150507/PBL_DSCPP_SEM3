// test_core.cpp -- automated checks for the backend (no network needed).
// Build & run from the project folder:
//   g++ -std=c++14 -Wall -o backend/test_core.exe backend/test_core.cpp -lws2_32 && backend\test_core.exe
#include <iostream>
#include <memory>
#include "bus_database.h"
#include "handlers.h"
#include "http.h"
#include "json_writer.h"
using namespace unigo;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } } while (0)

static bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

int main() {
    // --- time parsing ---
    CHECK(toMinutes("1:10 PM") == 13 * 60 + 10);
    CHECK(toMinutes(" 1:10pm ") == 13 * 60 + 10);
    CHECK(toMinutes("12:05 PM") == 12 * 60 + 5);
    CHECK(toMinutes("12:05 AM") == 5);
    CHECK(toMinutes("13:10") == 13 * 60 + 10);          // 24-hour
    CHECK(toMinutes("13:10:00") == 13 * 60 + 10);       // Excel time export
    CHECK(toMinutes("No Service") == -1);
    CHECK(toMinutes("") == -1);
    CHECK(toMinutes("13:10 PM") == -1);                 // invalid 12-hour value
    CHECK(toMinutes("1:75 PM") == -1);

    // --- CSV parsing ---
    std::vector<std::string> c = parseCsvLine("\"Saharanpur Chowk, Dehradun\",Gate No. 2,1:10 PM,,\"say \"\"hi\"\"\"");
    CHECK(c.size() == 5);
    CHECK(c[0] == "Saharanpur Chowk, Dehradun");
    CHECK(c[3].empty());
    CHECK(c[4] == "say \"hi\"");

    // --- edit distance ---
    CHECK(editDistance("clok", "clock") == 1);
    CHECK(editDistance("isbt", "isbt") == 0);
    CHECK(editDistance("", "abc") == 3);
    CHECK(editDistance("kitten", "sitting") == 3);

    // --- search on a small hand-made database ---
    BusDatabase db;
    db.add(BusStop("ISBT", "Parking Near Hill (Old)", {{"1:10 PM", "4:10 PM", "6:10 PM"}}));
    db.add(BusStop("Prince Chowk", "Parking Near Hill (Old)", {{"1:10 PM", "4:10 PM", "6:10 PM"}}));
    db.add(BusStop("Clock Tower", "Gate No. 2", {{"1:10 PM", "4:10 PM", "6:10 PM"}}));
    db.add(BusStop("Prince Chowk", "Gate No. 2", {{"No Service", "4:10 PM", "No Service"}}));   // duplicate name
    db.add(BusStop("Princeton", "Gate No. 2", {{"1:10 PM", "4:10 PM", "6:10 PM"}}));

    CHECK(db.search("isbt").size() == 1);
    CHECK(db.search("  IsBt  ").size() == 1);                       // trim + case-insensitive
    CHECK(db.search("prince   chowk").size() == 2);                 // spaces collapsed, both duplicates kept
    CHECK(db.search("prince chowk")[0]->place() == "Prince Chowk"); // exact match ranked first
    CHECK(db.search("prince").size() == 3);                         // prefix (trie)
    CHECK(db.search("chowk").size() == 2);                          // substring
    CHECK(db.search("tower")[0]->place() == "Clock Tower");
    CHECK(db.search("zzz").empty());                                // unknown
    CHECK(db.search("   ").empty());                                // empty
    CHECK(db.departurePoints().front() == "Gate No. 2");            // sorted

    // --- did you mean ---
    std::vector<std::string> dym = db.didYouMean("clok");
    CHECK(!dym.empty() && dym[0] == "Clock Tower");
    CHECK(db.didYouMean("isbtt").size() == 1);
    CHECK(db.didYouMean("prince chowk").size() == 1);              // duplicate names listed once
    CHECK(db.didYouMean("xyzxyz").empty());                        // nothing close: no wild guesses

    // --- next bus ---
    const BusStop& noService = *db.search("prince chowk")[1];
    CHECK(noService.nextShift(9 * 60) == 1);                        // skips "No Service"
    CHECK(noService.nextShift(17 * 60) == -1);                      // last bus gone
    CHECK(!noService.runsIn(0) && noService.runsIn(1));

    // --- JSON writer ---
    JsonWriter j;
    j.beginObject().key("a").num(1).key("b").beginArray().str("x\"y").boolean(true).null().endArray().endObject();
    CHECK(j.text() == "{\"a\":1,\"b\":[\"x\\\"y\",true,null]}");

    // --- HTTP request parsing ---
    HttpRequest req;
    CHECK(HttpRequest::parse("GET /api/search?q=prince+chowk&x=1 HTTP/1.1\r\nHost: a\r\n\r\n", req));
    CHECK(req.method == "GET" && req.path == "/api/search" && req.param("q") == "prince chowk");
    CHECK(!HttpRequest::parse("garbage", req));

    // --- full request -> handler -> response, on the real dataset ---
    DataStore store("data/stops.csv");
    CHECK(store.reload(true));
    CHECK(store.db().size() == 82);
    CHECK(store.db().skippedRows() == 0);
    CHECK(store.db().departurePoints().size() == 4);

    HttpServer server;
    server.addRoute("/api/search", std::unique_ptr<RequestHandler>(new SearchHandler(store)));
    server.addRoute("/api/meta", std::unique_ptr<RequestHandler>(new MetaHandler(store)));
    server.setFallback(std::unique_ptr<RequestHandler>(new StaticFileHandler("frontend")));
    auto get = [&](const std::string& target) {
        HttpRequest r;
        HttpRequest::parse("GET " + target + " HTTP/1.1\r\n\r\n", r);
        return server.dispatch(r);
    };
    HttpResponse r1 = get("/api/search?q=ISBT");
    CHECK(r1.status == 200 && contains(r1.body, "\"departureFrom\":\"Parking Near Hill (Old)\""));
    HttpResponse r2 = get("/api/search?q=clok%20towr");
    CHECK(r2.status == 200 && contains(r2.body, "\"count\":0") && contains(r2.body, "Clock Tower"));
    CHECK(get("/api/search?q=").status == 400);
    CHECK(get("/api/search?q=%3Cscript%3E").status == 400);
    CHECK(get("/api/unknown").status == 404);
    CHECK(get("/").status == 200);
    CHECK(get("/../data/stops.csv").status == 400);
    CHECK(get("/missing.html").status == 404);
    HttpRequest post;
    HttpRequest::parse("POST /api/search HTTP/1.1\r\n\r\n", post);
    CHECK(server.dispatch(post).status == 405);

    std::cout << (checks - failures) << "/" << checks << " checks passed\n";
    return failures ? 1 : 0;
}
