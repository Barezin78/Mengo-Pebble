// Mengão watchface — phone side (PebbleKit JS)
// Weather: phone GPS + Open-Meteo. Matches: ESPN public JSON (all competitions).
var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var FLA_ID = '819';
var ESPN = 'https://site.api.espn.com/apis/site/v2/sports/soccer/';
var FINAL_SHOW_MS = 6 * 3600 * 1000;     // show a final score until kickoff + 6h
var LIVE_WINDOW_MS = 3 * 3600 * 1000;

var COMP = {
  'bra.1': 'BR', 'conmebol.libertadores': 'LIB', 'bra.copa_do_brazil': 'CDB',
  'bra.camp.carioca': 'CAR', 'conmebol.sudamericana': 'SUL', 'conmebol.recopa': 'REC',
  'fifa.cwc': 'MUN', 'fifa.intercontinental_cup': 'INT', 'bra.supercopa': 'SUP'
};

// ---------- sequential AppMessage queue ----------
var queue = [], sending = false;
function send(dict) { queue.push({ d: dict, tries: 0 }); pump(); }
function pump() {
  if (sending || !queue.length) return;
  sending = true;
  var item = queue[0];
  Pebble.sendAppMessage(item.d, function () {
    queue.shift(); sending = false; pump();
  }, function () {
    sending = false;
    if (++item.tries >= 3) queue.shift();
    setTimeout(pump, 1000);
  });
}

function get(url, ok, fail) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (this.status !== 200) { fail('HTTP ' + this.status); return; }
    try { ok(JSON.parse(this.responseText)); } catch (e) { fail('parse ' + e); }
  };
  xhr.onerror = function () { fail('network'); };
  xhr.ontimeout = function () { fail('timeout'); };
  xhr.timeout = 20000;
  xhr.open('GET', url);
  xhr.send();
}

function settings() {
  try { return JSON.parse(localStorage.getItem('clay-settings')) || {}; } catch (e) { return {}; }
}
function enabled(key) { var s = settings(); return s[key] === undefined ? true : !!s[key]; }

// ---------- Weather ----------
function updateWeather() {
  navigator.geolocation.getCurrentPosition(function (pos) {
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + pos.coords.latitude.toFixed(4) +
      '&longitude=' + pos.coords.longitude.toFixed(4) +
      '&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min' +
      '&timezone=auto&forecast_days=1';
    get(url, function (j) {
      var msg = {
        TEMP: Math.round(j.current.temperature_2m), CODE: j.current.weather_code,
        IS_DAY: j.current.is_day ? 1 : 0,
        TMIN: Math.round(j.daily.temperature_2m_min[0]), TMAX: Math.round(j.daily.temperature_2m_max[0])
      };
      console.log('Weather: ' + JSON.stringify(msg));
      send(msg);
    }, function (e) { console.log('Weather failed: ' + e); send({ ERROR: 1 }); });
  }, function (err) {
    console.log('Location failed: ' + (err && err.message));
    send({ ERROR: 1 });
  }, { timeout: 15000, maximumAge: 10 * 60 * 1000 });
}

