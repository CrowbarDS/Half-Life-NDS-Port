// Download page: reads the latest release from GitHub, guesses the visitor's
// system, and points at the matching converter package.
"use strict";

const REPO = "CrowbarDS/Half-Life-NDS-Port";
const API = "https://api.github.com/repos/" + REPO;
const RELEASES = "https://github.com/" + REPO + "/releases";

// What each system offers. `key` is the part of the asset name after the version.
const SYSTEMS = {
  windows: {
    name: "Windows",
    archs: [
      { id: "x64", label: "x64", sub: "Intel / AMD 64-bit", app: "windows-x64.zip" },
      { id: "arm64", label: "ARM64", sub: "Snapdragon, Surface", app: "windows-arm64.zip" },
      { id: "x86", label: "x86", sub: "32-bit Windows", app: "windows-x86.zip" },
    ],
    desc: "A zip with the converter's window, its command line, and everything they need.",
    cli: null,
  },
  macos: {
    name: "macOS",
    archs: [
      { id: "universal", label: "Universal", sub: "Apple silicon and Intel", app: "macos-universal.zip", cli: "cli-macos-universal.tar.gz" },
    ],
    desc: "The converter as a Mac app, in a zip. Runs on macOS 12 and later.",
  },
  linux: {
    name: "Linux",
    archs: [
      { id: "x86_64", label: "x86_64", sub: "Intel / AMD 64-bit", app: "linux-x86_64.AppImage", cli: "cli-linux-x86_64.tar.gz" },
      { id: "arm64", label: "ARM64", sub: "aarch64", app: "linux-arm64.AppImage", cli: "cli-linux-arm64.tar.gz" },
    ],
    desc: "A single AppImage with the converter's window. Runs on most distributions.",
  },
};

const $ = (id) => document.getElementById(id);

const state = { os: "windows", arch: "x64", assets: [], release: null, guessed: true, detail: "" };

function fmtSize(bytes) {
  if (!bytes) return "";
  const mb = bytes / (1024 * 1024);
  return mb >= 10 ? Math.round(mb) + " MB" : mb.toFixed(1) + " MB";
}

function fmtDate(iso) {
  try {
    return new Date(iso).toLocaleDateString("en-GB", { year: "numeric", month: "long", day: "numeric" });
  } catch (e) {
    return iso.slice(0, 10);
  }
}

// An asset whose name ends with "-<key>", for any version.
function findAsset(key) {
  if (!key) return null;
  return state.assets.find((a) => a.name.startsWith("hlds-convert-") && a.name.endsWith("-" + key)) || null;
}

// ---------- Detection ----------

function detectOS() {
  const uad = navigator.userAgentData;
  const p = ((uad && uad.platform) || navigator.platform || "").toLowerCase();
  const ua = navigator.userAgent.toLowerCase();
  if (p.includes("win") || ua.includes("windows")) return "windows";
  if (p.includes("mac") || ua.includes("mac os")) return /iphone|ipad/.test(ua) ? null : "macos";
  if (ua.includes("android") || /iphone|ipad|cros/.test(ua)) return null;
  if (p.includes("linux") || ua.includes("linux") || ua.includes("x11")) return "linux";
  return null;
}

function archFromUA(os) {
  const ua = navigator.userAgent.toLowerCase();
  if (os === "windows") {
    if (ua.includes("arm64") || ua.includes("aarch64")) return { arch: "arm64", sure: true };
    if (ua.includes("win64") || ua.includes("wow64") || ua.includes("x64")) return { arch: "x64", sure: false };
    return { arch: "x64", sure: false };
  }
  if (os === "linux") {
    if (ua.includes("aarch64") || ua.includes("arm64")) return { arch: "arm64", sure: true };
    if (ua.includes("x86_64") || ua.includes("amd64")) return { arch: "x86_64", sure: true };
    return { arch: "x86_64", sure: false };
  }
  return { arch: "universal", sure: true };
}

async function detect() {
  const os = detectOS();
  if (!os) {
    state.os = "windows";
    state.arch = "x64";
    state.guessed = true;
    state.detail = "This device can't run the converter. Pick the computer you'll use.";
    return;
  }
  state.os = os;
  let guess = archFromUA(os);
  const uad = navigator.userAgentData;
  if (os !== "macos" && uad && uad.getHighEntropyValues) {
    try {
      const v = await uad.getHighEntropyValues(["architecture", "bitness"]);
      if (v.architecture === "arm") guess = { arch: "arm64", sure: true };
      else if (v.architecture === "x86" && v.bitness === "32" && os === "windows") guess = { arch: "x86", sure: true };
      else if (v.architecture === "x86") guess = { arch: os === "windows" ? "x64" : "x86_64", sure: true };
    } catch (e) { /* keep the user-agent guess */ }
  }
  state.arch = guess.arch;
  state.guessed = !guess.sure;
  const arch = SYSTEMS[os].archs.find((a) => a.id === guess.arch);
  state.detail = "Detected <b>" + SYSTEMS[os].name + (os === "macos" ? "" : " " + arch.label) + "</b>" +
    (state.guessed ? " (processor guessed; check below if unsure)." : ".");
}

// ---------- Rendering ----------

function renderArchs() {
  const row = $("arch-row");
  row.textContent = "";
  for (const a of SYSTEMS[state.os].archs) {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "seg";
    b.dataset.arch = a.id;
    b.setAttribute("aria-pressed", String(a.id === state.arch));
    b.textContent = a.label;
    const s = document.createElement("span");
    s.className = "sub";
    s.textContent = a.sub;
    b.appendChild(s);
    b.addEventListener("click", () => { state.arch = a.id; render(); });
    row.appendChild(b);
  }
}

