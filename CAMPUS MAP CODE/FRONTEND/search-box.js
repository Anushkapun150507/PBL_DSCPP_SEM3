// search-box.js -- one reusable search box with autocomplete, used by the Campus Map's From, To
// and classroom boxes. It behaves exactly like the Bus Finder box: same validation rule, 120 ms
// debounce, ticket numbers against stale replies, arrow keys / Enter / Escape, highlighted letters.
(function () {
  "use strict";

  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  // Same rule as the C++ isValidQuery(): letters, digits, spaces and - . ' ( ) / & # , (max 60).
  const VALID = /^[A-Za-z0-9 .\-'()\/&#,]+$/;
  function validate(v) {
    if (!v.trim()) return "Please enter a name to search.";
    if (v.length > 60 || !VALID.test(v)) return "Use letters, numbers, spaces and simple symbols like - . / ( ) # only (max 60 characters).";
    return "";
  }

  async function getJson(path) {
    const res = await fetch(path, { headers: { Accept: "application/json" } });
    const data = await res.json().catch(() => ({}));
    return { ok: res.ok, status: res.status, data };
  }

  function highlight(text, q) {
    const i = text.toLowerCase().indexOf(q.toLowerCase());
    if (i < 0) return esc(text);
    return esc(text.slice(0, i)) + "<mark>" + esc(text.slice(i, i + q.length)) + "</mark>" + esc(text.slice(i + q.length));
  }

  // opts: { input, list, box, clearBtn, suggestUrl, onChoose(item), onSubmit(text), onClear() }
  // Suggestions from the server are objects { label, sub, id? }.
  function attach(opts) {
    const { input, list } = opts;
    let timer = null, seq = 0, active = -1, items = [];

    function hide() {
      list.hidden = true; list.innerHTML = ""; active = -1; items = [];
      input.setAttribute("aria-expanded", "false");
      input.removeAttribute("aria-activedescendant");
    }
    async function load(q) {
      const my = ++seq;
      try {
        const { data } = await getJson(opts.suggestUrl + encodeURIComponent(q));
        if (my !== seq || input.value.trim() !== q) return;          // stale reply or the text changed
        items = data.suggestions || [];
        if (!items.length) return hide();
        const idBase = input.id + "-opt";
        list.innerHTML = items.map((it, i) =>
          '<li role="option" id="' + idBase + i + '" data-i="' + i + '"><span>' + highlight(it.label, q) + "</span>" +
          (it.sub ? '<small>' + esc(it.sub) + "</small>" : "") + "</li>").join("");
        list.hidden = false; active = -1;
        input.setAttribute("aria-expanded", "true");
      } catch (e) { hide(); }
    }
    function move(delta) {
      const lis = list.querySelectorAll("li");
      if (!lis.length) return;
      active = (active + delta + lis.length) % lis.length;
      lis.forEach((li, i) => li.classList.toggle("active", i === active));
      input.setAttribute("aria-activedescendant", lis[active].id);
      lis[active].scrollIntoView({ block: "nearest" });
    }
    function choose(i) {
      const it = items[i];
      hide();
      if (!it) return;
      input.value = it.label;
      if (opts.clearBtn) opts.clearBtn.hidden = false;
      opts.onChoose(it);
    }

    input.addEventListener("input", () => {
      if (opts.clearBtn) opts.clearBtn.hidden = !input.value;
      if (opts.onType) opts.onType();
      clearTimeout(timer);
      const q = input.value.trim();
      if (!q || !VALID.test(q)) return hide();
      timer = setTimeout(() => load(q), 120);                          // debounce: wait for a pause
    });
    input.addEventListener("keydown", (e) => {
      if (e.key === "ArrowDown") { e.preventDefault(); move(1); }
      else if (e.key === "ArrowUp") { e.preventDefault(); move(-1); }
      else if (e.key === "Escape") hide();
      else if (e.key === "Enter") {
        e.preventDefault();
        if (active >= 0) return choose(active);
        hide();
        opts.onSubmit(input.value);
      }
    });
    list.addEventListener("mousedown", (e) => {                        // before the input loses focus
      const li = e.target.closest("li");
      if (li) { e.preventDefault(); choose(Number(li.dataset.i)); }
    });
    document.addEventListener("click", (e) => { if (!(opts.box || input).contains(e.target)) hide(); });
    if (opts.clearBtn) opts.clearBtn.addEventListener("click", () => {
      input.value = ""; opts.clearBtn.hidden = true; hide(); input.focus();
      if (opts.onClear) opts.onClear();
    });

    return {
      hide,
      setValue(v) { input.value = v || ""; if (opts.clearBtn) opts.clearBtn.hidden = !input.value; hide(); },
    };
  }

  window.UnigoSearch = { attach, validate, esc, getJson };
})();
