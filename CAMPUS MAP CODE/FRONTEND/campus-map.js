// campus-map.js -- the Campus Map (#/map) with two tools:
//   Find a Route (#/map/route)            BLUE start, RED destination, up to 4 routes from the C++ server
//   Find a Classroom / Lab (#/map/rooms)  search rooms, see building + floor, then get directions
// The map itself is data/campus_map.json (served by /api/map), drawn as SVG.
(function () {
  "use strict";

  const S = window.UnigoSearch;
  const esc = S.esc;
  const $ = (id) => document.getElementById(id);
  const NS = "http://www.w3.org/2000/svg";

  const TYPE_ICON = { gate: "🚪", academic: "🏫", lab: "💻", library: "📚", admin: "🏛️", hostel: "🏠", residence: "🏡",
    food: "🍴", park: "🌳", sports: "⚽", facility: "➕", landmark: "📍", parking: "🅿️", transport: "🚌" };
  const ID_ICON = { dispensary: "🩺", washroom: "🚻", fountain: "⛲", "flag-area": "🚩", "indoor-badminton-court": "🏸",
    "basketball-court": "🏀", subway: "🚇", "happiness-hut": "☕", "quick-bite-cafe": "☕", "mechanical-workshop": "🔧",
    "aryabhatta-lab": "💻", "param-lab": "💻" };
  const TYPE_LABEL = { gate: "Gate", academic: "Academic block", lab: "Laboratory", library: "Library", admin: "Administration",
    hostel: "Hostel", residence: "Residence", food: "Food & café", park: "Park / open area", sports: "Sports", facility: "Facility",
    landmark: "Landmark", parking: "Parking", transport: "Bus stop" };
  const ROAD_LABELS = ["Bell Road", "Gate No. 2 Road", "Civil Engg. Lane", "Gate No. 1 Road"];

  let data = null, loadingPromise = null;
  const places = {};                       // id -> place from the JSON
  let svg, L = {};                         // svg element and its layers
  let view = { x: 0, y: 0, w: 1600, h: 900 }, W = 1600, H = 900;
  let from = null, to = null, routes = null, sel = 0, pick = null, routeSeq = 0, lookingFor = null;
  let fromBox, toBox, roomBox, relayoutQueued = false;
  let mode = "choose";                    // "choose" | "route" | "rooms"

  const iconFor = (p) => ID_ICON[p.id] || TYPE_ICON[p.type] || "📍";
  const help = (msg, bad) => { const h = $("mapHelp"); h.textContent = msg; h.classList.toggle("bad", !!bad); };

  function el(tag, attrs, parent) {
    const e = document.createElementNS(NS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  const pts = (list) => list.map((p) => p[0] + "," + p[1]).join(" ");

  // ---------- loading and drawing the map ----------
  function ensureLoaded() {
    if (data) return Promise.resolve();
    if (!loadingPromise) {
      loadingPromise = S.getJson("/api/map").then(({ ok, data: d }) => {
        if (!ok || !d.places) throw new Error("bad map");
        data = d;
        W = d.meta.width; H = d.meta.height;
        d.places.forEach((p) => { places[p.id] = p; });
        if (d.meta.note) $("mapNote").textContent = d.meta.note;
        draw();
        $("mapStatus").hidden = true;
      }).catch(() => {
        loadingPromise = null;
        $("mapStatus").innerHTML = "<span>Can’t load the campus map. Please check that the UNIGO server is running and refresh.</span>";
      });
    }
    return loadingPromise;
  }

  function draw() {
    svg.innerHTML = "";
    const defs = el("defs", {}, svg);
    defs.innerHTML = '<filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="4" result="b"/>' +
      '<feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>';
    ["ground", "roads", "places", "labels", "route", "markers"].forEach((n) => { L[n] = el("g", { class: "layer-" + n }, svg); });

    el("rect", { x: -400, y: -400, width: W + 800, height: H + 800, class: "map-bg" }, L.ground);
    (data.decor || []).forEach((d) => {
      if (d.rect) el("rect", { x: d.rect[0], y: d.rect[1], width: d.rect[2], height: d.rect[3], rx: 14, class: "decor-" + d.kind }, L.ground);
      if (d.circle) el("circle", { cx: d.circle[0], cy: d.circle[1], r: d.circle[2], class: "decor-" + d.kind }, L.ground);
      if (d.points) el("polyline", { points: pts(d.points), class: "decor-" + d.kind }, d.kind === "wall" ? L.roads : L.ground);
    });

    const N = data.nodes;
    const casing = el("g", {}, L.roads), surface = el("g", {}, L.roads), paths = el("g", {}, L.roads), names = el("g", {}, L.roads);
    data.roads.forEach((r) => {
      const p = pts(r.path.map((id) => N[id]));
      if (r.kind === "road") {
        el("polyline", { points: p, class: "road-casing" }, casing);
        el("polyline", { points: p, class: "road" }, surface);
      } else if (r.kind === "subway") {
        el("polyline", { points: p, class: "subway-band" }, paths);
        el("polyline", { points: p, class: "subway" }, paths);
      } else {
        el("polyline", { points: p, class: "footpath" }, paths);
      }
    });
    // road names on the longest straight piece of each main road
    ROAD_LABELS.forEach((name) => {
      let best = null;
      data.roads.filter((r) => r.name === name).forEach((r) => {
        for (let i = 1; i < r.path.length; i++) {
          const a = N[r.path[i - 1]], b = N[r.path[i]], len = Math.hypot(b[0] - a[0], b[1] - a[1]);
          if (!best || len > best.len) best = { a, b, len };
        }
      });
      if (!best) return;
      const vertical = Math.abs(best.b[0] - best.a[0]) < Math.abs(best.b[1] - best.a[1]);
      const cx = (best.a[0] + best.b[0]) / 2, cy = (best.a[1] + best.b[1]) / 2;
      const t = el("text", { x: cx, y: cy + 3.5, class: "road-name", transform: vertical ? "rotate(-90 " + cx + " " + cy + ")" : "" }, names);
      t.textContent = name;
    });

    data.places.forEach((p) => {
      const g = el("g", { class: "place type-" + p.type, "data-place": p.id, tabindex: "0", role: "button", "aria-label": p.name }, L.places);
      el("title", {}, g).textContent = p.name;
      if (p.rect) {
        const [x, y, w, h] = p.rect;
        if (p.shape === "ellipse") {
          if (p.type === "sports") el("rect", { x: x - 6, y: y - 6, width: w + 12, height: h + 12, rx: h / 2 + 6, class: "track" }, g);
          el("ellipse", { cx: x + w / 2, cy: y + h / 2, rx: w / 2, ry: h / 2, class: "shape" }, g);
        } else {
          el("rect", { x, y, width: w, height: h, rx: 8, class: "shape" }, g);
        }
      } else {
        const [mx, my] = p.marker || N[p.link];
        el("circle", { cx: mx, cy: my, r: 13, class: "shape" }, g);
      }
      el("g", { class: "plabel", "data-for": p.id }, L.labels);
    });

    fitAll();
  }

  // Labels keep a constant on-screen size (12px) and are only shown when they fit inside the building.
  function scale() { const m = svg.getScreenCTM(); return m && m.a ? m.a : 0; }
  function queueRelayout() {
    if (relayoutQueued) return;
    relayoutQueued = true;
    requestAnimationFrame(() => { relayoutQueued = false; relayout(); });
  }
  function wrap(text, font, maxW) {
    const charW = font * 0.53, words = text.split(" "), lines = [];
    let line = "";
    for (const w of words) {
      const tryLine = line ? line + " " + w : w;
      if (tryLine.length * charW <= maxW) line = tryLine;
      else { if (!line || w.length * charW > maxW) return null; lines.push(line); line = w; }
    }
    if (line) lines.push(line);
    return lines;
  }
  function relayout() {
    if (!data) return;
    const k = scale();
    if (!k) return;
    const font = 12 / k, lh = font * 1.18, iconSize = 15 / k;
    data.places.forEach((p) => {
      const g = L.labels.querySelector('[data-for="' + p.id + '"]');
      g.innerHTML = "";
      if (!p.rect) {                                            // e.g. the subway: label beside the marker
        const [mx, my] = p.marker || data.nodes[p.link];
        const ic = el("text", { x: mx, y: my + iconSize * 0.35, class: "picon", "font-size": iconSize }, g); ic.textContent = iconFor(p);
        const t = el("text", { x: mx + 16 / k, y: my + font * 0.35, class: "plabel-side", "font-size": font }, g); t.textContent = p.name;
        return;
      }
      const [x, y, w, h] = p.rect, cx = x + w / 2, cy = y + h / 2;
      if (p.type === "gate") {                                   // gates are landmarks: always name them
        const t = el("text", { x: cx, y: cy + font * 0.35, class: "plabel-side gate-label", "font-size": font }, g); t.textContent = p.name;
        return;
      }
      // Try the normal size, then a slightly smaller one, before falling back to the icon only.
      let fit = null;
      for (const px of [12, 10.5, 9.5]) {
        const f = px / k, lines = wrap(p.name, f, w - 6 / k);
        if (lines && lines.length * f * 1.12 <= h - 3 / k) { fit = { f, lines, lh: f * 1.12 }; break; }
      }
      if (fit) {
        const textH = fit.lines.length * fit.lh;
        const withIcon = textH + iconSize + 2 / k <= h - 6 / k;
        let top = cy - (textH + (withIcon ? iconSize + 2 / k : 0)) / 2;
        if (withIcon) { const ic = el("text", { x: cx, y: top + iconSize * 0.85, class: "picon", "font-size": iconSize }, g); ic.textContent = iconFor(p); top += iconSize + 2 / k; }
        const t = el("text", { x: cx, y: top + fit.f * 0.92, class: "plabel-text", "font-size": fit.f }, g);
        fit.lines.forEach((ln, i) => { const s = el("tspan", { x: cx, dy: i ? fit.lh : 0 }, t); s.textContent = ln; });
      } else if (iconSize <= Math.min(w, h) - 4 / k) {
        const ic = el("text", { x: cx, y: cy + iconSize * 0.35, class: "picon", "font-size": iconSize }, g); ic.textContent = iconFor(p);
      }
    });
    L.roads.querySelectorAll(".road-name").forEach((t) => t.setAttribute("font-size", 11 / k));
    drawMarkers();
  }

  // ---------- view: pan and zoom by changing the SVG viewBox ----------
  function aspect() { const r = svg.getBoundingClientRect(); return r.width ? r.height / r.width : H / W; }
  function setView(v) {
    const a = aspect();
    v.w = Math.min(Math.max(v.w, 240), W * 1.4);
    v.h = v.w * a;
    v.x = Math.min(Math.max(v.x, -v.w * 0.25), W - v.w * 0.75);   // keep some of the map on screen
    v.y = Math.min(Math.max(v.y, -v.h * 0.25), H - v.h * 0.75);
    view = v;
    svg.setAttribute("viewBox", [v.x, v.y, v.w, v.h].map((n) => n.toFixed(1)).join(" "));
    queueRelayout();
  }
  function fitBox(x0, y0, x1, y1, pad) {
    const a = aspect();
    let w = Math.max(x1 - x0 + 2 * pad, (y1 - y0 + 2 * pad) / a, 240);
    setView({ x: (x0 + x1) / 2 - w / 2, y: (y0 + y1) / 2 - (w * a) / 2, w });
  }
  // Fit a box leaving fixed SCREEN margins (px), so marker labels never hang off the edge.
  function fitBoxPx(x0, y0, x1, y1, m) {
    const r = svg.getBoundingClientRect(), fw = r.width || 800, fh = r.height || 500, a = fh / fw;
    const s = Math.min(1, fw / 700);                            // smaller margins on phone-sized maps
    m = { l: m.l * s, r: m.r * s, t: m.t * s, b: m.b * s };
    const sx = Math.max(x1 - x0, 1), sy = Math.max(y1 - y0, 1);
    let w = Math.max(sx / Math.max(0.2, 1 - (m.l + m.r) / fw), sy / (a * Math.max(0.2, 1 - (m.t + m.b) / fh)), 240);
    const h = w * a;
    setView({ x: x0 - (m.l * w) / fw - (w - sx - ((m.l + m.r) * w) / fw) / 2, y: y0 - (m.t * h) / fh - (h - sy - ((m.t + m.b) * h) / fh) / 2, w });
  }
  function fitAll() {
    const a = aspect(), mw = W * 1.04, mh = H * 1.04;             // 2% margin on every side
    if (a > mh / mw) setView({ x: (W - mw) / 2, y: (H - mw * a) / 2, w: mw });   // tall screen: fit width
    else { const w = mh / a; setView({ x: (W - w) / 2, y: (H - mh) / 2, w }); }
  }
  function toMap(clientX, clientY) {
    const p = svg.createSVGPoint();
    p.x = clientX; p.y = clientY;
    const r = p.matrixTransform(svg.getScreenCTM().inverse());
    return { x: r.x, y: r.y };
  }
  function zoomAt(clientX, clientY, f) {
    const p = toMap(clientX, clientY);
    setView({ x: p.x - (p.x - view.x) * f, y: p.y - (p.y - view.y) * f, w: view.w * f });
  }
  function zoomCenter(f) { const r = svg.getBoundingClientRect(); zoomAt(r.left + r.width / 2, r.top + r.height / 2, f); }

  function setupGestures() {
    const ptrs = new Map();
    let drag = null, pinch = null, moved = false;
    svg.addEventListener("pointerdown", (e) => {
      try { svg.setPointerCapture(e.pointerId); } catch (err) { /* capture is optional; dragging still works */ }
      ptrs.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (ptrs.size === 1) { drag = { x: e.clientX, y: e.clientY, v: Object.assign({}, view) }; moved = false; }
      if (ptrs.size === 2) {
        const [a, b] = [...ptrs.values()];
        pinch = { d: Math.hypot(a.x - b.x, a.y - b.y), v: Object.assign({}, view), c: toMap((a.x + b.x) / 2, (a.y + b.y) / 2) };
        moved = true;
      }
    });
    svg.addEventListener("pointermove", (e) => {
      if (!ptrs.has(e.pointerId)) return;
      ptrs.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pinch && ptrs.size === 2) {
        const [a, b] = [...ptrs.values()];
        const f = pinch.d / Math.max(10, Math.hypot(a.x - b.x, a.y - b.y));
        const w = pinch.v.w * f;
        setView({ x: pinch.c.x - (pinch.c.x - pinch.v.x) * f, y: pinch.c.y - (pinch.c.y - pinch.v.y) * f, w });
      } else if (drag) {
        const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
        if (Math.hypot(dx, dy) > 5) moved = true;
        if (moved) { const k = scale(); setView({ x: drag.v.x - dx / k, y: drag.v.y - dy / k, w: drag.v.w }); }
      }
    });
    const end = (e) => {
      if (!ptrs.has(e.pointerId)) return;
      ptrs.delete(e.pointerId);
      if (ptrs.size < 2) pinch = null;
      if (ptrs.size === 0) { if (!moved && e.type === "pointerup") tap(e.clientX, e.clientY); drag = null; }
      else { const [a] = [...ptrs.values()]; drag = { x: a.x, y: a.y, v: Object.assign({}, view) }; }
    };
    svg.addEventListener("pointerup", end);
    svg.addEventListener("pointercancel", end);
    svg.addEventListener("wheel", (e) => { e.preventDefault(); zoomAt(e.clientX, e.clientY, e.deltaY > 0 ? 1.18 : 1 / 1.18); }, { passive: false });
    svg.addEventListener("keydown", (e) => {                       // keyboard: Enter on a focused building
      const g = e.target.closest && e.target.closest("[data-place]");
      if (g && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); choosePlace(g.dataset.place); }
    });
  }

  // ---------- choosing start / destination ----------
  const placeEnd = (id) => { const p = places[id]; const c = center(p); return { kind: "place", id, name: p.name, x: c.x, y: c.y }; };
  const pointEnd = (x, y, name) => ({ kind: "point", x, y, name: name || "Selected point" });
  const param = (e) => (e.kind === "place" ? e.id : Math.round(e.x) + "," + Math.round(e.y));
  function center(p) {
    if (p.rect) return { x: p.rect[0] + p.rect[2] / 2, y: p.rect[1] + p.rect[3] / 2 };
    const m = p.marker || data.nodes[p.link];
    return { x: m[0], y: m[1] };
  }

  function tap(clientX, clientY) {
    const hit = document.elementFromPoint(clientX, clientY);
    const g = hit && hit.closest && hit.closest("[data-place]");
    if (pick) {
      const p = toMap(clientX, clientY);
      if (p.x < 0 || p.y < 0 || p.x > W || p.y > H) return help("That spot is outside the campus map. Tap inside the map.", true);
      const end = g ? placeEnd(g.dataset.place) : pointEnd(p.x, p.y, pick === "from" ? "Your location" : "Selected point");
      const which = pick;
      setPick(null);
      return which === "from" ? setFrom(end) : setTo(end);
    }
    const alt = hit && hit.closest && hit.closest("[data-route]");
    if (alt) return selectRoute(Number(alt.dataset.route));
    if (g) choosePlace(g.dataset.place);
  }

  function setPick(which) {
    pick = which;
    $("pickFrom").setAttribute("aria-pressed", String(which === "from"));
    $("pickTo").setAttribute("aria-pressed", String(which === "to"));
    $("mapFrame").classList.toggle("picking", !!which);
    if (which) help(which === "from" ? "Tap the map where you are now (or tap a building)." : "Tap your destination on the map.");
  }

  function setFrom(end) {
    from = end;
    fromBox.setValue(end ? end.name : "");
    afterChange();
  }
  function setTo(end, keepLookingFor) {
    to = end;
    if (!keepLookingFor) lookingFor = null;
    toBox.setValue(end ? end.name : "");
    afterChange();
    if (end && !from) {
      focusOn(end);
      help("Destination set. Now choose your starting point: search it, use “Pick on map”, or “Use my location”.");
    }
  }
  function afterChange() {
    if (mode === "rooms") { routes = null; if (L.route) L.route.innerHTML = ""; highlightPlaces(); drawMarkers(); return; }
    routes = null;
    routeSeq++;
    L.route.innerHTML = "";
    highlightPlaces();
    drawMarkers();
    if (from && to) findRoute();
    else if (to && to.kind === "place") placeCard(to.id);
    else if (from && !to) {
      if (from.kind === "place") placeCard(from.id);
      help("Start set. Now search or pick your destination.");
    }
    else if (!from && !to) defaultPanel();
  }
  function focusOn(end) {
    const p = end.kind === "place" ? places[end.id] : null;
    const m = { l: 140, r: 140, t: 130, b: 110 };
    if (p && p.rect) fitBoxPx(p.rect[0], p.rect[1], p.rect[0] + p.rect[2], p.rect[1] + p.rect[3], m);
    else fitBoxPx(end.x - 40, end.y - 40, end.x + 40, end.y + 40, m);
  }

  function highlightPlaces() {
    L.places.querySelectorAll(".place").forEach((g) => {
      g.classList.toggle("is-dest", !!(to && to.kind === "place" && to.id === g.dataset.place));
      g.classList.toggle("is-start", !!(from && from.kind === "place" && from.id === g.dataset.place));
    });
  }

  // Markers keep a constant on-screen size: BLUE dot = start, RED pin = destination.
  function drawMarkers() {
    if (!L.markers) return;
    L.markers.innerHTML = "";
    const k = scale();
    if (!k) return;
    const s = 1 / k;
    if (from) {
      const g = el("g", { transform: "translate(" + from.x + " " + from.y + ") scale(" + s + ")", class: "marker-start" }, L.markers);
      el("circle", { r: 18, class: "pulse" }, g);
      el("circle", { r: 9, class: "dot" }, g);
      callout(g, from.name, "start", -20, from.x);
    }
    if (to) {
      const g = el("g", { transform: "translate(" + to.x + " " + to.y + ") scale(" + s + ")", class: "marker-dest" }, L.markers);
      el("path", { d: "M0,0 C-3,-9 -13,-14 -13,-25 A13,13 0 1 1 13,-25 C13,-14 3,-9 0,0Z", class: "pin" }, g);
      el("circle", { cy: -25, r: 5, class: "pin-hole" }, g);
      callout(g, to.name, "dest", -46, to.x);
    }
  }
  // A label above a marker. Units inside the marker group are screen pixels; if the marker is near
  // the edge of the map frame, the label slides sideways so it stays fully visible.
  function callout(g, text, kind, dy, mapX) {
    const label = text.length > 34 ? text.slice(0, 32) + "…" : text;
    const w = label.length * 7.1 + 18;
    const m = svg.getScreenCTM(), r = svg.getBoundingClientRect();
    const sx = m.a * mapX + m.e;                                  // marker's x on screen
    let dx = 0;
    if (sx - w / 2 < r.left + 6) dx = r.left + 6 - (sx - w / 2);
    else if (sx + w / 2 > r.right - 6) dx = r.right - 6 - (sx + w / 2);
    const c = el("g", { class: "callout callout-" + kind, transform: "translate(" + dx.toFixed(1) + " " + dy + ")" }, g);
    el("rect", { x: -w / 2, y: -26, width: w, height: 22, rx: 11 }, c);
    const t = el("text", { x: 0, y: -11 }, c);
    t.textContent = label;
  }

  // ---------- routes: the server sends up to 4 different routes, the user picks one ----------
  const GENERIC = ["Campus Road", "Subway Path"];
  const sigOf = (r) => r.via.filter((v) => GENERIC.indexOf(v) < 0);

  // A short name that tells the routes apart, e.g. "via the Subway" or "via Gate No. 1 & Bell Road".
  // Routes are grouped by HOW they cross to the other side ("the Subway" / "Gate No. 1 & Bell Road");
  // inside a group, each route is named after a road that sets it apart ("& Hostel Road"), or the road it skips.
  function groupOf(r) {
    const s = sigOf(r);
    return s.includes("Subway (Underpass)") ? "via the Subway" : s.includes("Bell Road") ? "via Gate No. 1 & Bell Road" : "";
  }
  function routeName(i) {
    if (routes.length === 1) return "The best route";
    const sigs = routes.map(sigOf), mine = sigs[i], group = groupOf(routes[i]);
    const crossing = (v) => v === "Subway (Underpass)" || v === "Bell Road";
    const common = sigs.reduce((acc, s) => acc.filter((v) => s.includes(v)));        // roads every route uses
    // Compare this route with the others in its group (routes with no crossing form one group too).
    const inGroup = (j) => groupOf(routes[j]) === group;
    const others = sigs.filter((s, j) => j !== i && inGroup(j));
    const shared = (v) => others.filter((s) => s.includes(v)).length;    // how many of them also use road v
    const own = mine.filter((v) => !crossing(v) && !common.includes(v));  // roads not every route uses
    const join = (v) => (group ? group + " & " : "via ") + v;
    const cands = [];                                                      // candidate names, best first
    if (!others.length) cands.push(group || (own.length ? "via " + own[0] : "The best route"));
    own.filter((v) => shared(v) < others.length)                          // roads that set it apart, rarest first
      .sort((a, b) => shared(a) - shared(b)).forEach((v) => cands.push(join(v)));
    const coveredBy = sigs.slice(0, i).find((s, j) => inGroup(j) && mine.every((v) => s.includes(v)));
    const skipped = coveredBy && coveredBy.find((v) => !mine.includes(v) && !crossing(v));
    if (skipped) cands.push((group ? group + ", " : "") + "avoiding the " + skipped);
    if (skipped && own.length) cands.push(join(own[0]) + ", avoiding the " + skipped);
    if (own.length > 1) cands.push((group ? group + ", " : "via ") + own.slice(0, -1).join(", ") + " & " + own[own.length - 1]);
    if (group) cands.push(group);
    const before = [];
    for (let j = 0; j < i; j++) before.push(routeName(j));
    for (const c of cands) if (!before.includes(c)) return c;
    return (cands[0] || "Route") + " (option " + (i + 1) + ")";
  }
  function lengthTag(i) {                       // the map is not to scale, so compare routes in words only
    if (i === 0) return "Shortest";
    const ratio = routes[i].length / routes[0].length;
    return ratio < 1.1 ? "About the same" : ratio < 1.35 ? "A little longer" : "Longer";
  }

  async function findRoute() {
    if (from.kind === "place" && to.kind === "place" && from.id === to.id) {
      return panel('<div class="card-lite"><h3>You are already there</h3><p class="muted">Your start and destination are the same place: <b>' + esc(to.name) + "</b>.</p>" + lookingCard() + clearButtons() + "</div>");
    }
    const my = ++routeSeq;
    panel('<div class="state"><div class="spinner" aria-hidden="true"></div><p>Finding the possible routes…</p></div>');
    try {
      const { ok, data: r } = await S.getJson("/api/route?from=" + encodeURIComponent(param(from)) + "&to=" + encodeURIComponent(param(to)));
      if (my !== routeSeq) return;                              // the user changed something meanwhile
      if (!ok || !r.found) return panel('<div class="state error"><h3>No route found</h3><p>' + esc(r.message || "These two places are not connected.") + "</p></div>");
      routes = r.routes;
      sel = 0;
      drawRoutes();
      const all = [].concat(...routes.map((x) => x.points));
      const xs = all.map((p) => p[0]).concat(from.x, to.x), ys = all.map((p) => p[1]).concat(from.y, to.y);
      fitBoxPx(Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys), { l: 90, r: 90, t: 95, b: 40 });
      routeCard();
      help(routes.length > 1 ? routes.length + " possible routes found. Route 1 is highlighted; choose another on the right or tap a faded line."
                             : "Route ready: follow the highlighted line from the blue marker to the red pin.");
    } catch (e) {
      if (my === routeSeq) panel('<div class="state error"><h3>Can’t reach the UNIGO server</h3><p>Please check that the server is running.</p></div>');
    }
  }
  function drawRoutes() {
    L.route.innerHTML = "";
    if (!routes) return;
    routes.forEach((r, i) => {                                  // the other options: faded and clickable
      if (i === sel) return;
      el("polyline", { points: pts(r.points), class: "route-alt", "data-route": i, "vector-effect": "non-scaling-stroke" }, L.route);
    });
    const p = pts(routes[sel].points);                          // the chosen route on top
    el("polyline", { points: p, class: "route-casing", "vector-effect": "non-scaling-stroke" }, L.route);
    el("polyline", { points: p, class: "route-line", "vector-effect": "non-scaling-stroke" }, L.route);
    el("polyline", { points: p, class: "route-flow", "vector-effect": "non-scaling-stroke" }, L.route);
  }
  function selectRoute(i) {
    if (!routes || !routes[i]) return;
    sel = i;
    drawRoutes();
    routeCard();
    help("Showing Route " + (i + 1) + " (" + routeName(i) + ").");
  }
  function routeCard() {
    const r = routes[sel];
    const named = sigOf(r);
    const via = named.length ? named.map(esc).join(" → ") : "campus roads (a short walk)";
    const options = routes.length > 1
      ? '<p class="muted small">' + routes.length + " possible routes. Choose the one you want:</p>" +
        '<div class="route-options">' + routes.map((x, i) =>
          '<button type="button" class="route-option' + (i === sel ? " selected" : "") + '" data-act="route" data-i="' + i + '" aria-pressed="' + (i === sel) + '">' +
          '<span class="ro-num">' + (i + 1) + '</span><span class="ro-text"><b>' + esc(routeName(i)) + "</b><small>" + lengthTag(i) + "</small></span></button>").join("") + "</div>"
      : '<p class="muted small">There is one sensible route between these places.</p>';
    panel('<div class="card-lite route-card"><h3>' + (routes.length > 1 ? "Choose your route" : "Your route") + "</h3>" + options +
      lookingCard() +
      '<ol class="route-steps"><li class="step-start"><span class="legend-dot start"></span><div><small>From</small>' + esc(from.name) + "</div></li>" +
      '<li class="step-via"><div><small>Route ' + (sel + 1) + " · walk along</small>" + via + "</div></li>" +
      '<li class="step-end"><span class="legend-pin"></span><div><small>To</small>' + esc(to.name) + "</div></li></ol>" +
      clearButtons() + "</div>");
  }
  function clearButtons() {
    return '<div class="panel-actions"><button type="button" class="btn-soft" data-act="new-dest">Choose a new destination</button>' +
      '<button type="button" class="btn-soft" data-act="clear-all">Clear route</button></div>';
  }

  // ---------- classrooms / facilities: shared pieces ----------
  // Status text comes straight from the data; only an explicit "verified" is shown as verified.
  function statusBadge(rm) {
    if (rm.verified) return '<span class="badge">Verified</span>';
    const s = rm.status || "Status not given";
    const warn = /verify|tentative|provisional|unconfirmed|not/i.test(s);
    return '<span class="badge ' + (warn ? "badge-warn" : "badge-info") + '">' + esc(s) + "</span>";
  }
  function roomDetails(rm) {
    return '<div class="room-detail"><div class="rd-head"><span class="place-icon small">🚪</span><div><b>' + esc(rm.name) + "</b>" +
      '<small>' + esc(rm.category || "Room") + "</small></div></div>" +
      '<dl class="room-facts"><div><dt>Building</dt><dd>' + esc(rm.placeName || rm.building) + "</dd></div>" +
      "<div><dt>Floor</dt><dd>" + (rm.floor ? esc(rm.floor) : "Not listed") + "</dd></div></dl>" +
      (rm.purpose ? '<p class="purpose">' + esc(rm.purpose) + "</p>" : "") + statusBadge(rm) + "</div>";
  }
  function lookingCard() {
    if (!lookingFor) return "";
    return lookingFor.building ? '<div class="looking-card"><small>You are going to</small>' + roomDetails(lookingFor) + "</div>"
                               : '<p class="looking">Looking for <b>' + esc(lookingFor.name) + "</b></p>";
  }
  // A destination for a room: its building, labelled "CR 1 (CSIT Block)".
  function roomEnd(rm) {
    if (!rm.placeId || !places[rm.placeId]) return null;
    const end = placeEnd(rm.placeId);
    end.name = rm.name + " (" + places[rm.placeId].name + ")";
    return end;
  }

  // ---------- side panel ----------
  function panel(html) { $("mapPanel").innerHTML = html; }
  function defaultPanel() {
    if (mode === "rooms") return roomsIntroPanel();
    panel('<div class="card-lite"><h3>Find your way</h3><ol class="howto"><li>Choose where you are (<span class="legend-dot start"></span> blue).</li>' +
      '<li>Choose the building you want to go to (<span class="legend-pin"></span> red).</li><li>Pick one of the possible routes.</li></ol>' +
      '<p class="muted small">Drag to move the map, scroll or pinch to zoom, and tap a building to see its options.</p>' +
      '<div class="legend"><span><i class="sw sw-road"></i>Road</span><span><i class="sw sw-path"></i>Footpath</span>' +
      '<span><i class="sw sw-subway"></i>Subway</span><span><i class="sw sw-wall"></i>Campus wall</span>' +
      '<span><i class="sw sw-route"></i>Chosen route</span><span><i class="sw sw-alt"></i>Other route</span></div></div>');
  }
  function roomsIntroPanel() {
    panel('<div class="card-lite"><h3>Find a classroom or lab</h3><ol class="howto">' +
      "<li>Search a room, e.g. CR 1, LT 12, CS Lab 1, Fee Enquiry, Registrar.</li>" +
      "<li>See its building, floor and details; the building is marked on the map.</li>" +
      "<li>Press “Get directions” to plan the route there.</li></ol>" +
      (roomsMeta ? '<p class="muted small">' + (roomsMeta.loaded ? roomsMeta.count + " classrooms, labs and offices are listed." : esc(roomsMeta.status)) + "</p>" : "") +
      '<p class="muted small">You can also tap a building on the map to list its rooms.</p></div>');
  }

  let roomsHere = [];                                            // rooms listed in the open building card
  async function placeCard(id) {
    const p = places[id];
    const isDest = to && to.kind === "place" && to.id === id, isStart = from && from.kind === "place" && from.id === id;
    const actions = mode === "rooms"
      ? '<button type="button" class="btn" data-act="go-route" data-id="' + esc(id) + '">Get directions here</button>'
      : (isDest ? "" : '<button type="button" class="btn" data-act="to" data-id="' + esc(id) + '">Directions to here</button>') +
        (isStart ? "" : '<button type="button" class="btn-soft" data-act="from" data-id="' + esc(id) + '">Start from here</button>');
    panel('<div class="card-lite place-card">' +
      '<div class="place-head"><span class="place-icon">' + iconFor(p) + "</span><div><h3>" + esc(p.name) + "</h3>" +
      '<p class="muted small">' + esc(TYPE_LABEL[p.type] || p.type) + "</p></div></div>" +
      (isDest && mode === "route" ? lookingCard() : "") +
      '<div class="panel-actions">' + actions +
      (p.type === "transport" ? '<a class="btn-soft" href="#/bus">Open Bus Finder</a>' : "") + "</div>" +
      (isDest && !from && mode === "route" ? '<p class="hint">Now choose your starting point to see the possible routes.</p>' : "") +
      '<div class="rooms-here" id="roomsHere"><p class="muted small">Loading classrooms…</p></div></div>');
    try {
      const { ok, data: r } = await S.getJson("/api/rooms/in?place=" + encodeURIComponent(id));
      const box = $("roomsHere");
      if (!box) return;
      if (!ok) { box.innerHTML = ""; return; }
      if (!r.loaded) { box.innerHTML = '<p class="muted small">Classroom details will appear here once the classroom data is added.</p>'; return; }
      if (!r.rooms.length) { box.innerHTML = '<p class="muted small">No classrooms or facilities are listed for this place yet.</p>'; return; }
      roomsHere = r.rooms;
      box.innerHTML = "<h4>Classrooms &amp; facilities here (" + r.rooms.length + ")</h4><ul class='room-mini'>" + r.rooms.map((rm, i) =>
        '<li><button type="button" class="room-pick" data-act="room-here" data-i="' + i + '"><b>' + esc(rm.name) + "</b>" +
        "<small>" + esc(rm.floor || "Floor not listed") + (rm.category ? " · " + esc(rm.category) : "") + "</small></button></li>").join("") + "</ul>";
    } catch (e) { /* the room list is optional */ }
  }

  function choosePlace(id) {
    if (!places[id]) return;
    placeCard(id);
  }

  // ---------- Find a Route: Enter pressed without picking a suggestion ----------
  let lastChoices = [];
  async function submitPlaceSearch(text, which) {
    const problem = S.validate(text);
    if (problem) return help(problem, true);
    help("Searching…");
    try {
      const { ok, data: r } = await S.getJson("/api/places/search?q=" + encodeURIComponent(text.trim()));
      if (!ok) return help(r.message || "Please check what you typed.", true);
      const exact = r.results.filter((x) => x.exactMatch);
      if (r.count === 1 || exact.length === 1) {
        const end = placeEnd((exact[0] || r.results[0]).place.id);
        return which === "from" ? setFrom(end) : setTo(end);
      }
      const list = r.count ? r.results.map((x) => x.place) : r.didYouMean;
      if (!list.length) {
        help("No matching location found. Please check the spelling or try searching with a different name.", true);
        return panel('<div class="state error"><h3>No matching location found</h3><p>Please check the spelling or try searching with a different name.</p>' +
          '<p class="small">Looking for a classroom or lab? Use <a href="#/map/rooms">Find a Classroom / Lab</a>.</p></div>');
      }
      lastChoices = list;
      help(r.count ? "Several places match. Choose one." : "Did you mean one of these?");
      panel('<div class="card-lite"><h3>' + (r.count ? r.count + " places match “" + esc(text.trim()) + "”" : "Did you mean " + esc(list[0].name) + "?") + "</h3>" +
        '<div class="choice-list">' + list.map((p, i) => '<button type="button" class="tip" data-act="choice" data-which="' + which + '" data-i="' + i + '">' +
        (ID_ICON[p.id] || TYPE_ICON[p.type] || "📍") + " <b>" + esc(p.name) + "</b></button>").join("") + "</div></div>");
    } catch (e) { help("Can’t reach the UNIGO server.", true); }
  }

  // ---------- Find a Classroom / Lab ----------
  let roomList = [], roomQuery = "", roomSeqNo = 0, roomsMeta = null;
  async function searchRooms(text) {
    roomBox.hide();
    const problem = S.validate(text);
    if (problem) { help(problem, true); $("roomInput").focus(); return; }
    const my = ++roomSeqNo;
    help("Searching…");
    panel('<div class="state"><div class="spinner" aria-hidden="true"></div><p>Searching classrooms and labs…</p></div>');
    try {
      const { ok, data: r } = await S.getJson("/api/rooms/search?q=" + encodeURIComponent(text.trim()));
      if (my !== roomSeqNo) return;                             // an older search answered late
      if (!ok) { help(r.message || "Please check what you typed.", true); return roomsIntroPanel(); }
      if (!r.loaded) { help(r.status, true); return panel('<div class="state notice"><h3>Classroom data not added yet</h3><p>' + esc(r.status) + "</p></div>"); }
      roomQuery = r.query;
      if (r.count === 0) {
        roomList = r.didYouMean;
        if (!roomList.length) {
          help("No matching location found. Please check the spelling or try searching with a different name.", true);
          return panel('<div class="state error"><h3>No matching room found</h3><p>No matching location found. Please check the spelling or try searching with a different name.</p></div>');
        }
        help("Did you mean " + roomList[0].name + "?");
        return panel('<div class="card-lite"><h3>Did you mean ' + esc(roomList[0].name) + "?</h3>" +
          '<p class="muted small">We couldn’t find “' + esc(r.query) + "” exactly.</p>" + roomButtons() + "</div>");
      }
      roomList = r.results.map((x) => x.room);
      const exact = r.results.filter((x) => x.exactMatch);
      if (roomList.length === 1 || exact.length === 1) return showRoom(exact.length === 1 ? exact[0].room : roomList[0], roomList.length > 1);
      listRooms();
    } catch (e) {
      if (my === roomSeqNo) { help("Can’t reach the UNIGO server.", true); panel('<div class="state error"><h3>Can’t reach the UNIGO server</h3><p>Please check that the server is running.</p></div>'); }
    }
  }
  function roomButtons() {
    return '<div class="room-results">' + roomList.map((rm, i) =>
      '<button type="button" class="room-result" data-act="room-pick" data-i="' + i + '"><b>' + esc(rm.name) + "</b>" +
      "<small>" + esc(rm.placeName || rm.building) + (rm.floor ? " · " + esc(rm.floor) : "") + "</small>" +
      (rm.category ? '<span class="rr-cat">' + esc(rm.category) + "</span>" : "") + "</button>").join("") + "</div>";
  }
  function listRooms() {
    to = null; lookingFor = null; highlightPlaces(); drawMarkers();
    help(roomList.length + " rooms match. Choose one to see where it is.");
    panel('<div class="card-lite"><h3>' + roomList.length + " results</h3>" +
      '<p class="muted small">for “' + esc(roomQuery) + "”. Choose one:</p>" + roomButtons() + "</div>");
  }
  // Shows one room: its details in the panel and its building marked (red pin) on the map. No route yet.
  function showRoom(rm, fromList) {
    lookingFor = rm;
    from = null; routes = null; routeSeq++;
    L.route.innerHTML = "";
    to = roomEnd(rm);
    highlightPlaces();
    drawMarkers();
    if (to) focusOn(to);
    panel('<div class="card-lite">' + roomDetails(rm) +
      '<div class="panel-actions">' + (to ? '<button type="button" class="btn" data-act="room-route">Get directions</button>' : "") +
      (fromList ? '<button type="button" class="btn-soft" data-act="room-back">Back to results</button>' : "") + "</div>" +
      (to ? "" : '<p class="hint">This building is not on the campus map yet, so it can’t be shown or routed to.</p>') + "</div>");
    help(to ? rm.name + " is in " + places[rm.placeId].name + (rm.floor ? ", " + rm.floor : "") + ". Press “Get directions” to plan a route."
            : rm.name + " is in " + rm.building + ", which is not on the map yet.");
  }
  // Hand over to Find a Route with this building (and room) as the destination.
  let pendingRoom = null;
  function goRoute(placeId, room) {
    pendingRoom = room || null;
    location.hash = "#/map/route?to=" + encodeURIComponent(placeId);
  }

  $("mapPanel") && $("mapPanel").addEventListener("click", (e) => {
    const b = e.target.closest("[data-act]");
    if (!b) return;
    const act = b.dataset.act, id = b.dataset.id, i = Number(b.dataset.i);
    if (act === "to") setTo(placeEnd(id));
    else if (act === "from") setFrom(placeEnd(id));
    else if (act === "route") selectRoute(i);
    else if (act === "choice") { const end = placeEnd(lastChoices[i].id); b.dataset.which === "from" ? setFrom(end) : setTo(end); }
    else if (act === "room-here") {                              // a room in a building card
      const rm = roomsHere[i];
      if (!rm) return;
      if (mode === "rooms") showRoom(rm, false);
      else { lookingFor = rm; setTo(roomEnd(rm) || placeEnd(rm.placeId), true); }
    }
    else if (act === "room-pick") { roomBox.setValue(roomList[i].name); showRoom(roomList[i], roomList.length > 1); }
    else if (act === "room-back") listRooms();
    else if (act === "room-route") goRoute(lookingFor.placeId, lookingFor);
    else if (act === "go-route") goRoute(id, null);
    else if (act === "new-dest") { lookingFor = null; setTo(null); toBox.setValue(""); $("toInput").focus(); help("Search or pick your new destination."); }
    else if (act === "clear-all") { from = null; to = null; lookingFor = null; fromBox.setValue(""); toBox.setValue(""); afterChange(); fitAll(); help("Route cleared. Choose a new start and destination."); }
  });

  // ---------- "Use my location" (only when the map has GPS reference points) ----------
  function geoToMap(lat, lon) {
    const g = data.meta.geo, [p1, p2] = g.points;
    const k = Math.cos((p1.lat * Math.PI) / 180);
    const z = (p) => ({ re: p.lon * k, im: -p.lat });                // local flat projection, y downwards
    const sub = (a, b) => ({ re: a.re - b.re, im: a.im - b.im }), mul = (a, b) => ({ re: a.re * b.re - a.im * b.im, im: a.re * b.im + a.im * b.re });
    const div = (a, b) => { const d = b.re * b.re + b.im * b.im; return { re: (a.re * b.re + a.im * b.im) / d, im: (a.im * b.re - a.re * b.im) / d }; };
    const z1 = z(p1), z2 = z(p2), w1 = { re: p1.x, im: p1.y }, w2 = { re: p2.x, im: p2.y };
    const a = div(sub(w2, w1), sub(z2, z1));                          // rotation + scale between the two systems
    const w = mul(a, sub(z({ lat, lon }), z1));
    return { x: w.re + w1.re, y: w.im + w1.im };
  }
  function useMyLocation() {
    const g = data && data.meta.geo;
    if (!g || !g.points || g.points.length < 2) {
      return help("Location access isn’t set up for this campus map yet. Please search your location or use “Pick on map”.", true);
    }
    if (!navigator.geolocation) return help("Your browser can’t share your location. Please use “Pick on map”.", true);
    help("Asking your browser for your location…");
    navigator.geolocation.getCurrentPosition((pos) => {
      const p = geoToMap(pos.coords.latitude, pos.coords.longitude);
      if (p.x < 0 || p.y < 0 || p.x > W || p.y > H) return help("You seem to be outside the campus. Please choose your starting point manually.", true);
      setFrom(pointEnd(p.x, p.y, "Your location"));
    }, () => help("Location permission was not given. Please search your location or use “Pick on map”.", true),
    { enableHighAccuracy: true, timeout: 10000 });
  }

  // ---------- public: called by app.js when the page opens ----------
  // tool = "choose" (the two big cards), "route" (Find a Route) or "rooms" (Find a Classroom / Lab).
  const TOOL_TEXT = {
    route: ["Find a Route", "Choose where you are and the building you want to reach, then pick one of the possible routes."],
    rooms: ["Find a Classroom / Lab", "Search a classroom, lab, lecture theatre or office to see its building and floor, then get directions."],
  };
  function resetTool() {
    from = null; to = null; routes = null; lookingFor = null; routeSeq++;
    setPick(null);
    fromBox.setValue(""); toBox.setValue(""); roomBox.setValue("");
    if (L.route) L.route.innerHTML = "";
    if (L.places) { highlightPlaces(); drawMarkers(); }
  }
  function show(params, tool) {
    const previous = mode;
    mode = tool || "choose";
    $("mapChooser").hidden = mode !== "choose";
    $("mapWork").hidden = mode === "choose";
    if (mode === "choose") { ensureLoaded(); return Promise.resolve(); }   // start loading the map in the background
    $("tabRoute").classList.toggle("active", mode === "route");
    $("tabRooms").classList.toggle("active", mode === "rooms");
    $("tabRoute").setAttribute("aria-current", mode === "route" ? "page" : "false");
    $("tabRooms").setAttribute("aria-current", mode === "rooms" ? "page" : "false");
    $("routeForm").hidden = mode !== "route";
    $("roomForm").hidden = mode !== "rooms";
    $("toolTitle").textContent = TOOL_TEXT[mode][0];
    $("toolIntro").textContent = TOOL_TEXT[mode][1];
    const changed = previous !== mode;
    return ensureLoaded().then(() => {
      requestAnimationFrame(() => {
        if (changed) resetTool();                               // each tool starts fresh
        if (mode === "route") {
          const target = params && params.get("to");
          if (target && places[target]) {                       // e.g. from "Get directions"
            lookingFor = pendingRoom || (params.get("room") ? { name: params.get("room") } : null);
            pendingRoom = null;
            setTo(lookingFor && lookingFor.placeId ? roomEnd(lookingFor) : placeEnd(target), true);
            if (from) findRoute();
          } else if (changed) {
            fitAll(); defaultPanel();
            help("Tip: search or tap where you are and where you want to go. Tap any building to see its options.");
          } else queueRelayout();
        } else {
          if (changed) {
            fitAll(); roomsIntroPanel();
            help("Tip: partial names, any capital letters and small spelling mistakes are fine.");
            S.getJson("/api/rooms/meta").then(({ ok, data: m }) => { if (ok) { roomsMeta = m; if (!lookingFor && !roomList.length) roomsIntroPanel(); } });
            if (window.matchMedia("(hover: hover)").matches) $("roomInput").focus();
          } else queueRelayout();
        }
      });
    });
  }

  function init() {
    svg = $("campusSvg");
    if (!svg) return;
    setupGestures();
    // Find a Route: both boxes search buildings and places
    fromBox = S.attach({ input: $("fromInput"), list: $("fromList"), box: $("fromBox"), clearBtn: $("fromClear"), suggestUrl: "/api/places/suggest?q=",
      onChoose: (it) => setFrom(placeEnd(it.id)), onSubmit: (t) => submitPlaceSearch(t, "from"), onClear: () => { from = null; afterChange(); } });
    toBox = S.attach({ input: $("toInput"), list: $("toList"), box: $("toBox"), clearBtn: $("toClear"), suggestUrl: "/api/places/suggest?q=",
      onChoose: (it) => setTo(placeEnd(it.id)), onSubmit: (t) => submitPlaceSearch(t, "to"), onClear: () => { to = null; lookingFor = null; afterChange(); } });
    // Find a Classroom / Lab: one box that searches rooms
    roomBox = S.attach({ input: $("roomInput"), list: $("roomList"), box: $("roomBox"), clearBtn: $("roomClear"), suggestUrl: "/api/rooms/suggest?q=",
      onChoose: (it) => searchRooms(it.label), onSubmit: searchRooms,
      onClear: () => { roomList = []; to = null; lookingFor = null; highlightPlaces(); drawMarkers(); roomsIntroPanel(); help("Search a classroom, lab or office."); } });
    $("roomSearchBtn").addEventListener("click", () => searchRooms($("roomInput").value));
    $("pickFrom").addEventListener("click", () => setPick(pick === "from" ? null : "from"));
    $("pickTo").addEventListener("click", () => setPick(pick === "to" ? null : "to"));
    $("gpsBtn").addEventListener("click", () => ensureLoaded().then(useMyLocation));
    $("swapBtn").addEventListener("click", () => {
      [from, to] = [to, from];
      fromBox.setValue(from ? from.name : ""); toBox.setValue(to ? to.name : "");
      afterChange();
    });
    $("zoomIn").addEventListener("click", () => zoomCenter(1 / 1.4));
    $("zoomOut").addEventListener("click", () => zoomCenter(1.4));
    $("zoomReset").addEventListener("click", () => { fitAll(); });
    document.addEventListener("keydown", (e) => { if (e.key === "Escape" && pick) { setPick(null); help("Picking cancelled."); } });
    window.addEventListener("resize", () => { if (data && !$("view-map").hidden && mode !== "choose") setView(Object.assign({}, view)); });
  }

  init();
  window.UnigoMap = { show };
})();