function setAlt(el, sizeEl, asset, fallbackHref) {
  if (asset) {
    el.href = asset.browser_download_url;
    el.classList.remove("missing");
    if (sizeEl) sizeEl.textContent = fmtSize(asset.size);
  } else if (state.release) {
    el.href = "#";
    el.classList.add("missing");
    if (sizeEl) sizeEl.textContent = "n/a";
  } else {
    el.href = fallbackHref;
    el.classList.remove("missing");
    if (sizeEl) sizeEl.textContent = "";
  }
}

function render() {
  const sys = SYSTEMS[state.os];
  if (!sys.archs.some((a) => a.id === state.arch)) state.arch = sys.archs[0].id;
  const arch = sys.archs.find((a) => a.id === state.arch);

  for (const b of document.querySelectorAll("#os-row .seg")) b.setAttribute("aria-pressed", String(b.dataset.os === state.os));
  renderArchs();
  $("detected").innerHTML = state.detail;

  for (const g of document.querySelectorAll("[data-guide]")) g.hidden = g.dataset.guide !== state.os;
  $("install-os").textContent = sys.name;

  // The main package.
  const app = findAsset(arch.app);
  $("main-title").textContent = "Converter for " + sys.name + (state.os === "macos" ? "" : " " + arch.label);
  $("main-desc").textContent = sys.desc;
  const btn = $("main-btn");
  if (app) {
    btn.href = app.browser_download_url;
    $("main-file").textContent = app.name;
    $("main-size").textContent = fmtSize(app.size);
    btn.firstChild.textContent = "Download ";
  } else {
    btn.href = state.release ? state.release.html_url : RELEASES + "/latest";
    $("main-file").textContent = state.release ? "Not in this release." : "hlds-convert-<version>-" + arch.app;
    $("main-size").textContent = "";
    btn.firstChild.textContent = "Open the release page ";
  }

  // The command line: Windows has it inside the zip.
  const cliEl = $("alt-cli");
  if (state.os === "windows") {
    cliEl.hidden = true;
  } else {
    cliEl.hidden = false;
    $("alt-cli-d").textContent = state.os === "linux"
      ? "A static program, no window: for scripts and servers"
      : "No window, for scripts and Terminal";
    setAlt(cliEl, $("alt-cli-s"), findAsset(arch.cli), RELEASES + "/latest");
  }

  const rom = state.assets.find((a) => /^half-life-nds-port-.*\.zip$/.test(a.name));
  setAlt($("alt-rom"), $("alt-rom-s"), rom, RELEASES + "/latest");
  const sums = state.assets.find((a) => a.name === "SHA256SUMS.txt");
  setAlt($("alt-sums"), null, sums, RELEASES + "/latest");
}

// Release notes: headings and bullets only, every piece of text escaped.
function renderNotes(body) {
  const box = $("notes");
  box.textContent = "";
  let list = null;
  for (const raw of (body || "").split(/\r?\n/)) {
    const line = raw.trim();
    if (!line) { list = null; continue; }
    const h = line.match(/^#{1,6}\s+(.*)$/);
    const li = line.match(/^[-*]\s+(.*)$/);
    if (h) {
      const el = document.createElement("h4");
      el.textContent = h[1];
      box.appendChild(el);
      list = null;
    } else if (li) {
      if (!list) { list = document.createElement("ul"); box.appendChild(list); }
      const el = document.createElement("li");
      el.textContent = li[1].replace(/\*\*|`/g, "");
      list.appendChild(el);
    } else {
      const el = document.createElement("p");
      el.className = "dim small";
      el.textContent = line.replace(/\*\*|`/g, "");
      box.appendChild(el);
      list = null;
    }
  }
  return box.childElementCount > 0;
}

function renderOlder(list) {
  const ul = $("older");
  ul.textContent = "";
  for (const r of list) {
    if (r.draft) continue;
    const li = document.createElement("li");
    const a = document.createElement("a");
    a.href = r.html_url;
    a.textContent = r.name || r.tag_name;
    const d = document.createElement("span");
    d.className = "dim";
    d.textContent = fmtDate(r.published_at) + (r.prerelease ? " · pre-release" : "");
    li.append(a, d);
    ul.appendChild(li);
  }
  const all = document.createElement("li");
  const a = document.createElement("a");
  a.href = RELEASES;
  a.textContent = "All releases on GitHub";
  all.appendChild(a);
  ul.appendChild(all);
}

// ---------- Data ----------

async function getJSON(url) {
  const r = await fetch(url, { headers: { Accept: "application/vnd.github+json" } });
  if (!r.ok) throw new Error(url + ": " + r.status);
  return r.json();
}

async function load() {
  try {
    const rel = await getJSON(API + "/releases/latest");
    state.release = rel;
    state.assets = rel.assets || [];
    $("status-dot").className = "dot live";
    $("status-text").textContent = "Latest release";
    $("version").textContent = rel.tag_name;
    $("date").textContent = fmtDate(rel.published_at);
    $("status-version").hidden = false;
    $("status-date").hidden = false;
    $("notes-version").textContent = rel.tag_name;
    $("notes-box").hidden = !renderNotes(rel.body);
  } catch (e) {
    $("status-dot").className = "dot err";
    $("status-text").textContent = "Release list unavailable";
    $("api-error").hidden = false;
  }
  render();
  try {
    renderOlder(await getJSON(API + "/releases?per_page=10"));
  } catch (e) { /* the link to all releases stays */ }
}

// ---------- Start ----------

for (const b of document.querySelectorAll("#os-row .seg")) {
  b.addEventListener("click", () => {
    state.os = b.dataset.os;
    state.arch = SYSTEMS[state.os].archs[0].id;
    render();
  });
}

detect().then(() => { render(); load(); });
