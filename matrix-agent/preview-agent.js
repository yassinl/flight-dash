// this is deprecated code, i'm thinking we'll need a webserver to allow
// the user to configure our program from their phone or computer so we can repurpose this later
const http = require('http');
const fs   = require('fs');
const path = require('path');

const WIDTH    = 64;
const HEIGHT   = 32;
const STATE_URL = process.env.STATE_URL || 'http://localhost:8080/api/state/current';
const PORT      = Number(process.env.PORT || 8090);
const POLL_MS   = Number(process.env.POLL_MS || 5000);

let latestState = null;
let latestError = null;
let latestFrame = new Uint8Array(WIDTH * HEIGHT * 3);

// ---------------------------------------------------------------------------
// CSV helpers — mirrors next_csv_field / load_airports / load_airlines in
// flight_data.cc so we're using the same data and the same parsing rules.
// ---------------------------------------------------------------------------

function nextCSVField(line, posRef) {
  let field = '';
  let pos = posRef.pos;
  if (pos >= line.length) return field;
  if (line[pos] === '"') {
    pos++;
    while (pos < line.length && line[pos] !== '"') field += line[pos++];
    if (pos < line.length) pos++; // closing quote
  } else {
    while (pos < line.length && line[pos] !== ',') field += line[pos++];
  }
  if (pos < line.length && line[pos] === ',') pos++;
  posRef.pos = pos;
  return field;
}

function loadAirlines() {
  const filePath = process.env.AIRLINES_DAT || path.join(__dirname, 'airlines.dat');
  const map = new Map();
  try {
    const lines = fs.readFileSync(filePath, 'utf8').split('\n');
    for (const line of lines) {
      const ref = { pos: 0 };
      const fields = [];
      for (let i = 0; i < 5; i++) fields.push(nextCSVField(line, ref));
      // col 1 = name, col 4 = ICAO prefix
      const name = fields[1];
      const icao = fields[4];
      if (!icao || icao === 'N/A' || icao === '\\N') continue;
      if (!name || name === '\\N') continue;
      map.set(icao, name);
    }
    console.log(`loaded ${map.size} airlines from ${filePath}`);
  } catch (e) {
    console.error(`airlines.dat not found — set AIRLINES_DAT env var (${e.message})`);
  }
  return map;
}

function loadAirports() {
  const filePath = process.env.AIRPORTS_CSV || path.join(__dirname, 'airports.csv');
  const map = new Map();
  try {
    const lines = fs.readFileSync(filePath, 'utf8').split('\n');
    // skip header (line 0)
    for (let i = 1; i < lines.length; i++) {
      const ref = { pos: 0 };
      const fields = [];
      for (let f = 0; f < 6; f++) fields.push(nextCSVField(lines[i], ref));
      // col 1 = ident, col 2 = type, col 3 = name
      const ident = fields[1];
      const type  = fields[2];
      const name  = fields[3];
      if (type === 'heliport' || type === 'balloonport' || type === 'closed') continue;
      if (!ident || !name) continue;
      map.set(ident, name);
    }
    console.log(`loaded ${map.size} airports from ${filePath}`);
  } catch (e) {
    console.error(`airports.csv not found — set AIRPORTS_CSV env var (${e.message})`);
  }
  return map;
}

const airlineMap = loadAirlines();
const airportMap = loadAirports();

// ---------------------------------------------------------------------------
// State enrichment — adds human-readable names to the tracked flight
// ---------------------------------------------------------------------------

