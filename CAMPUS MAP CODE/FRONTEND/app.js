// UNIGO Campus Map (on its own): picks the tool from the address and hands over to campus-map.js.
//   #/map         -> choose a tool
//   #/map/route   -> Find a Route          (#/map/route?to=csit-block opens it with a destination)
//   #/map/rooms   -> Find a Classroom / Lab
(function () {
  "use strict";
  const TOOLS = { "#/map/route": "route", "#/map/rooms": "rooms" };
  function route() {
    const [base, query] = location.hash.split("?");
    window.UnigoMap.show(new URLSearchParams(query || ""), TOOLS[base] || "choose");
    window.scrollTo(0, 0);
  }
  window.addEventListener("hashchange", route);
  route();
})();
