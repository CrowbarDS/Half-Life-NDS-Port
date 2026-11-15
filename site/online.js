// Online play as the relay counts it (tools/relay/README.md, "The website's
// counts"): the top bar's ONLINE, a dot (green, and how many consoles, when
// anyone is on), and on the home page the section #online. Read when a page
// opens and every 30 s while it is shown. Only numbers are read from it, and
// only as numbers.
(function () {
  'use strict';

  // Where the relay's status.json is given, over HTTPS. Empty: nothing shown.
  var kStatusUrl = 'https://164.132.255.209/status.json';
  if (!kStatusUrl) return;

  var kEveryMs = 30000;
  var kStaleS = 60; // the relay writes it each 5 s: older than this, it has stopped

  var nav = document.getElementById('nav-online');
  var navDot = document.getElementById('nav-online-dot');
  var navCount = document.getElementById('nav-online-count');
  var section = document.getElementById('online');
  var dot = document.getElementById('online-dot');
  var said = document.getElementById('online-said');
  var when = document.getElementById('online-when');
  var counts = section ? section.querySelector('.online-counts') : null;
  var fields = {
    consoles: document.getElementById('online-consoles'),
    rooms: document.getElementById('online-rooms'),
    inRooms: document.getElementById('online-in')
  };
  var timer = null;

  function count(v) {
    return typeof v === 'number' && Number.isInteger(v) && v >= 0 && v < 100000 ? v : null;
  }
  function seconds(v) {
    return typeof v === 'number' && Number.isInteger(v) && v > 0 ? v : null;
  }

  function showNav(status) {
    if (!nav) return;
    nav.hidden = false;
    var anyone = status !== null && status.consoles > 0;
    navDot.className = status === null ? 'dot err' : (anyone ? 'dot live' : 'dot');
    navCount.textContent = anyone ? String(status.consoles) : '';
    nav.title = status === null ? 'Online play: the relay is not answering'
      : anyone ? status.consoles + (status.consoles === 1 ? ' console' : ' consoles') + ' online now'
      : 'Online play: nobody on right now';
  }

  function showSection(status) {
    if (!section || !counts) return;
    section.hidden = false;
    if (!status) {
      dot.className = 'dot err';
      said.textContent = 'The relay is not answering right now.';
      when.textContent = '';
      Object.keys(fields).forEach(function (k) { fields[k].textContent = '-'; });
      counts.classList.add('quiet');
      return;
    }
    Object.keys(fields).forEach(function (k) { fields[k].textContent = String(status[k]); });
    var anyone = status.consoles > 0;
    dot.className = anyone ? 'dot live' : 'dot';
    counts.classList.toggle('quiet', !anyone);
    said.textContent = anyone
      ? status.consoles + (status.consoles === 1 ? ' console' : ' consoles') + ' online now'
      : 'Nobody online right now: host a game and others can join it.';
    when.textContent = 'updated ' + new Date(status.updated * 1000).toLocaleTimeString();
  }

  function show(status) {
    showNav(status);
    showSection(status);
  }

  function read() {
    var abort = typeof AbortController === 'function' ? new AbortController() : null;
    var stop = abort ? setTimeout(function () { abort.abort(); }, 8000) : null;
    fetch(kStatusUrl, { cache: 'no-store', credentials: 'omit', signal: abort ? abort.signal : undefined })
      .then(function (r) { if (!r.ok) throw new Error('status ' + r.status); return r.json(); })
      .then(function (j) {
        var status = {
          updated: seconds(j && j.updated),
          consoles: count(j && j.consoles),
          rooms: count(j && j.rooms),
          inRooms: count(j && j.inRooms)
        };
        var fresh = status.updated !== null && Date.now() / 1000 - status.updated < kStaleS;
        var whole = status.consoles !== null && status.rooms !== null && status.inRooms !== null;
        show(fresh && whole ? status : null);
      })
      .catch(function () { show(null); })
      .then(function () { if (stop) clearTimeout(stop); });
  }

  function start() {
    if (timer === null) { read(); timer = setInterval(read, kEveryMs); }
  }
  function pause() {
    if (timer !== null) { clearInterval(timer); timer = null; }
  }

  document.addEventListener('visibilitychange', function () {
    if (document.hidden) pause(); else start();
  });
  if (!document.hidden) start();
})();