function enrichState(state) {
  if (!state || !state.tracked_flight) return state;
  const f = state.tracked_flight;

  // Airline full name: prefer what the C++ pipeline set, fall back to CSV lookup
  const callsignPrefix = (f.callsign || '').slice(0, 3).toUpperCase();
  const airlineName = f.airline || f.airline_name
    || airlineMap.get(callsignPrefix) || null;

  // Airport full names: support both C++ field names (origin_icao / dest_icao)
  // and the webapp mock names (origin_airport / destination_airport)
  const originIcao = f.origin_icao || f.origin_airport || null;
  const destIcao   = f.dest_icao   || f.destination_airport || null;
  const originName = originIcao ? (airportMap.get(originIcao) || null) : null;
  const destName   = destIcao   ? (airportMap.get(destIcao)   || null) : null;

  // Normalise units: C++ uses SI, webapp mock already has imperial
  const altFt  = f.altitude_ft  ?? (f.altitude_m       != null ? Math.round(f.altitude_m       * 3.28084)  : null);
  const spdKts = f.speed_kts    ?? (f.speed_ms          != null ? Math.round(f.speed_ms          * 1.94384)  : null);
  const vrFpm  = f.vertical_rate_fpm ?? (f.vertical_rate_ms != null ? Math.round(f.vertical_rate_ms * 196.85) : null);

  return {
    ...state,
    tracked_flight: {
      ...f,
      airline_full_name:  airlineName,
      origin_icao:        originIcao,
      dest_icao:          destIcao,
      origin_name:        originName,
      dest_name:          destName,
      altitude_ft:        altFt,
      speed_kts:          spdKts,
      vertical_rate_fpm:  vrFpm
    }
  };
}

// ---------------------------------------------------------------------------
// 3×5 pixel font
// ---------------------------------------------------------------------------

const font3x5 = {
  A: ['010', '101', '111', '101', '101'],
  B: ['110', '101', '110', '101', '110'],
  C: ['011', '100', '100', '100', '011'],
  D: ['110', '101', '101', '101', '110'],
  E: ['111', '100', '110', '100', '111'],
  F: ['111', '100', '110', '100', '100'],
  G: ['011', '100', '101', '101', '011'],
  H: ['101', '101', '111', '101', '101'],
  I: ['111', '010', '010', '010', '111'],
  J: ['001', '001', '001', '101', '010'],
  K: ['101', '101', '110', '101', '101'],
  L: ['100', '100', '100', '100', '111'],
  M: ['101', '111', '111', '101', '101'],
  N: ['101', '111', '111', '111', '101'],
  O: ['010', '101', '101', '101', '010'],
  P: ['110', '101', '110', '100', '100'],
  Q: ['010', '101', '101', '010', '001'],
  R: ['110', '101', '110', '101', '101'],
  S: ['011', '100', '010', '001', '110'],
  T: ['111', '010', '010', '010', '010'],
  U: ['101', '101', '101', '101', '111'],
  V: ['101', '101', '101', '101', '010'],
  W: ['101', '101', '111', '111', '101'],
  X: ['101', '101', '010', '101', '101'],
  Y: ['101', '101', '010', '010', '010'],
  Z: ['111', '001', '010', '100', '111'],
  0: ['111', '101', '101', '101', '111'],
  1: ['010', '110', '010', '010', '111'],
  2: ['110', '001', '010', '100', '111'],
  3: ['111', '001', '011', '001', '111'],
  4: ['101', '101', '111', '001', '001'],
  5: ['111', '100', '111', '001', '111'],
  6: ['111', '100', '111', '101', '111'],
  7: ['111', '001', '010', '100', '100'],
  8: ['111', '101', '111', '101', '111'],
  9: ['111', '101', '111', '001', '111'],
  '-': ['000', '000', '111', '000', '000'],
  ':': ['000', '010', '000', '010', '000'],
  '%': ['101', '001', '010', '100', '101'],
  '>': ['100', '010', '001', '010', '100'],
  ' ': ['000', '000', '000', '000', '000']
};

function frameIndex(x, y) { return (y * WIDTH + x) * 3; }

function setPixel(frame, x, y, r, g, b) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
  const idx = frameIndex(x, y);
  frame[idx] = r; frame[idx + 1] = g; frame[idx + 2] = b;
}

function fillRect(frame, x, y, w, h, r, g, b) {
  for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++) setPixel(frame, xx, yy, r, g, b);
}

function clearFrame(frame, r, g, b) {
  for (let y = 0; y < HEIGHT; y++) for (let x = 0; x < WIDTH; x++) setPixel(frame, x, y, r, g, b);
}

