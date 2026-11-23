// lostfound.js -- Lost & Found page.
// Two tabs: home (choose Lost / Found -> step-by-step forms) and mine (claims on items I found, my posts with their
// possible matches, items I claimed). Plus desk (collection desk, admin only).
// A claim: the owner says "This is mine" -> the finder (who still has the item) approves or rejects it -> only after
// approving, the finder hands it in at the CSIT desk -> the owner collects it there and presses "I got it back".
// It uses the site login from the start page (roll number + password, see app.js). Photos are shrunk in the browser and
// re-saved as JPEG before upload, which also removes hidden location (GPS) data from phone photos.
(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const ICON = { phone: "📱", electronics: "🎧", bottle: "🥤", bag: "👜", wallet: "👛", idcard: "🪪", keys: "🔑",
                 books: "📚", clothing: "👕", jewellery: "⌚", other: "📦" };
  const DAY = { morning: "Morning", afternoon: "Afternoon", evening: "Evening" };
  const STATUS = {
    found: { open: ["Waiting for owner", "st-open"], pending: ["Someone says it’s theirs", "st-pending"], approved: ["Owner confirmed", "st-approved"],
             returned: ["Returned 🎉", "st-returned"], closed: ["Closed", "st-closed"], expired: ["Expired", "st-closed"] },
    lost:  { open: ["Still looking", "st-open"], closed: ["Closed", "st-closed"], expired: ["Expired", "st-closed"],
             pending: ["Still looking", "st-open"], approved: ["Still looking", "st-open"], returned: ["Returned 🎉", "st-returned"] },
  };
  const CLAIM_STATUS = {
    pending: ["Waiting for the finder to check", "st-pending"], approved: ["Approved – finder is taking it to the desk", "st-approved"],
    atdesk: ["Ready to collect at the desk", "st-approved"], rejected: ["Not approved", "st-closed"], returned: ["Got it back 🎉", "st-returned"],
  };
  // The 4 steps of a claim, shown as a progress bar to both students.
  const CLAIM_STEPS = ["Claim sent", "Finder approves", "At the CSIT desk", "Got it back"];
  function claimSteps(status) {
    const at = { pending: 1, approved: 2, atdesk: 3, returned: 4 }[status];
    if (status === "rejected") return '<ol class="lf-track lf-track-no"><li class="done">Claim sent</li><li class="no">Not approved</li></ol>';
    return '<ol class="lf-track">' + CLAIM_STEPS.map((s, i) => '<li class="' + (i < at ? "done" : i === at ? "current" : "") + '">' + s + "</li>").join("") + "</ol>";
  }
  const CLOSE_REASONS = { "found-myself": "I found it myself", "owner-collected": "The owner has it back", "no-longer-needed": "No longer needed" };

  let meta = null, me = null, screen = "home", params = new URLSearchParams();
  let renderSeq = 0, placeBox = null;
  const matchWarn = {};            // found post id -> date warning from "possible matches" (shown before claiming)
  const matchFrom = {};            // found post id -> my lost post whose matches showed it (sent with the claim)

  // ---------- server ----------
  async function api(path, body) {
    const opts = body === undefined ? { headers: { Accept: "application/json" } }
      : { method: "POST", headers: { "Content-Type": "application/json", Accept: "application/json" }, body: JSON.stringify(body) };
    const res = await fetch(path, opts);
    const data = await res.json().catch(() => ({}));
    if (res.status === 401 && window.UnigoApp) window.UnigoApp.sessionEnded();   // login expired: back to the log in page
    return { ok: res.ok, status: res.status, data };
  }
  const offline = '<div class="state error"><h3>Can’t reach the UNIGO server</h3><p>Please check that the server is running and refresh the page.</p></div>';

  // ---------- small pieces ----------
  const catLabel = (k) => { const c = meta && meta.categories.find((x) => x.key === k); return c ? c.label : k; };
  function placeText(p) {
    if (p.place.unknown) return "Place not remembered";
    return p.place.name || p.place.text || "";
  }
  function whenText(p) {
    if (p.timeUnknown || !p.date) return "Time not remembered";
    const d = new Date(p.date + "T00:00:00");
    const nice = isNaN(d) ? p.date : d.toLocaleDateString(undefined, { day: "numeric", month: "short", year: "numeric" });
    return nice + (p.dayPart ? ", " + (DAY[p.dayPart] || p.dayPart).toLowerCase() : "");
  }
  function statusBadge(p) {
    const s = (STATUS[p.type] || STATUS.found)[p.status] || [p.status, "st-closed"];
    return '<span class="lf-status ' + s[1] + '">' + esc(s[0]) + "</span>";
  }
  function photoHtml(p, cls) {
    return p.photos.length ? '<img class="' + cls + '" src="' + esc(p.photos[0]) + '" alt="Photo of the ' + esc(catLabel(p.category).toLowerCase()) + '" loading="lazy">'
                           : '<div class="' + cls + ' lf-nophoto" aria-hidden="true">' + (ICON[p.category] || "📦") + "</div>";
  }
  function postCard(p, extra) {
    return '<article class="lf-card lf-' + p.type + '">' +
      '<button type="button" class="lf-card-open" data-act="open" data-id="' + p.id + '" aria-label="Open details: ' + esc(catLabel(p.category)) + '">' +
      photoHtml(p, "lf-card-photo") +
      '<div class="lf-card-body"><div class="lf-card-top"><span class="lf-cat">' + (ICON[p.category] || "") + " " + esc(catLabel(p.category)) + "</span>" + statusBadge(p) + "</div>" +
      '<p class="lf-desc">' + esc(p.description) + "</p>" +
      '<p class="lf-meta"><span>📍 ' + esc(placeText(p)) + "</span><span>🕒 " + esc(whenText(p)) + "</span></p></div></button>" +
      (extra || "") + "</article>";
  }
  function setScreenHtml(html) { $("lfScreen").innerHTML = html; }
  function loading(text) { setScreenHtml('<div class="state"><div class="spinner" aria-hidden="true"></div><p>' + esc(text || "Loading…") + "</p></div>"); }

  // Forget unfinished posts (called by app.js when someone logs out, so the next person starts clean).
  function reset() { drafts = {}; me = null; }

  // ---------- page entry (app.js only opens this page for a logged-in student) ----------
  async function show(sub, p) {
    params = p || new URLSearchParams();
    me = window.UnigoApp ? window.UnigoApp.me() : null;
    if (!me) return;
    const my = ++renderSeq;
    if (!meta) {
      const m = await api("/api/lostfound/meta").catch(() => null);
      if (my !== renderSeq) return;
      if (!m || !m.ok) { setScreenHtml(offline); return; }
      meta = m.data;
    }
    render(sub);
  }
  function render(sub) {
    screen = sub;
    $("lfAccount").hidden = false;
    $("lfWho").innerHTML = (me.role === "admin" ? "🛎️ Collection desk" : "🎓 Roll no. " + esc(me.id));
    $("lfDeskTab").hidden = me.role !== "admin";
    const tab = sub === "lost" || sub === "found" ? "home" : sub;
    document.querySelectorAll(".lf-tabs a").forEach((a) => {
      const on = a.dataset.tab === tab;
      a.classList.toggle("active", on);
      if (on) a.setAttribute("aria-current", "page"); else a.removeAttribute("aria-current");
    });
    if (sub !== "mine") refreshBadge();
    if (sub === "lost" || sub === "found") return startWizard(sub);
    if (sub === "mine") return renderMine();
    if (sub === "desk") return me.role === "admin" ? renderDesk() : setScreenHtml('<div class="state error"><h3>Desk only</h3><p>Only the collection desk can open this page.</p></div>');
    renderHome();
  }

  // ---------- start screen ----------
  function renderHome() {
    setScreenHtml(
      '<h3 class="lf-h3 lf-ask">Did you lose something, or find something?</h3>' +
      '<div class="lf-choose">' +
        '<a class="lf-big lf-big-lost" href="#/lostfound/lost"><span class="lf-big-icon" aria-hidden="true">😟</span>' +
          "<h3>I lost something</h3><p>Tell us what it looks like. We’ll show you found items that might be yours.</p>" +
          '<span class="tool-go">Report a lost item →</span></a>' +
        '<a class="lf-big lf-big-found" href="#/lostfound/found"><span class="lf-big-icon" aria-hidden="true">🙌</span>' +
          "<h3>I found something</h3><p>Add a photo and say where you found it. Keep it until the owner’s description matches, then hand it in at the desk.</p>" +
          '<span class="tool-go">Post a found item →</span></a>' +
      "</div>" +
      '<p class="lf-simple-how">📍 Approved items are collected at <b>' + esc(meta.collectionPoint.name) + "</b>. Everything you post, and every claim, is in <a href=\"#/lostfound/mine\">My posts</a>.</p>");
  }

  // Red number on the "My posts" tab = claims on my found items that wait for my answer.
  function setBadge(n) { const b = $("lfMineBadge"); if (b) { b.hidden = !n; b.textContent = n || ""; } }
  function refreshBadge() {
    if (!me || me.role === "admin") return setBadge(0);
    api("/api/lostfound/mine").then(({ ok, data }) => {
      if (ok) setBadge(data.incoming.filter((c) => c.status === "pending" || c.status === "approved").length);
    }).catch(() => {});
  }

  // ---------- step-by-step forms ----------
  let drafts = {};                 // one unfinished draft per type, so switching tabs doesn't lose work
  let wiz = null;                  // { type, steps, at }
  const STEPS = {
    lost: [["category", "What did you lose?"], ["details", "Describe it"], ["where", "Where did you lose it?"], ["when", "When did you lose it?"], ["review", "Check and post"]],
    found: [["category", "What did you find?"], ["details", "Add a photo"], ["where", "Where did you find it?"], ["when", "When did you find it?"], ["handin", "Keep it safe"]],
  };
  function blankDraft(type) {
    return { type, category: params.get("category") || "", description: "", photos: [], placeId: "", placeName: "", placeText: "",
             placeUnknown: false, date: meta.today, dayPart: "", timeUnknown: false, privateNote: "" };
  }
  function startWizard(type) {
    if (!drafts[type] || drafts[type].posted) drafts[type] = blankDraft(type);
    wiz = { type, steps: STEPS[type], at: 0 };
    renderStep(null);
  }
  function renderStep(direction) {
    const d = drafts[wiz.type], [key, title] = wiz.steps[wiz.at];
    const progress = wiz.steps.map((s, i) => '<li class="' + (i < wiz.at ? "done" : i === wiz.at ? "current" : "") + '"' + (i === wiz.at ? ' aria-current="step"' : "") + ">" + esc(s[1]) + "</li>").join("");
    const last = wiz.at === wiz.steps.length - 1;
    setScreenHtml(
      '<div class="lf-wizard lf-wizard-' + wiz.type + '">' +
        '<div class="lf-wizard-head"><span class="lf-pill">' + (wiz.type === "lost" ? "😟 Lost item" : "🙌 Found item") + "</span>" +
        '<a href="#/lostfound" class="btn-soft">Cancel</a></div>' +
        '<ol class="rv-progress lf-progress" aria-label="Steps">' + progress + "</ol>" +
        '<form id="lfStepForm" class="lf-step ' + (direction ? (direction === "back" ? "rv-in-back" : "rv-in-next") : "") + '" novalidate>' +
          '<fieldset class="rv-step"><legend>' + esc(title) + "</legend>" + stepBody(key, d) + "</fieldset>" +
          '<p class="field-error lf-error" id="lfStepError" role="alert"></p>' +
          '<div class="rv-nav">' + (wiz.at ? '<button type="button" class="btn-soft" data-go="back">Back</button>' : "<span></span>") +
          '<button type="submit" class="btn" id="lfNext">' + (last ? (wiz.type === "lost" ? "Post lost item" : "Post found item") : "Next") + "</button></div>" +
        "</form></div>");
    bindStep(key, d);
    const first = $("lfStepForm").querySelector("input:not([type=hidden]), textarea, select");
    if (direction && first) first.focus({ preventScroll: true });
  }

  function stepBody(key, d) {
    const lim = meta.limits, lost = d.type === "lost";
    if (key === "category") {
      return '<div class="lf-cats" role="radiogroup" aria-label="Category">' + meta.categories.map((c) =>
        '<label class="lf-cat-opt"><input type="radio" name="lfCat" value="' + c.key + '"' + (d.category === c.key ? " checked" : "") + ">" +
        '<span class="lf-cat-icon" aria-hidden="true">' + (ICON[c.key] || "📦") + "</span><span>" + esc(c.label) + "</span></label>").join("") + "</div>";
    }
    if (key === "details") {
      const photoNote = lost ? "Optional. Add up to " + lim.maxPhotos + " photos if you have any (for example an old photo of the item)."
                             : "Required. Add 1 or " + lim.maxPhotos + " clear photos of the item. On a phone you can take the photo now.";
      const descLabel = lost ? "Description <span class=\"muted\">(required)</span>" : "Short description <span class=\"muted\">(e.g. “black pouch”)</span>";
      const descHelp = lost ? "Brand, colour, size, stickers, what was inside… anything that helps." : "Keep it short. Don’t describe hidden details here: put them in the private note at the end.";
      const photos = '<div class="lf-photos">' +
        '<p class="rv-label">Photos ' + (lost ? '<span class="muted">(optional)</span>' : '<span class="muted">(required)</span>') + "</p>" +
        '<p class="rv-help">' + photoNote + "</p>" +
        '<div class="lf-photo-row" id="lfPhotoRow">' + photoTiles(d) + "</div>" +
        '<input type="file" id="lfFile" accept="image/*" multiple class="sr" tabindex="-1">' +
        '<p class="rv-help small" id="lfPhotoStatus" aria-live="polite"></p></div>';
      const desc = '<label for="lfDesc" class="rv-label">' + descLabel + "</label>" +
        '<textarea id="lfDesc" rows="' + (lost ? 4 : 2) + '" maxlength="' + (lost ? lim.lostDescMax : lim.foundDescMax) + '" placeholder="' +
        (lost ? "e.g. Blue steel water bottle, Milton, dent near the bottom, a Marvel sticker" : "e.g. Black pouch with a zip") + '">' + esc(d.description) + "</textarea>" +
        '<p class="rv-help">' + descHelp + ' <span class="rv-count" id="lfDescCount"></span></p>';
      return lost ? desc + photos : photos + desc;
    }
    if (key === "where") {
      return '<p class="rv-help">Choose a campus place, or type it in your own words (e.g. “CS Lab 3, second row”).</p>' +
        '<div class="search-box mini lf-place" id="lfPlaceBox">' +
          '<label for="lfPlace" class="sr">Place</label>' +
          '<input id="lfPlace" type="text" maxlength="80" autocomplete="off" placeholder="e.g. CSIT Block, Library, Ravi Canteen" value="' + esc(d.placeName || d.placeText) + '"' +
          ' role="combobox" aria-expanded="false" aria-controls="lfPlaceList" aria-autocomplete="list"' + (d.placeUnknown ? " disabled" : "") + ">" +
          '<button type="button" class="clear" id="lfPlaceClear" aria-label="Clear place"' + (d.placeName || d.placeText ? "" : " hidden") + ">&times;</button>" +
          '<ul class="suggestions" id="lfPlaceList" role="listbox" hidden></ul></div>' +
        '<p class="lf-chosen" id="lfPlaceChosen">' + placeChosenText(d) + "</p>" +
        (lost ? '<label class="rv-option lf-unknown"><input type="checkbox" id="lfPlaceUnknown"' + (d.placeUnknown ? " checked" : "") + '><span class="rv-tick" aria-hidden="true"></span>' +
          '<span class="rv-emoji" aria-hidden="true">🤷</span><span class="rv-opt-text"><b>I don’t remember where</b><small>That’s okay: matches will rely on the description and date.</small></span></label>' : "");
    }
    if (key === "when") {
      const parts = meta.dayParts.map((p) => '<label class="lf-chip"><input type="radio" name="lfDayPart" value="' + p + '"' + (d.dayPart === p ? " checked" : "") +
        (d.timeUnknown ? " disabled" : "") + "><span>" + (p === "morning" ? "🌅 " : p === "afternoon" ? "☀️ " : "🌙 ") + DAY[p] + "</span></label>").join("");
      return '<label for="lfDate" class="rv-label">Date</label>' +
        '<input id="lfDate" type="date" class="lf-date" max="' + meta.today + '" value="' + esc(d.date) + '"' + (d.timeUnknown ? " disabled" : "") + ">" +
        '<p class="rv-label">Part of the day</p><div class="lf-chips" role="radiogroup" aria-label="Part of the day">' + parts + "</div>" +
        (d.type === "lost" ? '<label class="rv-option lf-unknown"><input type="checkbox" id="lfTimeUnknown"' + (d.timeUnknown ? " checked" : "") + '><span class="rv-tick" aria-hidden="true"></span>' +
          '<span class="rv-emoji" aria-hidden="true">🤷</span><span class="rv-opt-text"><b>I don’t remember when</b><small>Matches will not use the date.</small></span></label>' : "");
    }
    if (key === "handin") {
      const cp = meta.collectionPoint, sens = meta.categories.find((c) => c.key === d.category && c.sensitive);
      return '<div class="lf-handin"><span class="lf-handin-icon" aria-hidden="true">🤲</span><div><b>Keep the item with you for now.</b>' +
          "<p>Don’t hand it in yet. When the owner’s description matches and you approve it, take it to <b>" + esc(cp.name) + "</b>. " +
          "The desk gives it only to the student you approved.</p>" +
          (cp.placeId ? '<a href="#/map/route?to=' + encodeURIComponent(cp.placeId) + '" target="_blank" rel="noopener">Show the desk on the Campus Map ↗</a>' : "") + "</div></div>" +
        (sens ? '<p class="lf-warn">⚠️ ' + esc(catLabel(d.category)) + " items hold personal data or money. Keep it safe and don’t use or open it" +
          (d.category === "idcard" ? ", and don’t photograph the personal details on the card." : ".") + "</p>" : "") +
        '<label for="lfNote" class="rv-label">Secret detail <span class="muted">(optional, only you and the desk see it)</span></label>' +
        '<textarea id="lfNote" rows="2" maxlength="' + meta.limits.noteMax + '" placeholder="e.g. Library card inside; small scratch on the back">' + esc(d.privateNote) + "</textarea>" +
        '<p class="rv-help">Optional. If you noticed something that isn’t in the photo, write it here as a reminder for checking claims. You can leave it empty.</p>' + summary(d);
    }
    return summary(d);                                          // review (lost)
  }
  function summary(d) {
    return '<div class="lf-summary"><p class="rv-label">Your post will look like this</p>' +
      postCard({ id: 0, type: d.type, category: d.category, description: d.description, photos: d.photos, status: "open",
                 place: d.placeUnknown ? { unknown: true } : { name: d.placeName, text: d.placeText }, date: d.date, dayPart: d.dayPart, timeUnknown: d.timeUnknown }) +
      '<p class="rv-help">Your roll number is not shown to anyone except the collection desk.</p></div>';
  }
  function placeChosenText(d) {
    if (d.placeUnknown) return "";
    if (d.placeId) return "✓ Campus place: <b>" + esc(d.placeName) + "</b>";
    if (d.placeText) return "✓ In your words: <b>" + esc(d.placeText) + "</b>";
    return "";
  }
  function photoTiles(d) {
    const tiles = d.photos.map((src, i) => '<div class="lf-photo"><img src="' + src + '" alt="Photo ' + (i + 1) + '">' +
      '<button type="button" class="lf-photo-x" data-remove="' + i + '" aria-label="Remove photo ' + (i + 1) + '">&times;</button></div>').join("");
    const add = d.photos.length < meta.limits.maxPhotos
      ? '<button type="button" class="lf-photo-add" id="lfAddPhoto"><span aria-hidden="true">📷</span>Add photo</button>' : "";
    return tiles + add;
  }

  function bindStep(key, d) {
    const form = $("lfStepForm");
    form.addEventListener("submit", (e) => { e.preventDefault(); next(); });
    form.addEventListener("click", (e) => { if (e.target.closest("[data-go=back]")) { save(key, d); wiz.at--; renderStep("back"); } });
    if (key === "category") form.addEventListener("change", () => { const c = form.querySelector("input[name=lfCat]:checked"); if (c) { d.category = c.value; error(""); } });
    if (key === "details") {
      const counter = () => { $("lfDescCount").textContent = Array.from($("lfDesc").value).length + " / " + $("lfDesc").maxLength; };
      $("lfDesc").addEventListener("input", () => { d.description = $("lfDesc").value; counter(); error(""); });
      counter();
      $("lfPhotoRow").addEventListener("click", (e) => {
        if (e.target.closest("#lfAddPhoto")) $("lfFile").click();
        const x = e.target.closest("[data-remove]");
        if (x) { d.photos.splice(Number(x.dataset.remove), 1); $("lfPhotoRow").innerHTML = photoTiles(d); }
      });
      $("lfFile").addEventListener("change", () => addPhotos($("lfFile").files, d));
    }
    if (key === "where") {
      placeBox = window.UnigoSearch.attach({
        input: $("lfPlace"), list: $("lfPlaceList"), box: $("lfPlaceBox"), clearBtn: $("lfPlaceClear"), suggestUrl: "/api/places/suggest?q=",
        onChoose: (it) => { d.placeId = it.id; d.placeName = it.label; d.placeText = ""; $("lfPlaceChosen").innerHTML = placeChosenText(d); error(""); },
        onSubmit: () => next(),
        onClear: () => { d.placeId = ""; d.placeName = ""; d.placeText = ""; $("lfPlaceChosen").innerHTML = ""; },
        onType: () => { d.placeId = ""; d.placeName = ""; d.placeText = $("lfPlace").value.trim(); $("lfPlaceChosen").innerHTML = placeChosenText(d); error(""); },
      });
      const unk = $("lfPlaceUnknown");
      if (unk) unk.addEventListener("change", () => {
        d.placeUnknown = unk.checked; $("lfPlace").disabled = unk.checked;
        $("lfPlaceChosen").innerHTML = placeChosenText(d); error("");
      });
    }
    if (key === "when") {
      $("lfDate").addEventListener("change", () => { d.date = $("lfDate").value; error(""); });
      form.addEventListener("change", (e) => { if (e.target.name === "lfDayPart") { d.dayPart = e.target.value; error(""); } });
      const unk = $("lfTimeUnknown");
      if (unk) unk.addEventListener("change", () => {
        d.timeUnknown = unk.checked; $("lfDate").disabled = unk.checked;
        form.querySelectorAll("input[name=lfDayPart]").forEach((r) => { r.disabled = unk.checked; }); error("");
      });
    }
    if (key === "handin") $("lfNote").addEventListener("input", () => { d.privateNote = $("lfNote").value; });
  }
  function save(key, d) {
    if (key === "details" && $("lfDesc")) d.description = $("lfDesc").value;
    if (key === "handin" && $("lfNote")) d.privateNote = $("lfNote").value;
  }
  function error(msg) { const e = $("lfStepError"); if (e) e.textContent = msg; }

  function checkStep(key, d) {
    const lim = meta.limits, lost = d.type === "lost", len = (s) => Array.from(s.trim()).length;
    if (key === "category" && !d.category) return "Please choose a category.";
    if (key === "details") {
      if (!lost && !d.photos.length) return "Please add at least one photo of the item you found.";
      const min = lost ? lim.lostDescMin : lim.foundDescMin, max = lost ? lim.lostDescMax : lim.foundDescMax;
      if (len(d.description) < min) return lost ? "Please describe your item in at least " + min + " characters." : "Please add a short description.";
      if (len(d.description) > max) return "Please keep the description under " + max + " characters.";
    }
    if (key === "where" && !d.placeUnknown && !d.placeId && len(d.placeText) < 3)
      return lost ? "Choose a place, type where it was, or tick “I don’t remember where”." : "Please say where you found it.";
    if (key === "when" && !d.timeUnknown) {
      if (!d.date) return "Please choose the date.";
      if (d.date > meta.today) return "The date can’t be in the future.";
      if (!d.dayPart) return "Please choose morning, afternoon or evening.";
    }
    return "";
  }
  function next() {
    const d = drafts[wiz.type], key = wiz.steps[wiz.at][0];
    save(key, d);
    const problem = checkStep(key, d);
    if (problem) return error(problem);
    if (wiz.at < wiz.steps.length - 1) { wiz.at++; return renderStep("next"); }
    submitPost(d);
  }
  async function submitPost(d) {
    for (let i = 0; i < wiz.steps.length; i++) {           // check every step again before sending
      const problem = checkStep(wiz.steps[i][0], d);
      if (problem) { wiz.at = i; renderStep("back"); return error(problem); }
    }
    const btn = $("lfNext");
    btn.disabled = true; btn.textContent = "Posting…";
    const body = { type: d.type, category: d.category, description: d.description, photos: d.photos,
                   placeId: d.placeUnknown ? "" : d.placeId, placeText: d.placeUnknown || d.placeId ? "" : d.placeText, placeUnknown: d.placeUnknown,
                   date: d.timeUnknown ? "" : d.date, dayPart: d.timeUnknown ? "" : d.dayPart, timeUnknown: d.timeUnknown, privateNote: d.privateNote };
    try {
      const { ok, status, data } = await api("/api/lostfound/posts", body);
      if (!ok) {
        if (status === 401) return;                 // api() already sent the student to the log in page
        const at = { category: 0, description: 1, photos: 1, place: 2, date: 3, dayPart: 3, privateNote: 4 }[data.field];
        if (at !== undefined && at !== wiz.at) { wiz.at = at; renderStep("back"); }
        error(data.message || "Your post could not be saved. Please try again.");
        return;
      }
      d.posted = true;
      renderPosted(data.post);
    } catch (e) { error("Can’t reach the UNIGO server. Please try again."); }
    finally { if ($("lfNext")) { $("lfNext").disabled = false; $("lfNext").textContent = d.type === "lost" ? "Post lost item" : "Post found item"; } }
  }
  function renderPosted(p) {
    const lost = p.type === "lost";
    setScreenHtml('<div class="lf-done rv-in-next"><span class="rv-done-icon" aria-hidden="true">' + (lost ? "📣" : "🙏") + "</span>" +
      "<h3>" + (lost ? "Your lost item is posted" : "Thank you for helping!") + "</h3>" +
      (lost ? '<p class="muted">We’ll compare it with found items. Check <b>My posts</b> for possible matches.</p>' +
              '<div class="lf-done-actions"><button type="button" class="btn" data-act="matches" data-id="' + p.id + '">See possible matches</button>' +
              '<a class="btn-soft" href="#/lostfound/mine">Go to My posts</a></div>'
            : '<p class="muted"><b>Keep the item safe with you for now.</b></p>' +
              '<ol class="lf-next-steps"><li>When someone says it’s theirs, you’ll see their description in <b>My posts</b>.</li>' +
              "<li>If it matches the item, press <b>“Yes, it’s theirs”</b>.</li>" +
              "<li>Then hand it in at <b>📍 " + esc(p.collectionPoint.name) + "</b>. The desk gives it only to the student you approved.</li></ol>" +
              '<p class="rv-help">Your roll number stays private.</p>' +
              '<a class="btn-soft" href="#/lostfound/mine">Go to My posts</a>') +
      '<p><a href="#/lostfound">Back to start</a></p></div>');
  }

  // ---------- photos: shrink and re-save as JPEG in the browser ----------
  async function addPhotos(files, d) {
    const status = $("lfPhotoStatus"), max = meta.limits.maxPhotos;
    const list = Array.from(files || []).slice(0, max - d.photos.length);
    if (!list.length) return;
    status.textContent = "Preparing photo…";
    for (const f of list) {
      try { d.photos.push(await shrinkPhoto(f, meta.limits.photoMaxBytes)); status.textContent = ""; }
      catch (e) { status.textContent = e.message || "That photo could not be used."; }
    }
    $("lfFile").value = "";
    if ($("lfPhotoRow")) $("lfPhotoRow").innerHTML = photoTiles(d);
    error("");
  }
  function shrinkPhoto(file, maxBytes) {
    return new Promise((resolve, reject) => {
      if (!/^image\//.test(file.type)) return reject(new Error("Please choose an image file."));
      const url = URL.createObjectURL(file), img = new Image();
      img.onload = () => {
        URL.revokeObjectURL(url);
        let scale = Math.min(1, 1280 / Math.max(img.naturalWidth, img.naturalHeight));
        for (let round = 0; round < 6; round++) {
          const c = document.createElement("canvas");
          c.width = Math.max(1, Math.round(img.naturalWidth * scale)); c.height = Math.max(1, Math.round(img.naturalHeight * scale));
          const g = c.getContext("2d");
          g.fillStyle = "#fff"; g.fillRect(0, 0, c.width, c.height);             // transparent PNGs get a white background
          g.drawImage(img, 0, 0, c.width, c.height);
          for (const q of [0.85, 0.75, 0.65]) {
            const data = c.toDataURL("image/jpeg", q);                          // a new JPEG: no GPS / camera data inside
            if ((data.length - 23) * 0.75 <= maxBytes) return resolve(data);
          }
          scale *= 0.75;
        }
        reject(new Error("That photo is too large even after shrinking. Try another one."));
      };
      img.onerror = () => { URL.revokeObjectURL(url); reject(new Error("This photo format can’t be opened here. Please use a JPG or PNG photo.")); };
      img.src = url;
    });
  }

  // Posts seen so far, by id (cards open their details window from here).
  const lastPosts = {};

  // ---------- details window ----------
  function openPost(p) {
    if (!p || !p.id) return;                                    // e.g. the preview card while posting
    lastPosts[p.id] = p;
    const found = p.type === "found", student = me.role !== "admin", can = ["open", "pending"].indexOf(p.status) >= 0;
    const gallery = p.photos.length ? '<div class="lf-gallery">' + p.photos.map((src, i) => '<img src="' + esc(src) + '" alt="Photo ' + (i + 1) + ' of the item">').join("") + "</div>"
                                    : '<div class="lf-gallery lf-nophoto-big" aria-hidden="true">' + (ICON[p.category] || "📦") + "</div>";
    let action = "";
    if (p.mine) {
      action = '<p class="lf-mine-note">This is your post.' + (p.privateNote ? " Secret detail: <i>" + esc(p.privateNote) + "</i>" : "") +
        (found && p.status === "pending" ? ' Someone says it’s theirs: answer in <a href="#/lostfound/mine" data-act="close-dialog">My posts</a>.' : "") + "</p>" +
        (can && p.status !== "approved" ? '<div class="lf-close"><p class="rv-label">Close this post</p>' + Object.keys(CLOSE_REASONS)
          .filter((k) => found ? k !== "found-myself" : k !== "owner-collected").map((k) =>
            '<button type="button" class="btn-soft" data-act="close" data-id="' + p.id + '" data-reason="' + k + '">' + esc(CLOSE_REASONS[k]) + "</button>").join(" ") + "</div>" : "");
    } else if (found && student && can) {
      action = (matchWarn[p.id] ? '<p class="lf-warn small">⚠️ ' + esc(matchWarn[p.id]) + "</p>" : "") + '<form class="lf-claim" id="lfClaimForm" data-id="' + p.id + '" novalidate><p class="rv-label">Is this yours?</p>' +
        '<label for="lfClaimText" class="rv-help">Describe your item in your own words: colour, brand, size, marks, what was inside, where and when you lost it… anything you remember. No secret detail is needed. The student who found it reads this and approves it if it matches.</label>' +
        '<textarea id="lfClaimText" rows="3" maxlength="' + meta.limits.claimMax + '"></textarea>' +
        '<p class="field-error" id="lfClaimError" role="alert"></p><button type="submit" class="btn">This is mine</button></form>';
    } else if (!found && student) {
      action = '<p class="rv-help">Did you find this? Post it as a found item and hand it in at the desk.</p>' +
        '<a class="btn" href="#/lostfound/found?category=' + encodeURIComponent(p.category) + '" data-act="close-dialog">🙌 I found this</a>';
    }
    const report = !p.mine && student ? '<button type="button" class="lf-report" data-act="report" data-id="' + p.id + '">⚑ Report this post</button>' : "";
    dialog('<div class="lf-detail">' + gallery + '<div class="lf-detail-body">' +
      '<div class="lf-card-top"><span class="lf-cat">' + (ICON[p.category] || "") + " " + esc(p.categoryLabel || catLabel(p.category)) + "</span>" + statusBadge(p) + "</div>" +
      '<h3 id="lfDialogTitle">' + (found ? "Found: " : "Lost: ") + esc(p.categoryLabel || catLabel(p.category)) + "</h3>" +
      '<p class="lf-detail-desc">' + esc(p.description) + "</p>" +
      '<dl class="room-facts"><div><dt>Where</dt><dd>' + esc(placeText(p)) + "</dd></div><div><dt>When</dt><dd>" + esc(whenText(p)) + "</dd></div>" +
      (found ? "<div><dt>Collect from</dt><dd>" + esc(p.collectionPoint.name) + "</dd></div>" : "") + "<div><dt>Posted</dt><dd>" + esc(p.postedOn) + "</dd></div></dl>" +
      action + report + "</div></div>");
  }
  function dialog(html) {
    const dlg = $("lfDialog");
    $("lfDialogBody").innerHTML = '<button type="button" class="lf-dialog-x" data-act="close-dialog" aria-label="Close">&times;</button>' + html;
    if (!dlg.open) dlg.showModal();
  }
  function closeDialog() { const d = $("lfDialog"); if (d.open) d.close(); }

  async function claim(form) {
    const text = $("lfClaimText").value.trim(), err = $("lfClaimError");
    if (Array.from(text).length < meta.limits.claimMin) { err.textContent = "Please describe your item in at least " + meta.limits.claimMin + " characters."; return; }
    const warning = matchWarn[Number(form.dataset.id)];
    if (warning && !confirm(warning + "\n\nAre you sure this is yours? Press OK to send the claim.")) return;
    const btn = form.querySelector("button[type=submit]");
    btn.disabled = true;
    try {
      const postId = Number(form.dataset.id);
      const { ok, data } = await api("/api/lostfound/claims", { postId, description: text, lostPostId: matchFrom[postId] || 0 });
      if (!ok) { err.textContent = data.message || "Your claim could not be sent."; return; }
      $("lfDialogBody").innerHTML = '<div class="lf-done"><span class="rv-done-icon" aria-hidden="true">📨</span><h3 id="lfDialogTitle">Claim sent</h3>' +
        claimSteps("pending") +
        '<p class="muted">The student who found it will compare your description with the item. If they approve, they hand it in at the desk. Follow it in <b>My posts</b>.</p>' +
        "<p>When it says <b>Ready to collect</b>, go to:<br><b>📍 " + esc(data.collectionPoint) + "</b><br>Take your college ID card with you.</p>" +
        '<div class="lf-done-actions"><a class="btn" href="#/lostfound/mine" data-act="close-dialog">Go to My posts</a>' +
        '<button type="button" class="btn-soft" data-act="close-dialog">Close</button></div></div>';
      if (screen === "mine") renderMine();               // the claim shows up behind the window at once
    } catch (e) { err.textContent = "Can’t reach the UNIGO server."; }
    finally { btn.disabled = false; }
  }

  // ---------- my posts, matches and claims ----------
  async function renderMine() {
    loading("Loading your posts…");
    const my = renderSeq;
    try {
      const { ok, status, data } = await api("/api/lostfound/mine");
      if (my !== renderSeq) return;
      if (status === 401) return;                 // api() already sent the student to the log in page
      if (!ok) return setScreenHtml(offline);
      data.posts.forEach((p) => { lastPosts[p.id] = p; });
      const waiting = data.incoming.filter((c) => c.status === "pending" || c.status === "approved");
      setBadge(waiting.length);

      // 1. Claims on items I found: I decide (the finder has seen the item, so they can compare).
      const incoming = data.incoming.filter((c) => ["pending", "approved", "atdesk"].indexOf(c.status) >= 0).map((c) => {
        const p = c.post, st = c.status, needs = st === "pending" || st === "approved";
        const badge = { pending: ["Your answer is needed", "st-pending"], approved: ["Take it to the desk", "st-pending"], atdesk: ["At the desk", "st-approved"] }[st];
        return '<div class="lf-claim-row lf-incoming' + (needs ? " lf-needs" : "") + '"><div>' + photoHtml(p, "lf-claim-photo") + "</div><div>" +
          "<b>" + (ICON[p.category] || "") + " " + esc(p.categoryLabel) + " you found</b>" +
          '<span class="lf-status ' + badge[1] + '">' + badge[0] + "</span>" + claimSteps(st) +
          '<p class="lf-said">Someone says it’s theirs and describes it as:<br>“' + esc(c.description) + "”</p>" +
          (p.privateNote ? '<p class="small muted">Your secret detail: <i>' + esc(p.privateNote) + "</i></p>" : "") +
          (st === "pending"
            ? '<p class="small">Look at the item you have. Does this description match it? Only say yes if you’re sure.</p><div class="lf-desk-actions">' +
              '<button type="button" class="btn" data-act="decide" data-id="' + c.id + '" data-decision="approve">✓ Yes, it’s theirs</button>' +
              '<button type="button" class="btn-soft" data-act="decide" data-id="' + c.id + '" data-decision="reject">✗ No, it doesn’t match</button></div>'
          : st === "approved"
            ? '<p class="small"><b>Now take the item to 📍 ' + esc(p.collectionPoint.name) + "</b> and press the button when you have handed it in.</p>" +
              '<div class="lf-desk-actions"><button type="button" class="btn" data-act="decide" data-id="' + c.id + '" data-decision="atdesk">📦 I handed it in at the desk</button>' +
              '<button type="button" class="btn-soft" data-act="decide" data-id="' + c.id + '" data-decision="reject">Undo: not theirs</button></div>'
            : '<p class="small">Thank you! The desk will give it only to the student you approved, after checking their college ID.</p>') +
          "</div></div>";
      }).join("");

      // 2. My posts.
      const posts = data.posts.map((p) => postCard(p, p.type === "lost" && p.status === "open"
        ? '<button type="button" class="lf-match-btn' + (p.matchCount ? " lf-has-match" : "") + '" data-act="matches" data-id="' + p.id + '">' +
          (p.matchCount ? "✨ " + p.matchCount + " possible match" + (p.matchCount > 1 ? "es" : "") + " – have a look" : "🔍 No matches yet – check again later") + "</button>" : "")).join("");

      // 3. Items I said were mine, with a progress bar.
      const claims = data.claims.map((c) => {
        const s = CLAIM_STATUS[c.status] || [c.status, "st-closed"], p = c.post;
        return '<div class="lf-claim-row"><div>' + (p ? photoHtml(p, "lf-claim-photo") : "") + "</div><div><b>" + (p ? (ICON[p.category] || "") + " " + esc(p.categoryLabel) : "Item") + "</b>" +
          '<span class="lf-status ' + s[1] + '">' + esc(s[0]) + "</span>" + claimSteps(c.status) +
          '<p class="small muted">Your description: “' + esc(c.description) + "” · " + esc(c.claimedOn) + "</p>" +
          (c.status === "pending" ? '<p class="small">The student who found it hasn’t answered yet. Check back here later.</p>' : "") +
          (c.status === "approved" ? '<p class="small">The finder agreed it’s yours 🎉 They will now hand it in at the desk. This page will say <b>Ready to collect</b> when it’s there.</p>' : "") +
          (c.status === "atdesk" && p ? '<p class="small"><b>Collect it from 📍 ' + esc(p.collectionPoint.name) + "</b>. Take your college ID card: the desk checks your roll number.</p>" +
            '<div class="lf-desk-actions"><button type="button" class="btn" data-act="decide" data-id="' + c.id + '" data-decision="returned">🎉 I got it back</button></div>' : "") +
          (c.status === "rejected" ? '<p class="small">The finder said your description doesn’t match this item. Keep checking “See possible matches”.</p>' : "") +
          "</div></div>";
      }).join("");

      setScreenHtml(
        (incoming ? '<h3 class="lf-h3">Someone says your found item is theirs</h3><div class="lf-claims">' + incoming + "</div>" : "") +
        '<h3 class="lf-h3">My posts</h3>' +
        (posts ? '<div class="lf-grid">' + posts + "</div>" : '<div class="state lf-empty"><span aria-hidden="true">🗂️</span><h3>No posts yet</h3><p>Your lost and found posts will appear here.</p></div>') +
        '<h3 class="lf-h3">Items I said are mine</h3>' +
        (claims ? '<div class="lf-claims">' + claims + "</div>" : '<p class="muted">You haven’t claimed any item. Open a found item and press “This is mine”.</p>'));
    } catch (e) { setScreenHtml(offline); }
  }
  async function showMatches(id) {
    dialog('<div class="state"><div class="spinner" aria-hidden="true"></div><p id="lfDialogTitle">Looking for matches…</p></div>');
    try {
      const { ok, data } = await api("/api/lostfound/matches?post=" + id);
      if (!ok) { $("lfDialogBody").innerHTML = '<p class="field-error">' + esc(data.message || "Could not load matches.") + "</p>"; return; }
      data.matches.forEach((m) => {
        lastPosts[m.post.id] = m.post; matchFrom[m.post.id] = id;
        if (m.warning) matchWarn[m.post.id] = m.warning; else delete matchWarn[m.post.id];
      });
      dialog('<h3 id="lfDialogTitle" class="lf-h3">Possible matches</h3>' +
        (data.matches.length
          ? '<p class="rv-help">Found items that look like yours, best first. The computer only suggests: if one is yours, open it and press “This is mine”.</p><div class="lf-matches">' +
            data.matches.map((m) => '<div class="lf-match lf-' + m.strength + '">' + postCard(m.post) +
              '<div><p class="lf-why"><b>' + (m.strength === "strong" ? "🌟 Strong match" : "Possible match") + ":</b> " + m.reasons.map(esc).join(" · ") + "</p>" +
              (m.warning ? '<p class="lf-warn small">⚠️ ' + esc(m.warning) + "</p>" : "") + "</div></div>").join("") + "</div>"
          : '<div class="state lf-empty"><span aria-hidden="true">🔍</span><h3>No matches yet</h3><p>Nobody has posted a found item like yours so far. Check <b>My posts</b> again later: the number on your post updates by itself.</p>' +
            '<p class="small">Found items that you posted yourself are never shown as matches.</p>' +
            '<button type="button" class="btn-soft" data-act="close-dialog">Close</button></div>'));
    } catch (e) { $("lfDialogBody").innerHTML = offline; }
  }

  // ---------- collection desk ----------
  async function renderDesk() {
    loading("Loading the desk…");
    const my = renderSeq;
    try {
      const { ok, status, data } = await api("/api/lostfound/admin");
      if (my !== renderSeq) return;
      if (status === 401) return;                 // api() already sent the student to the log in page
      if (!ok) return setScreenHtml('<div class="state error"><p>' + esc(data.message || "Could not load the desk.") + "</p></div>");
      const s = data.stats;
      const pct = s.found ? Math.round((s.returnedIn30 / s.found) * 100) : 0;
      const hours = s.avgHoursToReturn;
      const tiles = '<div class="lf-stats"><div><b>' + s.returned + "</b><span>items returned</span></div>" +
        "<div><b>" + pct + "%</b><span>of found items returned within 30 days</span></div>" +
        "<div><b>" + (s.returned ? (hours < 48 ? hours.toFixed(1) + " h" : (hours / 24).toFixed(1) + " days") : "–") + "</b><span>average time to return</span></div>" +
        "<div><b>" + s.waiting + "</b><span>found items waiting</span></div><div><b>" + s.lost + "</b><span>lost reports</span></div></div>";
      data.claims.concat(data.flagged, data.waiting).forEach((x) => { const p = x.post || x; if (p) lastPosts[p.id] = p; });
      const claims = data.claims.map((c) => {
        const p = c.post;
        return '<div class="lf-desk-claim"><div class="lf-desk-item">' + (p ? photoHtml(p, "lf-claim-photo") : "") + "<div><b>" + (p ? (ICON[p.category] || "") + " " + esc(p.categoryLabel) + " · " + esc(p.description) : "") + "</b>" +
          (p ? '<p class="small muted">Found at ' + esc(placeText(p)) + " · " + esc(whenText(p)) + " · finder " + esc(p.owner) + "</p>" +
               '<p class="small">🔒 Private note: ' + (p.privateNote ? "<i>" + esc(p.privateNote) + "</i>" : '<span class="muted">none</span>') + "</p>" : "") + "</div></div>" +
          '<div class="lf-desk-who"><p><b>' + (c.status === "pending" ? "Claim by" : "Give only to") + " roll no. " + esc(c.user) + "</b> · " + esc(c.claimedOn) +
          ' <span class="lf-status ' + (CLAIM_STATUS[c.status] || ["", "st-pending"])[1] + '">' +
          esc({ pending: "Finder hasn’t answered", approved: "Approved, finder bringing it", atdesk: "Item is at the desk" }[c.status] || c.status) + "</span></p>" +
          "<p>“" + esc(c.description) + "”</p><div class=\"lf-desk-actions\">" +
          (c.status === "pending" ? '<button type="button" class="btn" data-act="decide" data-id="' + c.id + '" data-decision="approve">Approve</button>' : "") +
          (c.status === "approved" ? '<button type="button" class="btn-soft" data-act="decide" data-id="' + c.id + '" data-decision="atdesk">Item received from finder</button>' : "") +
          (c.status === "approved" || c.status === "atdesk" ? '<button type="button" class="btn" data-act="decide" data-id="' + c.id + '" data-decision="returned">Handed to owner ✓</button>' : "") +
          '<button type="button" class="btn-soft" data-act="decide" data-id="' + c.id + '" data-decision="reject">Reject</button></div></div></div>';
      }).join("");
      const flagged = data.flagged.map((p) => postCard(p, '<div class="lf-flag"><span>' + (p.hidden ? "Hidden" : "Visible") + " · " + p.reports + " report(s) · by " + esc(p.owner) + "</span>" +
        '<button type="button" class="btn-soft" data-act="hide" data-id="' + p.id + '" data-hidden="' + (!p.hidden) + '">' + (p.hidden ? "Show again" : "Hide") + "</button></div>")).join("");
      const waiting = data.waiting.map((p) => postCard(p, '<div class="lf-flag"><span>Finder ' + esc(p.owner) + "</span>" +
        '<button type="button" class="btn-soft" data-act="hide" data-id="' + p.id + '" data-hidden="true">Hide</button></div>')).join("");
      setScreenHtml('<h3 class="lf-h3">Collection desk</h3>' + tiles +
        '<h3 class="lf-h3">Open claims</h3><p class="rv-help">Finders keep items until they approve a claim, then hand them in here. Give an item only to the roll number shown, after checking the college ID card. The computer never decides.</p>' +
        (claims || '<p class="muted">No open claims.</p>') +
        '<h3 class="lf-h3">Reported or hidden posts</h3>' + (flagged ? '<div class="lf-grid">' + flagged + "</div>" : '<p class="muted">Nothing reported.</p>') +
        '<h3 class="lf-h3">Found items waiting for their owner</h3>' + (waiting ? '<div class="lf-grid">' + waiting + "</div>" : '<p class="muted">None.</p>'));
    } catch (e) { setScreenHtml(offline); }
  }
  async function deskAction(path, body) {
    const { ok, data } = await api(path, body).catch(() => ({ ok: false, data: {} }));
    if (!ok) alert(data.message || "That didn’t work. Please try again.");
    renderDesk();
  }

  // ---------- one click handler for the whole page ----------
  function init() {
    if (!$("view-lostfound")) return;
    $("view-lostfound").addEventListener("click", async (e) => {
      const b = e.target.closest("[data-act]");
      if (!b) return;
      const act = b.dataset.act, id = Number(b.dataset.id);
      if (act === "open") openPost(lastPosts[id]);
      else if (act === "matches") showMatches(id);
      else if (act === "close-dialog") closeDialog();
      else if (act === "report") {
        if (!confirm("Report this post to the collection desk as spam or inappropriate?")) return;
        const { ok, data } = await api("/api/lostfound/report", { postId: id }).catch(() => ({ ok: false, data: {} }));
        b.outerHTML = '<p class="small muted">' + esc(ok ? "Thanks, the desk will check it." : data.message || "Could not report.") + "</p>";
      } else if (act === "close") {
        const { ok, data } = await api("/api/lostfound/close", { id, reason: b.dataset.reason }).catch(() => ({ ok: false, data: {} }));
        if (!ok) return alert(data.message || "Could not close the post.");
        closeDialog();
        render(screen);
      } else if (act === "decide") {
        const d = b.dataset.decision;
        const ask = { approve: "Yes, it’s theirs? After this, please take the item to " + meta.collectionPoint.name + ".",
                      reject: "Say no to this claim?", atdesk: "Have you handed the item in at " + meta.collectionPoint.name + "?",
                      returned: "Mark this item as returned to its owner?" }[d];
        if (ask && !confirm(ask)) return;
        if (screen === "desk") return deskAction("/api/lostfound/decide", { claimId: id, decision: d });
        b.disabled = true;
        const { ok, data } = await api("/api/lostfound/decide", { claimId: id, decision: d }).catch(() => ({ ok: false, data: {} }));
        if (!ok) { b.disabled = false; return alert(data.message || "That didn’t work. Please try again."); }
        renderMine();
      } else if (act === "hide") deskAction("/api/lostfound/admin/hide", { postId: id, hidden: b.dataset.hidden === "true" });
    });
    $("view-lostfound").addEventListener("submit", (e) => { if (e.target.id === "lfClaimForm") { e.preventDefault(); claim(e.target); } });
    $("lfDialog").addEventListener("click", (e) => { if (e.target === $("lfDialog")) closeDialog(); });   // click on the dark backdrop
  }

  init();
  window.UnigoLostFound = { show, reset };
})();
