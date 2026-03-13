'use strict';

// matrix-bridge: Raspberry Pi hardware driver for the flight-dash LED matrix.
//
// Polls the webapp state API, renders a 64×32 RGB frame, and pushes it to
// the HUB75 LED panel via the rpi-led-matrix (hzeller) native bindings.
// When run outside a Raspberry Pi (or without compiled bindings) the driver
// falls back to stub mode and logs a status summary to stdout instead.
//
// Environment variables:
//   STATE_URL       — webapp state endpoint  (default: http://localhost:8080/api/state/current)
//   ROWS            — LED panel rows         (default: 32)
//   COLS            — LED panel columns      (default: 64)
//   CHAIN           — daisy-chain length     (default: 1)
//   PARALLEL        — parallel chains        (default: 1)
//   BRIGHTNESS      — panel brightness 0-100 (default: 60)
//   HARDWARE_PULSE  — set to 0 to disable    (default: 1)
//   SLOWDOWN_GPIO   — GPIO slowdown value    (default: 3)
//   POLL_MS         — state poll interval ms (default: 5000)

const STATE_URL = process.env.STATE_URL || 'http://localhost:8080/api/state/current';
const ROWS = Number(process.env.ROWS || 32);
const COLS = Number(process.env.COLS || 64);
const CHAIN = Number(process.env.CHAIN || 1);
const PARALLEL = Number(process.env.PARALLEL || 1);
const BRIGHTNESS = Number(process.env.BRIGHTNESS || 60);
const HARDWARE_PULSE = process.env.HARDWARE_PULSE !== '0';
const SLOWDOWN_GPIO = Number(process.env.SLOWDOWN_GPIO || 3);
const POLL_MS = Number(process.env.POLL_MS || 5000);

const WIDTH = COLS;
const HEIGHT = ROWS;
const MAX_PROGRESS_PCT = 100;

// ── 3×5 pixel bitmap font ────────────────────────────────────────────────────

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

// ── Pixel utilities ──────────────────────────────────────────────────────────

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
  for (let py = 0; py < HEIGHT; py += 1) {
    for (let px = 0; px < WIDTH; px += 1) {
      const idx = frameIndex(px, py);
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

// ── Frame renderer ───────────────────────────────────────────────────────────

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
  const progress = Math.max(0, Math.min(MAX_PROGRESS_PCT, Math.round(flight.progress_pct || 0)));

  const [lr, lg, lb] = hashColor(code);
  fillRect(frame, 1, 1, 8, 8, lr, lg, lb);
  drawText(frame, 2, 3, code, 0, 0, 0);

  drawText(frame, 12, 2, callsign.slice(0, 10), 220, 220, 255);

  fillRect(frame, 2, 14, 60, 6, 20, 20, 20);
  const progressW = Math.round((progress / MAX_PROGRESS_PCT) * 60);
  fillRect(frame, 2, 14, progressW, 6, 60, 210, 110);
  drawText(frame, 24, 22, `${progress}%`, 255, 255, 255);

  const wind = Math.round(state.weather?.wind_kts || 0);
  drawText(frame, 2, 27, `W:${wind}`, 255, 220, 130);

  return frame;
}

// ── Hardware binding ─────────────────────────────────────────────────────────

let matrix = null;

function initMatrix() {
  try {
    const { LedMatrix, GpioMapping } = require('rpi-led-matrix');
    matrix = new LedMatrix(
      {
        ...LedMatrix.defaultMatrixOptions(),
        rows: HEIGHT,
        cols: WIDTH,
        chainLength: CHAIN,
        parallel: PARALLEL,
        hardwareMapping: GpioMapping.Regular,
        disableHardwarePulsing: !HARDWARE_PULSE
      },
      {
        ...LedMatrix.defaultRuntimeOptions(),
        gpioSlowdown: SLOWDOWN_GPIO
      }
    );
    matrix.brightness(BRIGHTNESS);
    console.log(`LED matrix initialised (${WIDTH}×${HEIGHT}, chain=${CHAIN}, parallel=${PARALLEL})`);
  } catch (err) {
    console.warn(`Hardware bindings unavailable (${String(err.message || err)}); running in stub mode`);
    matrix = null;
  }
}

// ── Frame output ─────────────────────────────────────────────────────────────

function pushFrame(frame, state) {
  if (!matrix) {
    const flight = state?.tracked_flight;
    const callsign = flight ? (flight.callsign || flight.id || 'unknown') : 'none';
    const progress = flight ? `${Math.round(flight.progress_pct || 0)}%` : 'n/a';
    const alert = state?.weather?.alert_level || 'n/a';
    const wind = state?.weather?.wind_kts ?? 'n/a';
    console.log(
      `[stub] flight=${callsign} progress=${progress} alert=${alert} wind=${wind}kts`
    );
    return;
  }

  matrix.clear();
  for (let y = 0; y < HEIGHT; y += 1) {
    for (let x = 0; x < WIDTH; x += 1) {
      const idx = (y * WIDTH + x) * 3;
      const r = frame[idx];
      const g = frame[idx + 1];
      const b = frame[idx + 2];
      if (r || g || b) {
        matrix.fgColor({ r, g, b }).setPixel(x, y);
      }
    }
  }
  matrix.sync();
}

// ── State poller ─────────────────────────────────────────────────────────────

let latestState = null;

async function pollAndRender() {
  try {
    const response = await fetch(STATE_URL);
    if (!response.ok) {
      throw new Error(`state fetch failed: ${response.status}`);
    }
    latestState = await response.json();
  } catch (err) {
    console.warn(`poll failed: ${String(err.message || err)}`);
  }

  const frame = renderFrame(latestState);
  pushFrame(frame, latestState);
}

// ── Boot ─────────────────────────────────────────────────────────────────────

initMatrix();
setInterval(pollAndRender, POLL_MS);
pollAndRender();
console.log(`matrix-bridge started — polling ${STATE_URL} every ${POLL_MS}ms`);