function drawChar(frame, x, y, char, r, g, b) {
  const glyph = font3x5[char] || font3x5[' '];
  for (let row = 0; row < glyph.length; row++) for (let col = 0; col < glyph[row].length; col++)
    if (glyph[row][col] === '1') setPixel(frame, x + col, y + row, r, g, b);
}

function drawText(frame, x, y, text, r, g, b) {
  for (let i = 0; i < text.length; i++) drawChar(frame, x + i * 4, y, text[i], r, g, b);
}

function hashColor(code) {
  let hash = 0;
  for (const ch of code) hash = (hash * 31 + ch.charCodeAt(0)) & 0xffff;
  return [80 + (hash % 176), 80 + ((hash >> 3) % 176), 80 + ((hash >> 7) % 176)];
}

// ---------------------------------------------------------------------------
// Matrix frame renderer (64×32)
//
// Layout:
//   y=1-8   Left: 8×8 airline logo square
//           Right (x=11): airline code / callsign
//   y=10-14 Left: origin IATA code    Right (x=15): ALT value
//   y=17-21 Left: dest IATA code      Right (x=15): SPD value
//   y=22-26 Full-width progress bar
//   y=27-31 Percentage close to bar
// ---------------------------------------------------------------------------

function renderFrame(state) {
  const frame = new Uint8Array(WIDTH * HEIGHT * 3);

  if (!state || !state.tracked_flight) {
    clearFrame(frame, 8, 8, 16);
    drawText(frame, 3, 12, 'NO DATA', 255, 60, 60);
    return frame;
  }

  const alert = state.weather?.alert_level || 'normal';
  const bg = alert === 'alert' ? [35, 0, 0] : alert === 'caution' ? [26, 14, 0] : [0, 18, 0];
  clearFrame(frame, bg[0], bg[1], bg[2]);

  const flight   = state.tracked_flight;
  const code     = (flight.airline_code || (flight.callsign || '').slice(0, 2) || 'NA').slice(0, 2).toUpperCase();
  const callsign = (flight.callsign || flight.id || 'UNKNOWN').slice(0, 8).toUpperCase();
  const progress = Math.max(0, Math.min(100, Math.round(flight.progress_pct || 0)));

  // --- Airline logo square (top-left) ---
  const [lr, lg, lb] = hashColor(code);
  fillRect(frame, 1, 1, 8, 8, lr, lg, lb);
  drawText(frame, 2, 3, code, 0, 0, 0);

  // --- Callsign to the right of the logo ---
  drawText(frame, 11, 2, callsign, 220, 220, 255);

  // --- Origin / destination codes below the logo ---
  const originCode = (flight.origin_icao || flight.origin_airport || '---').slice(0, 4).toUpperCase();
  const destCode   = (flight.dest_icao   || flight.destination_airport || '---').slice(0, 4).toUpperCase();
  drawText(frame, 1, 10, originCode, 180, 230, 180);
  drawText(frame, 1, 17, destCode,   180, 180, 230);

  // --- Metrics (right column) ---
  const altFt  = flight.altitude_ft;
  const spdKts = flight.speed_kts;

  if (altFt != null) {
    const altStr = String(Math.round(altFt / 100) * 100).padStart(5);
    drawText(frame, 15, 10, 'A', 160, 160, 160);
    drawText(frame, 20, 10, altStr.trim().slice(0, 6), 200, 200, 255);
  }
  if (spdKts != null) {
    drawText(frame, 15, 17, 'S', 160, 160, 160);
    drawText(frame, 20, 17, String(spdKts).slice(0, 5), 200, 255, 200);
  }

  // --- Progress bar ---
  fillRect(frame, 1, 22, 62, 4, 20, 20, 20);
  const progressW = Math.round((progress / 100) * 62);
  fillRect(frame, 1, 22, progressW, 4, 60, 210, 110);

  // --- Percentage close to bar ---
  drawText(frame, 2, 27, `${String(progress)}%`, 255, 255, 255);

  return frame;
}

// ---------------------------------------------------------------------------
// State polling
// ---------------------------------------------------------------------------

