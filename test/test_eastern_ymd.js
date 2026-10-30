'use strict';
/**
 * Unit test for America/New_York date key used by ESPN scoreboard fetch.
 * Mirrors src/pkjs/index.js easternYmd.
 */
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
  } catch (e) {}
  var dt = new Date(ms);
  var y = dt.getUTCFullYear();
  function nthSunday(monthIndex, n) {
    var first = new Date(Date.UTC(y, monthIndex, 1));
    var day = first.getUTCDay();
    var dom = 1 + ((7 - day) % 7) + (n - 1) * 7;
    return Date.UTC(y, monthIndex, dom, 7, 0, 0);
  }
  var start = nthSunday(2, 2);
  var end = nthSunday(10, 1);
  var off = (ms >= start && ms < end) ? 4 : 5;
  var et = new Date(ms - off * 3600 * 1000);
  return et.getUTCFullYear() + ('0' + (et.getUTCMonth() + 1)).slice(-2) + ('0' + et.getUTCDate()).slice(-2);
}

function assert(c, m) { if (!c) throw new Error(m); }

// 2026-03-08 06:30 UTC = still 01:30 EST (UTC-5) → 20260308
assert(easternYmd(Date.UTC(2026, 2, 8, 6, 30)) === '20260308', 'pre-DST morning');
// 2026-03-08 07:30 UTC = 03:30 EDT (UTC-4) after 02:00 spring forward → 20260308
assert(easternYmd(Date.UTC(2026, 2, 8, 7, 30)) === '20260308', 'post-spring-forward');
// Late evening ET that is next day UTC: 2026-07-04 23:30 EDT = 2026-07-05 03:30 UTC → still 20260704
assert(easternYmd(Date.UTC(2026, 6, 5, 3, 30)) === '20260704', 'EDT evening stays same calendar day');
// Winter: 2026-01-15 04:30 UTC = 2026-01-14 23:30 EST → 20260114
assert(easternYmd(Date.UTC(2026, 0, 15, 4, 30)) === '20260114', 'EST previous calendar day');
// Fixed -5 would wrongly say 20260308 for a kickoff that is still Mar 7 in EDT evening... 
// Kickoff 2026-11-01 03:30 UTC = 2026-10-31 23:30 EDT → 20261031
assert(easternYmd(Date.UTC(2026, 10, 1, 3, 30)) === '20261031', 'EDT Halloween evening');

console.log('OK easternYmd DST cases');
