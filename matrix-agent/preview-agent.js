const http = require('http');

const WIDTH = 64;
const HEIGHT = 32;
const STATE_URL = process.env.STATE_URL || 'http://localhost:8080/api/state/current';
const PORT = Number(process.env.PORT || 8090);
const POLL_MS = Number(process.env.POLL_MS || 5000);

let latestState = null;
let latestError = null;
let latestFrame = new Uint8Array(WIDTH * HEIGHT * 3);

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
  ' ': ['000', '000', '000', '000', '000']
};

function frameIndex(x, y) {
  return (y * WIDTH + x) * 3;
}

function setPixel(frame, x, y, r, g, b) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
  const idx = frameIndex(x, y);
  frame[idx] = r;
  frame[idx + 1] = g;
  frame[idx + 2] = b;
}

function fillRect(frame, x, y, w, h, r, g, b) {
  for (let yy = y; yy < y + h; yy += 1) {
    for (let xx = x; xx < x + w; xx += 1) {
      setPixel(frame, xx, yy, r, g, b);
    }
  }
}

function clearFrame(frame, r, g, b) {
  for (let y = 0; y < HEIGHT; y += 1) {
    for (let x = 0; x < WIDTH; x += 1) {
      const idx = frameIndex(x, y);
      frame[idx] = r;
      frame[idx + 1] = g;
      frame[idx + 2] = b;
    }
  }
}

function drawChar(frame, x, y, char, r, g, b) {
  const glyph = font3x5[char] || font3x5[' '];
  for (let row = 0; row < glyph.length; row += 1) {
    for (let col = 0; col < glyph[row].length; col += 1) {
      if (glyph[row][col] === '1') {
        setPixel(frame, x + col, y + row, r, g, b);
      }
    }
  }
}

function drawText(frame, x, y, text, r, g, b) {
  for (let i = 0; i < text.length; i += 1) {
    drawChar(frame, x + i * 4, y, text[i], r, g, b);
  }
}

function hashColor(code) {
  let hash = 0;
  for (const ch of code) {
    hash = (hash * 31 + ch.charCodeAt(0)) & 0xffff;
  }
  const r = 80 + (hash % 176);
  const g = 80 + ((hash >> 3) % 176);
  const b = 80 + ((hash >> 7) % 176);
  return [r, g, b];
}

function renderFrame(state) {
  const frame = new Uint8Array(WIDTH * HEIGHT * 3);

  if (!state || !state.tracked_flight) {
    clearFrame(frame, 8, 8, 16);
    drawText(frame, 3, 12, 'NO DATA', 255, 60, 60);
    return frame;
  }

  const alert = state.weather?.alert_level || 'normal';
  const bg =
    alert === 'alert'
      ? [35, 0, 0]
      : alert === 'caution'
        ? [26, 14, 0]
        : [0, 18, 0];

  clearFrame(frame, bg[0], bg[1], bg[2]);

  const flight = state.tracked_flight;
  const code = (flight.airline_code || 'NA').slice(0, 2).toUpperCase();
  const callsign = (flight.callsign || flight.id || 'UNKNOWN').slice(0, 10).toUpperCase();
  const progress = Math.max(0, Math.min(100, Math.round(flight.progress_pct || 0)));

  const [lr, lg, lb] = hashColor(code);
  fillRect(frame, 1, 1, 8, 8, lr, lg, lb);
  drawText(frame, 2, 3, code, 0, 0, 0);

  drawText(frame, 12, 2, callsign.slice(0, 10), 220, 220, 255);

  fillRect(frame, 2, 14, 60, 6, 20, 20, 20);
  const progressW = Math.round((progress / 100) * 60);
  fillRect(frame, 2, 14, progressW, 6, 60, 210, 110);
  drawText(frame, 24, 22, `${String(progress)}%`, 255, 255, 255);

  const wind = Math.round(state.weather?.wind_kts || 0);
  drawText(frame, 2, 27, `W:${String(wind)}`, 255, 220, 130);

  return frame;
}

async function pollState() {
  try {
    const response = await fetch(STATE_URL);
    if (!response.ok) {
      throw new Error(`state fetch failed: ${response.status}`);
    }
    latestState = await response.json();
    latestFrame = renderFrame(latestState);
    latestError = null;
  } catch (error) {
    latestError = String(error.message || error);
    latestFrame = renderFrame(latestState);
  }
}

function sendJson(res, code, payload) {
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store'
  });
  res.end(JSON.stringify(payload));
}

function htmlPage() {
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Matrix Preview</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 16px; background:#111; color:#eee; }
    .card { max-width: 420px; margin: 0 auto; }
    canvas { width: 100%; image-rendering: pixelated; border: 1px solid #444; background: #000; }
    .meta { font-size: 14px; color: #bbb; margin-top: 8px; line-height: 1.4; }
    button { margin-top: 10px; padding: 10px 14px; border: 0; border-radius: 8px; background: #2d7df6; color: white; }
  </style>
</head>
<body>
  <div class="card">
    <h2>64x32 Matrix Preview</h2>
    <canvas id="matrix" width="64" height="32"></canvas>
    <div class="meta" id="meta">Loading...</div>
    <button id="refresh">Refresh now</button>
  </div>
  <script>
    const canvas = document.getElementById('matrix');
    const ctx = canvas.getContext('2d');
    const meta = document.getElementById('meta');
    const imageData = ctx.createImageData(64, 32);

    function drawFrame(pixels) {
      for (let i = 0, j = 0; i < pixels.length; i += 3, j += 4) {
        imageData.data[j] = pixels[i];
        imageData.data[j + 1] = pixels[i + 1];
        imageData.data[j + 2] = pixels[i + 2];
        imageData.data[j + 3] = 255;
      }
      ctx.putImageData(imageData, 0, 0);
    }

    async function refresh() {
      const [frameRes, stateRes] = await Promise.all([
        fetch('/api/frame'),
        fetch('/api/state')
      ]);
      const frame = await frameRes.json();
      const state = await stateRes.json();
      drawFrame(frame.pixels);

      const tracked = state.tracked_flight || {};
      meta.textContent = [
        'Flight: ' + (tracked.callsign || tracked.id || 'none'),
        'Progress: ' + (tracked.progress_pct ?? 'n/a') + '%',
        'Nearby: ' + (state.nearby_count ?? 0),
        'Alert: ' + (state.weather?.alert_level || 'n/a')
      ].join(' | ');
    }

    document.getElementById('refresh').addEventListener('click', refresh);
    refresh();
    setInterval(refresh, 1000);
  </script>
</body>
</html>`;
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);

  if (req.method === 'GET' && url.pathname === '/api/health') {
    sendJson(res, 200, {
      ok: true,
      service: 'matrix-agent-preview',
      state_url: STATE_URL,
      last_error: latestError,
      time: new Date().toISOString()
    });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/state') {
    sendJson(res, 200, latestState || { stale: true, tracked_flight: null });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/frame') {
    sendJson(res, 200, {
      width: WIDTH,
      height: HEIGHT,
      pixels: Array.from(latestFrame)
    });
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