async function pollState() {
  try {
    const response = await fetch(STATE_URL);
    if (!response.ok) throw new Error(`state fetch failed: ${response.status}`);
    latestState = await response.json();
    latestFrame = renderFrame(enrichState(latestState));
    latestError = null;
  } catch (error) {
    latestError = String(error.message || error);
    latestFrame = renderFrame(enrichState(latestState));
  }
}

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

function sendJson(res, code, payload) {
  res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(payload));
}

// ---------------------------------------------------------------------------
// HTML preview page
// ---------------------------------------------------------------------------

function htmlPage() {
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Matrix Preview</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 16px; background: #111; color: #eee; }
    .card { max-width: 480px; margin: 0 auto; }
    h2 { font-size: 0.85rem; color: #555; font-weight: 400; letter-spacing: 0.06em; text-transform: uppercase; margin-bottom: 14px; }

    /* ---- top section: canvas left, metrics right ---- */
    .flight-top { display: flex; gap: 16px; align-items: flex-start; }

    .left-panel { flex: 0 0 auto; }
    canvas { display: block; width: 192px; height: 96px; image-rendering: pixelated; border: 1px solid #2a2a2a; background: #000; }

    .airports-panel { margin-top: 10px; font-size: 11px; line-height: 1.6; color: #aaa; max-width: 192px; }
    .airport-name { color: #ddd; }
    .airport-arrow { color: #444; text-align: center; font-size: 13px; margin: 1px 0; }

    .right-panel { flex: 1; min-width: 0; padding-top: 2px; }
    .airline-name { font-size: 13px; font-weight: 600; color: #fff; margin-bottom: 12px; line-height: 1.3; }

    .metrics { display: grid; grid-template-columns: auto 1fr; row-gap: 6px; column-gap: 12px; }
    .metric-label { font-size: 9px; font-weight: 600; letter-spacing: 0.1em; text-transform: uppercase; color: #555; padding-top: 1px; }
    .metric-value { font-size: 12px; color: #ccc; font-variant-numeric: tabular-nums; }

    /* ---- progress section (full width below top) ---- */
    .progress-section { margin-top: 14px; }
    .progress-track { height: 7px; background: #222; border-radius: 4px; overflow: hidden; }
    .progress-fill  { height: 100%; background: #3cd26e; border-radius: 4px; transition: width 0.5s ease; }
    .progress-labels { display: flex; justify-content: space-between; align-items: baseline; margin-top: 5px; }
    .progress-pct  { font-size: 18px; font-weight: 700; color: #fff; }
    .progress-time { font-size: 11px; color: #555; }

    button { margin-top: 14px; padding: 7px 14px; border: 1px solid #2a2a2a; border-radius: 6px; background: #1a1a1a; color: #666; cursor: pointer; font-size: 11px; }
    button:hover { border-color: #444; color: #eee; }
    .no-data { color: #444; }
  </style>
</head>
<body>
  <div class="card">
    <h2>64 &times; 32 Matrix Preview</h2>
    <div class="flight-top">
      <div class="left-panel">
        <canvas id="matrix" width="64" height="32"></canvas>
        <div class="airports-panel" id="airports"><span class="no-data">—</span></div>
      </div>
      <div class="right-panel">
        <div class="airline-name" id="airline-name"><span class="no-data">—</span></div>
        <div class="metrics" id="metrics"></div>
      </div>
    </div>
    <div class="progress-section">
      <div class="progress-track"><div class="progress-fill" id="progress-fill" style="width:0%"></div></div>
      <div class="progress-labels">
        <span class="progress-pct" id="progress-pct">—</span>
        <span class="progress-time" id="progress-time">—</span>
      </div>
    </div>
    <button id="refresh">Refresh now</button>
  </div>
  <script>
    const canvas    = document.getElementById('matrix');
    const ctx       = canvas.getContext('2d');
    const imageData = ctx.createImageData(64, 32);

    function drawFrame(pixels) {
      for (let i = 0, j = 0; i < pixels.length; i += 3, j += 4) {
        imageData.data[j]     = pixels[i];
        imageData.data[j + 1] = pixels[i + 1];
        imageData.data[j + 2] = pixels[i + 2];
        imageData.data[j + 3] = 255;
      }
      ctx.putImageData(imageData, 0, 0);
    }

    function fmtMetric(val, unit) {
      if (val == null) return '<span style="color:#444">—</span>';
      return val.toLocaleString() + '\u2009' + unit;
    }

    function fmtVR(fpm) {
      if (fpm == null) return '<span style="color:#444">—</span>';
      const sign = fpm >= 0 ? '+' : '';
      return sign + fpm.toLocaleString() + '\u2009fpm';
    }

    function timeRemaining(elapsed, duration) {
      const rem = Math.max(0, (duration || 90) - (elapsed || 0));
      if (rem === 0) return 'Arrived';
      const h = Math.floor(rem / 60), m = rem % 60;
      return h > 0 ? h + 'h\u2009' + m + 'm remaining' : m + 'm remaining';
    }

    async function refresh() {
      const [frameRes, stateRes] = await Promise.all([fetch('/api/frame'), fetch('/api/state')]);
      const frame = await frameRes.json();
      const state = await stateRes.json();
      drawFrame(frame.pixels);

      const f = state.tracked_flight || {};

      // Airline name
      document.getElementById('airline-name').innerHTML =
        f.airline_full_name || f.airline || f.airline_name || f.callsign ||
        '<span style="color:#444">—</span>';

      // Airports
      const airportsEl = document.getElementById('airports');
      if (f.origin_name || f.dest_name || f.origin_icao || f.dest_icao) {
        const orig = f.origin_name || f.origin_icao || '—';
        const dest = f.dest_name   || f.dest_icao   || '—';
        airportsEl.innerHTML =
          '<div class="airport-name">' + orig + '</div>' +
          '<div class="airport-arrow">&#8595;</div>' +
          '<div class="airport-name">' + dest + '</div>';
      } else {
        airportsEl.innerHTML = '<span class="no-data">No route data</span>';
      }

      // Metrics
      const trk = f.track_deg != null ? f.track_deg + '&deg;' : '<span style="color:#444">—</span>';
      const metrics = [
        ['ALT', fmtMetric(f.altitude_ft,  'ft')],
        ['SPD', fmtMetric(f.speed_kts,    'kts')],
        ['TRK', trk],
        ['VR',  fmtVR(f.vertical_rate_fpm)]
      ];
      document.getElementById('metrics').innerHTML = metrics.map(([lbl, val]) =>
        '<span class="metric-label">' + lbl + '</span><span class="metric-value">' + val + '</span>'
      ).join('');

      // Progress
      const pct = f.progress_pct ?? 0;
      document.getElementById('progress-fill').style.width = pct + '%';
      document.getElementById('progress-pct').textContent  = pct + '%';
      document.getElementById('progress-time').textContent =
        f.elapsed_min != null ? timeRemaining(f.elapsed_min, f.duration_min) : '—';
    }

    document.getElementById('refresh').addEventListener('click', refresh);
    refresh();
    setInterval(refresh, 1000);
  </script>
</body>
</html>`;
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------

const server = http.createServer((req, res) => {
  const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);

  if (req.method === 'GET' && url.pathname === '/api/health') {
    sendJson(res, 200, { ok: true, service: 'matrix-agent-preview', state_url: STATE_URL, last_error: latestError, time: new Date().toISOString() });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/state') {
    sendJson(res, 200, enrichState(latestState) || { stale: true, tracked_flight: null });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/frame') {
    sendJson(res, 200, { width: WIDTH, height: HEIGHT, pixels: Array.from(latestFrame) });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/') {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(htmlPage());
    return;
  }

  sendJson(res, 404, { error: 'not found' });
});

setInterval(pollState, POLL_MS);
pollState();

server.listen(PORT, () => {
  console.log(`matrix-agent preview listening on http://localhost:${PORT}`);
  console.log(`reading state from ${STATE_URL}`);
});