// ---------- Matches ----------
function parseEvent(e) {
  var c = e.competitions[0], fla = null, opp = null;
  for (var i = 0; i < c.competitors.length; i++) {
    var x = c.competitors[i];
    if (String(x.id) === FLA_ID || (x.team && String(x.team.id) === FLA_ID)) fla = x; else opp = x;
  }
  if (!fla || !opp) return null;
  var slug = (e.league && e.league.slug) || '';
  var st = (c.status || e.status || {});
  return {
    id: parseInt(e.id, 10), slug: slug, kickoff: Date.parse(e.date),
    state: st.type ? st.type.state : 'pre', status: st.type ? st.type.name : '',
    clock: st.displayClock || '',
    home: fla.homeAway === 'home' ? 1 : 0,
    opp: (opp.team.abbreviation || opp.team.shortDisplayName || '???').substring(0, 5).toUpperCase(),
    fla_score: score(fla.score), opp_score: score(opp.score),
    comp: COMP[slug] || ((e.league && e.league.abbreviation) || '').substring(0, 3).toUpperCase()
  };
}
function score(s) {
  if (s === undefined || s === null) return 0;
  if (typeof s === 'object') s = s.displayValue !== undefined ? s.displayValue : s.value;
  var n = parseInt(s, 10);
  return isNaN(n) ? 0 : n;
}
function clockText(m) {
  if (/HALFTIME/.test(m.status)) return 'INT';
  if (/PENALT|SHOOTOUT/.test(m.status)) return 'PEN';
  var c = (m.clock || '').replace(/'\+/, '+');   // "90'+3'" -> "90+3'"
  return c.substring(0, 6) || 'AO VIVO';
}
function sendMatch(m, mode) {
  var d = {
    M_MODE: mode, M_ID: m.id, M_HOME: m.home, M_TIME: Math.floor(m.kickoff / 1000),
    M_FLA: m.fla_score, M_OPP: m.opp_score, M_OPP_NAME: m.opp, M_COMP: m.comp,
    M_CLOCK: mode === 2 ? clockText(m) : ''
  };
  console.log('Match: ' + JSON.stringify(d));
  send(d);
}

// ESPN scoreboard is keyed by the calendar date in America/New_York (handles DST).
function easternYmd(ms) {
  try {
    var parts = new Intl.DateTimeFormat('en-US', {
      timeZone: 'America/New_York', year: 'numeric', month: '2-digit', day: '2-digit'
    }).formatToParts(new Date(ms));
    var y = '', mo = '', d = '';
    for (var i = 0; i < parts.length; i++) {
      if (parts[i].type === 'year') y = parts[i].value;
      else if (parts[i].type === 'month') mo = parts[i].value;
      else if (parts[i].type === 'day') d = parts[i].value;
    }
    if (y && mo && d) return y + mo + d;
  } catch (e) { /* fall through */ }
  // Fallback without Intl: US Eastern DST (2nd Sun Mar 02:00 → 1st Sun Nov 02:00).
  var dt = new Date(ms);
  var y = dt.getUTCFullYear();
  function nthSunday(monthIndex, n) {
    var first = new Date(Date.UTC(y, monthIndex, 1));
    var day = first.getUTCDay();
    var dom = 1 + ((7 - day) % 7) + (n - 1) * 7;
    return Date.UTC(y, monthIndex, dom, 7, 0, 0); // 02:00 ET ≈ 07:00 UTC in EST; close enough for date key
  }
  var start = nthSunday(2, 2); // March
  var end = nthSunday(10, 1);  // November
  var off = (ms >= start && ms < end) ? 4 : 5;
  var et = new Date(ms - off * 3600 * 1000);
  return et.getUTCFullYear() + ('0' + (et.getUTCMonth() + 1)).slice(-2) + ('0' + et.getUTCDate()).slice(-2);
}

// Live score: the league scoreboard refreshes faster than the team schedule
function fetchLive(m, done) {
  var ymd = easternYmd(m.kickoff);
  get(ESPN + m.slug + '/scoreboard?dates=' + ymd, function (j) {
    var evs = j.events || [];
    for (var i = 0; i < evs.length; i++) {
      if (parseInt(evs[i].id, 10) === m.id) {
        var e = evs[i]; e.league = { slug: m.slug, abbreviation: m.comp };
        var lm = parseEvent(e);
        if (lm) { lm.comp = m.comp; done(lm); return; }
      }
    }
    done(m);
  }, function () { done(m); });
}

function updateMatch() {
  get(ESPN + 'all/teams/' + FLA_ID + '/schedule?fixture=true', function (j) {
    var now = Date.now(), next = null, evs = j.events || [];
    for (var i = 0; i < evs.length; i++) {
      var m = parseEvent(evs[i]);
      if (m && m.state !== 'post') { next = m; break; }
    }
    if (next && (next.state === 'in' || (now >= next.kickoff && now < next.kickoff + LIVE_WINDOW_MS))) {
      fetchLive(next, function (lm) {
        if (lm.state === 'in') sendMatch(lm, 2);
        else if (lm.state === 'post') sendMatch(lm, 3);
        else sendMatch(lm, 1);   // kickoff time passed but not started yet
      });
      return;
    }
    // A match that ended recently? (results list, newest first)
    get(ESPN + 'all/teams/' + FLA_ID + '/schedule', function (r) {
      var res = r.events || [], last = null;
      for (var k = 0; k < res.length && !last; k++) {
        var pm = parseEvent(res[k]);
        if (pm && pm.state === 'post') last = pm;
      }
      if (last && now - last.kickoff < FINAL_SHOW_MS) sendMatch(last, 3);
      else if (next) sendMatch(next, 1);
      else send({ M_MODE: 0 });
    }, function () {
      if (next) sendMatch(next, 1); else send({ M_ERR: 1 });
    });
  }, function (e) {
    console.log('Match fetch failed: ' + e);
    send({ M_ERR: 1 });   // watch keeps the cached match
  });
}

Pebble.addEventListener('ready', function () {
  if (enabled('CFG_SHOW_WEATHER')) updateWeather();
  if (enabled('CFG_SHOW_MATCH')) updateMatch();
});

// The watch asks for refreshes: bit 1 = weather, bit 2 = match
Pebble.addEventListener('appmessage', function (e) {
  var r = e.payload && (e.payload.REQUEST !== undefined ? e.payload.REQUEST : e.payload[0]);
  if (r === undefined) return;
  if (r & 1) updateWeather();
  if (r & 2) updateMatch();
});
