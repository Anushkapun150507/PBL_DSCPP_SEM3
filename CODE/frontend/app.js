// UNIGO frontend. Pages: landing -> name -> home (4 blocks) -> bus finder.
// Bus data comes from the C++ backend (/api/*); nothing about buses is hardcoded here.
(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  // ---------- name storage (localStorage, with an in-memory fallback if it is blocked) ----------
  const NAME_KEY = "unigo_user_name";
  let memoryName = "";
  function getName() {
    try { return localStorage.getItem(NAME_KEY) || memoryName; } catch (e) { return memoryName; }
  }
  function setName(n) {
    memoryName = n;
    try { localStorage.setItem(NAME_KEY, n); } catch (e) { /* keep the in-memory copy */ }
  }
  function clearName() {
    memoryName = "";
    try { localStorage.removeItem(NAME_KEY); } catch (e) { /* ignore */ }
  }

  // ---------- routing by URL hash so the browser Back button works ----------
  const VIEWS = ["landing", "name", "home", "bus"];
  const ROUTES = { "": "landing", "#/": "landing", "#/name": "name", "#/home": "home", "#/bus": "bus" };

  function go(hash) { if (location.hash === hash) route(); else location.hash = hash; }

  function route() {
    let view = ROUTES[location.hash] || "landing";
    const name = getName();
    if ((view === "home" || view === "bus") && !name) view = "name";     // must enter a name first
    if ((view === "landing" || view === "name") && name) view = "home";  // already signed in
    VIEWS.forEach((v) => { $("view-" + v).hidden = v !== view; });

    const inApp = view === "home" || view === "bus";
    $("topbar").hidden = !inApp;
    $("homeBtn").classList.toggle("active", view === "home");
    document.querySelectorAll("#userName, .user-inline").forEach((el) => { el.textContent = name; });
    window.scrollTo(0, 0);

    if (view === "name") $("nameInput").focus();
    // Don't pop the phone keyboard up on every visit; desktop users get the cursor ready.
    if (view === "bus" && window.matchMedia("(hover: hover)").matches) $("destination").focus();
  }
  window.addEventListener("hashchange", route);

  // ---------- landing / name / header ----------
  $("startBtn").addEventListener("click", () => go("#/name"));

  // Letters from any language (e.g. Devanagari), plus spaces, dots, apostrophes and hyphens.
  const NAME_OK = /^\p{L}[\p{L}\p{M} .'\-]*$/u;
  $("nameForm").addEventListener("submit", (e) => {
    e.preventDefault();
    const input = $("nameInput");
    const value = input.value.trim().replace(/\s+/g, " ");
    let problem = "";
    if (!value) problem = "Please enter your name.";
    else if (value.length < 2) problem = "Your name needs at least 2 characters.";
    else if (!NAME_OK.test(value)) problem = "Please use letters only. Spaces, dots, apostrophes and hyphens are fine.";
    $("nameError").textContent = problem;
    input.setAttribute("aria-invalid", problem ? "true" : "false");
    if (problem) return input.focus();
    setName(value);
    go("#/home");
  });
  $("nameInput").addEventListener("input", () => {
    $("nameError").textContent = "";
    $("nameInput").setAttribute("aria-invalid", "false");
  });

  $("homeBtn").addEventListener("click", () => go("#/home"));
  $("logoutBtn").addEventListener("click", () => {
    clearName();
    $("nameInput").value = "";
    resetBus();
    go("#/");
  });
  $("openBus").addEventListener("click", () => go("#/bus"));

  // ---------- Bus Finder ----------
  const input = $("destination");
  const box = $("searchBox");
  const help = $("fieldHelp");
  const list = $("suggestions");
  const clearBtn = $("clearBtn");
  const STATES = ["idle", "loading", "error", "results"];
  const HELP_DEFAULT = help.textContent;
  let suggestTimer = null, suggestSeq = 0, searchSeq = 0, activeIndex = -1;

  const showState = (name) => STATES.forEach((s) => { $("state-" + s).hidden = s !== name; });

  // Same rule as the backend, for instant feedback.
  const VALID = /^[A-Za-z0-9 .\-'()\/&]+$/;
  function validate(v) {
    if (!v.trim()) return "Please enter a destination to search.";
    if (v.length > 60 || !VALID.test(v)) return "Use only letters, numbers and basic punctuation (max 60 characters).";
    return "";
  }
  function setFieldError(msg) {
    box.classList.toggle("invalid", !!msg);
    help.classList.toggle("bad", !!msg);
    help.textContent = msg || HELP_DEFAULT;
    input.setAttribute("aria-invalid", msg ? "true" : "false");
  }

  async function api(path) {
    const res = await fetch(path, { headers: { Accept: "application/json" } });
    const data = await res.json().catch(() => ({}));
    return { ok: res.ok, data };
  }

  async function search(query) {
    hideSuggestions();
    const problem = validate(query);
    if (problem) { setFieldError(problem); input.focus(); return; }
    setFieldError("");
    const seq = ++searchSeq;
    showState("loading");
    try {
      const { ok, data } = await api("/api/search?q=" + encodeURIComponent(query.trim()));
      if (seq !== searchSeq) return;
      if (!ok) return showError("We couldn’t search that", data.message || "Something went wrong. Please try again.");
      if (data.count === 0) return showNotFound(query.trim(), data.didYouMean || []);
      renderResults(query.trim(), data);
    } catch (e) {
      if (seq !== searchSeq) return;
      showError("Can’t reach the UNIGO server", "Please check that the server is running and refresh the page.");
    }
  }

  function renderResults(query, data) {
    const exact = data.results.length === 1 && data.results[0].exactMatch;
    $("resultsTitle").textContent = exact ? data.results[0].place : data.count + " matching destinations";
    $("resultsSub").textContent = exact ? "Here is the bus information for your destination." : "Results for “" + query + "”. Choose the place you are travelling to.";
    $("resultsGrid").innerHTML = data.results.map(card).join("");
    showState("results");
  }

  function card(r) {
    const shifts = r.shifts.map((s) => {
      const cls = !s.runs ? "shift off" : s.next ? "shift next-up" : "shift";
      return '<div class="' + cls + '"><span>' + esc(s.label) + "</span><b>" + esc(s.time) + "</b></div>";
    }).join("");
    const next = r.nextBus
      ? '<div class="next"><small>Next bus today</small><strong>' + esc(r.nextBus) + "</strong></div>"
      : '<div class="next none"><small>No more buses today</small><strong>All shifts have departed</strong></div>';
    return '<article class="card"><div class="card-top"><h4>' + esc(r.place) + "</h4>" +
      (r.exactMatch ? '<span class="badge">Exact match</span>' : "") + "</div>" +
      '<div class="board"><small>Board at</small><strong>' + esc(r.departureFrom) + "</strong></div>" +
      next + '<div class="shifts">' + shifts + "</div></article>";
  }

  function showError(title, text, tipsHtml) {
    $("errorTitle").textContent = title;
    $("errorText").textContent = text;
    $("errorTips").hidden = !tipsHtml;
    $("errorTips").innerHTML = tipsHtml || "";
    showState("error");
  }

  // `similar` = real place names the backend found close to a misspelled query.
  function showNotFound(query, similar) {
    const tips = similar.length
      ? '<span class="muted">Did you mean:</span>' + similar.map((p) => '<button type="button" class="tip" data-place="' + esc(p) + '">' + esc(p) + "</button>").join("")
      : "";
    showError("No bus found for “" + query + "”", "That place isn’t in the bus schedule. Check the spelling or try a shorter name.", tips);
  }

  function resetBus() {
    searchSeq++;
    input.value = "";
    clearBtn.hidden = true;
    setFieldError("");
    hideSuggestions();
    showState("idle");
  }
  function resetAndFocus() { resetBus(); input.focus(); }

  // autocomplete
  function hideSuggestions() {
    list.hidden = true; list.innerHTML = ""; activeIndex = -1;
    input.setAttribute("aria-expanded", "false");
    input.removeAttribute("aria-activedescendant");
  }
  function highlight(name, q) {
    const i = name.toLowerCase().indexOf(q.toLowerCase());
    if (i < 0) return esc(name);
    return esc(name.slice(0, i)) + "<mark>" + esc(name.slice(i, i + q.length)) + "</mark>" + esc(name.slice(i + q.length));
  }
  async function loadSuggestions(q) {
    const seq = ++suggestSeq;
    try {
      const { data } = await api("/api/suggest?q=" + encodeURIComponent(q));
      if (seq !== suggestSeq || input.value.trim() !== q) return;
      const items = data.suggestions || [];
      if (!items.length) return hideSuggestions();
      list.innerHTML = items.map((p, i) => '<li role="option" id="opt' + i + '" data-place="' + esc(p) + '">' + highlight(p, q) + "</li>").join("");
      list.hidden = false; activeIndex = -1;
      input.setAttribute("aria-expanded", "true");
    } catch (e) { hideSuggestions(); }
  }
  function moveActive(delta) {
    const items = list.querySelectorAll("li");
    if (!items.length) return;
    activeIndex = (activeIndex + delta + items.length) % items.length;
    items.forEach((li, i) => li.classList.toggle("active", i === activeIndex));
    input.setAttribute("aria-activedescendant", "opt" + activeIndex);
    items[activeIndex].scrollIntoView({ block: "nearest" });
  }
  function choose(place) { input.value = place; clearBtn.hidden = false; search(place); }

  $("searchForm").addEventListener("submit", (e) => {   // Enter key and the Search button both land here
    e.preventDefault();
    const active = list.querySelector("li.active");
    if (active) return choose(active.dataset.place);
    search(input.value);
  });
  input.addEventListener("input", () => {
    clearBtn.hidden = !input.value;
    if (help.classList.contains("bad")) setFieldError("");
    clearTimeout(suggestTimer);
    const q = input.value.trim();
    if (!q || !VALID.test(q)) return hideSuggestions();
    suggestTimer = setTimeout(() => loadSuggestions(q), 120);   // debounce keystrokes only
  });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown") { e.preventDefault(); moveActive(1); }
    else if (e.key === "ArrowUp") { e.preventDefault(); moveActive(-1); }
    else if (e.key === "Escape") hideSuggestions();
  });
  list.addEventListener("mousedown", (e) => {   // mousedown fires before the input loses focus
    const li = e.target.closest("li");
    if (li) { e.preventDefault(); choose(li.dataset.place); }
  });
  document.addEventListener("click", (e) => { if (!e.target.closest("#searchBox")) hideSuggestions(); });
  clearBtn.addEventListener("click", resetAndFocus);
  $("resetBtn").addEventListener("click", resetAndFocus);
  $("errorReset").addEventListener("click", resetAndFocus);
  $("errorTips").addEventListener("click", (e) => {
    const b = e.target.closest(".tip");
    if (b) choose(b.dataset.place);
  });

  route();
})();
