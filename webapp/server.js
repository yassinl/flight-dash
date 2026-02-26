const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = Number(process.env.PORT || 8080);
const FLIGHT_PROVIDER = (process.env.FLIGHT_PROVIDER || 'mock').toLowerCase();
const OPENSKY_USERNAME = process.env.OPENSKY_USERNAME || '';
const OPENSKY_PASSWORD = process.env.OPENSKY_PASSWORD || '';
const DATA_DIR = path.join(__dirname, 'data');
const CONFIG_PATH = path.join(DATA_DIR, 'config.json');

const PUBLIC_AIRLINES = [
  { code: 'DL', name: 'Delta Air Lines' },
  { code: 'UA', name: 'United Airlines' },
  { code: 'AA', name: 'American Airlines' },
  { code: 'B6', name: 'JetBlue' },
  { code: 'WN', name: 'Southwest' },
  { code: 'AS', name: 'Alaska Airlines' }
];

const AIRLINE_NAMES = {
  AAL: 'American Airlines',
  AA: 'American Airlines',
  DAL: 'Delta Air Lines',
  DL: 'Delta Air Lines',
  UAL: 'United Airlines',
  UA: 'United Airlines',
  JBU: 'JetBlue',
  B6: 'JetBlue',
  SWA: 'Southwest',
  WN: 'Southwest',
  ASA: 'Alaska Airlines',
  AS: 'Alaska Airlines',
  FFT: 'Frontier Airlines',
  F9: 'Frontier Airlines',
  NKS: 'Spirit Airlines',
  NK: 'Spirit Airlines'
};

const PRIVATE_PREFIX_BLOCKLIST = ['N', 'EJA', 'LXJ', 'GAJ'];

const defaultConfig = {
  wifi_configured: false,
  location_source: 'ip',
  auto_location_enabled: true,
  latitude: 40.7128,
  longitude: -74.006,
  radius_miles: 100,
  tracked_flight_id: null,
  brightness_pct: 60,
  refresh_seconds: 15
};

function ensureDataDir() {
  if (!fs.existsSync(DATA_DIR)) {
    fs.mkdirSync(DATA_DIR, { recursive: true });
  }
}

function loadConfig() {
  ensureDataDir();
  if (!fs.existsSync(CONFIG_PATH)) {
    fs.writeFileSync(CONFIG_PATH, JSON.stringify(defaultConfig, null, 2));
    return { ...defaultConfig };
  }
  try {
    const raw = fs.readFileSync(CONFIG_PATH, 'utf8');
    return { ...defaultConfig, ...JSON.parse(raw) };
  } catch (error) {
    return { ...defaultConfig };
  }
}

function saveConfig(config) {
  ensureDataDir();
  fs.writeFileSync(CONFIG_PATH, JSON.stringify(config, null, 2));
}

let config = loadConfig();
const flightTrackingStore = new Map();

function readRequestBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', (chunk) => {
      body += chunk;
      if (body.length > 1_000_000) {
        reject(new Error('Payload too large'));
      }
    });
    req.on('end', () => {
      if (!body) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(body));
      } catch (error) {
        reject(new Error('Invalid JSON'));
      }
    });
    req.on('error', reject);
  });
}

function sendJson(res, statusCode, payload) {
  const text = JSON.stringify(payload);
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store'
  });
  res.end(text);
}

function haversineMiles(lat1, lon1, lat2, lon2) {
  const toRad = (deg) => (deg * Math.PI) / 180;
  const R = 3958.8;
  const dLat = toRad(lat2 - lat1);
  const dLon = toRad(lon2 - lon1);
  const a =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(toRad(lat1)) * Math.cos(toRad(lat2)) * Math.sin(dLon / 2) ** 2;
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return R * c;
}

function buildFlight(airline, idx, nowMs) {
  const startOffsetMin = 15 + idx * 7;
  const durationMin = 80 + idx * 22;
  const startMs = nowMs - startOffsetMin * 60_000;
  const elapsedMin = Math.max(0, Math.floor((nowMs - startMs) / 60_000));
  const progressPct = Math.min(100, Math.round((elapsedMin / durationMin) * 100));

  const phase = nowMs / 1000 + idx * 13;
  const lat = config.latitude + Math.sin(phase / 150) * 0.6;
  const lon = config.longitude + Math.cos(phase / 130) * 0.8;
  const distanceMiles = haversineMiles(config.latitude, config.longitude, lat, lon);

  return {
    id: `${airline.code}${110 + idx}`,
    airline_code: airline.code,
    airline_name: airline.name,
    logo_url: null,
    callsign: `${airline.code}${2300 + idx}`,
    status: progressPct >= 100 ? 'arrived' : 'enroute',
    elapsed_min: Math.min(elapsedMin, durationMin),
    duration_min: durationMin,
    progress_pct: progressPct,
    lat,
    lon,
    distance_miles: Number(distanceMiles.toFixed(1)),
    is_public_airline: true
  };
}

function generateMockFlights(nowMs) {
  const flights = PUBLIC_AIRLINES.map((airline, idx) => buildFlight(airline, idx, nowMs));
  return flights
    .filter((flight) => flight.distance_miles <= config.radius_miles)
    .filter((flight) => flight.status !== 'arrived')
    .sort((a, b) => a.distance_miles - b.distance_miles);
}

function decodeCallsign(raw) {
  return String(raw || '').trim().toUpperCase();
}

function airlineFromCallsign(callsign) {
  if (!callsign) return { code: null, name: null };
  const three = callsign.slice(0, 3);
  const two = callsign.slice(0, 2);
  const code = AIRLINE_NAMES[three] ? three : AIRLINE_NAMES[two] ? two : null;
  return {
    code,
    name: code ? AIRLINE_NAMES[code] : null
  };
}

function isLikelyPublicAirline(callsign) {
  if (!callsign) return false;
  if (!/^[A-Z]{2,3}\d+/.test(callsign)) return false;
  return !PRIVATE_PREFIX_BLOCKLIST.some((prefix) => callsign.startsWith(prefix));
}

function getDurationAndProgress(flightId, nowMs) {
  if (!flightTrackingStore.has(flightId)) {
    const durationMin = 90 + (Math.abs(flightId.split('').reduce((a, c) => a + c.charCodeAt(0), 0)) % 70);
    flightTrackingStore.set(flightId, {
      firstSeenMs: nowMs,
      durationMin
    });
  }

  const stored = flightTrackingStore.get(flightId);
  const elapsedMin = Math.max(0, Math.floor((nowMs - stored.firstSeenMs) / 60_000));
  const progressPct = Math.max(0, Math.min(100, Math.round((elapsedMin / stored.durationMin) * 100)));

  return {
    elapsedMin: Math.min(elapsedMin, stored.durationMin),
    durationMin: stored.durationMin,
    progressPct
  };
}

async function fetchOpenSkyFlights(nowMs) {
  const lat = Number(config.latitude);
  const lon = Number(config.longitude);
  const deltaDeg = Number(config.radius_miles) / 69;
  const lamin = lat - deltaDeg;
  const lamax = lat + deltaDeg;
  const lonScale = Math.max(0.2, Math.cos((lat * Math.PI) / 180));
  const lomin = lon - deltaDeg / lonScale;
  const lomax = lon + deltaDeg / lonScale;

  const url = `https://opensky-network.org/api/states/all?lamin=${lamin.toFixed(4)}&lomin=${lomin.toFixed(4)}&lamax=${lamax.toFixed(4)}&lomax=${lomax.toFixed(4)}`;

  const headers = {};
  if (OPENSKY_USERNAME && OPENSKY_PASSWORD) {
    const token = Buffer.from(`${OPENSKY_USERNAME}:${OPENSKY_PASSWORD}`).toString('base64');
    headers.Authorization = `Basic ${token}`;
  }

  const response = await fetch(url, { headers });
  if (!response.ok) {
    throw new Error(`OpenSky request failed with status ${response.status}`);
  }

  const payload = await response.json();
  const states = Array.isArray(payload.states) ? payload.states : [];

  const flights = states
    .map((row) => {
      const icao24 = String(row[0] || '').trim();
      const callsign = decodeCallsign(row[1]);
      const lonValue = row[5];
      const latValue = row[6];
      const velocity = Number(row[9] || 0);
      const onGround = Boolean(row[8]);

      if (!icao24 || typeof latValue !== 'number' || typeof lonValue !== 'number') return null;
      if (onGround) return null;
      if (!isLikelyPublicAirline(callsign)) return null;

      const distanceMiles = haversineMiles(lat, lon, latValue, lonValue);
      if (distanceMiles > config.radius_miles) return null;

      const airline = airlineFromCallsign(callsign);
      const id = callsign || icao24;
      const timeline = getDurationAndProgress(id, nowMs);

      return {
        id,
        airline_code: airline.code,
        airline_name: airline.name,
        logo_url: null,
        callsign,
        status: 'enroute',
        elapsed_min: timeline.elapsedMin,
        duration_min: timeline.durationMin,
        progress_pct: timeline.progressPct,
        lat: latValue,
        lon: lonValue,
        distance_miles: Number(distanceMiles.toFixed(1)),
        speed_kts: Math.round(velocity * 1.94384),
        is_public_airline: true
      };
    })
    .filter(Boolean)
    .sort((a, b) => a.distance_miles - b.distance_miles)
    .slice(0, 20);

  return flights;
}

async function getFlights(nowMs) {
  if (FLIGHT_PROVIDER === 'opensky') {
    try {
      return await fetchOpenSkyFlights(nowMs);
    } catch (error) {
      console.error(`OpenSky unavailable, falling back to mock provider: ${String(error.message || error)}`);
      return generateMockFlights(nowMs);
    }
  }
  return generateMockFlights(nowMs);
}

function computeAlertLevel(flightCount, windKts) {
  if (windKts >= 28 || flightCount === 0) return 'alert';
  if (windKts >= 18 || flightCount <= 2) return 'caution';
  return 'normal';
}

async function currentState() {
  const now = Date.now();
  const flights = await getFlights(now);
  const tracked =
    flights.find((flight) => flight.id === config.tracked_flight_id) ||
    flights[0] ||
    null;

  const windKts = 8 + Math.round((Math.sin(now / 200000) + 1) * 11);
  const visibilityM = 3000 + Math.round((Math.cos(now / 260000) + 1) * 3500);
  const alertLevel = computeAlertLevel(flights.length, windKts);

  return {
    schema_version: 1,
    timestamp: new Date(now).toISOString(),
    provider: FLIGHT_PROVIDER,
    location: { lat: config.latitude, lon: config.longitude, source: config.location_source },
    weather: {
      condition: windKts > 22 ? 'windy' : 'clear',
      wind_kts: windKts,
      visibility_m: visibilityM,
      alert_level: alertLevel
    },
    tracked_flight: tracked,
    nearby_count: flights.length,
    stale: false
  };
}

const server = http.createServer(async (req, res) => {
  if (!req.url) {
    sendJson(res, 404, { error: 'Not found' });
    return;
  }

  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

  if (req.method === 'GET' && url.pathname === '/api/health') {
    sendJson(res, 200, {
      ok: true,
      service: 'webapp',
      flight_provider: FLIGHT_PROVIDER,
      time: new Date().toISOString()
    });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/config') {
    sendJson(res, 200, config);
    return;
  }

  if (req.method === 'POST' && url.pathname === '/api/config') {
    try {
      const body = await readRequestBody(req);
      config = { ...config, ...body };
      saveConfig(config);
      sendJson(res, 200, { ok: true, config });
    } catch (error) {
      sendJson(res, 400, { ok: false, error: String(error.message || error) });
    }
    return;
  }

  if (req.method === 'POST' && url.pathname === '/api/location/override') {
    try {
      const body = await readRequestBody(req);
      if (typeof body.latitude !== 'number' || typeof body.longitude !== 'number') {
        sendJson(res, 400, { ok: false, error: 'latitude and longitude are required numbers' });
        return;
      }

      config.latitude = body.latitude;
      config.longitude = body.longitude;
      config.location_source = 'manual';
      config.auto_location_enabled = false;
      saveConfig(config);

      sendJson(res, 200, {
        ok: true,
        location: {
          latitude: config.latitude,
          longitude: config.longitude,
          source: config.location_source,
          auto_location_enabled: config.auto_location_enabled
        }
      });
    } catch (error) {
      sendJson(res, 400, { ok: false, error: String(error.message || error) });
    }
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/flights') {
    const flights = await getFlights(Date.now());
    sendJson(res, 200, {
      timestamp: new Date().toISOString(),
      provider: FLIGHT_PROVIDER,
      flights
    });
    return;
  }

  if (req.method === 'POST' && url.pathname === '/api/track') {
    try {
      const body = await readRequestBody(req);
      const candidate = (await getFlights(Date.now())).find((flight) => flight.id === body.flight_id);
      if (!candidate) {
        sendJson(res, 404, { ok: false, error: 'flight not found in current radius' });
        return;
      }
      config.tracked_flight_id = candidate.id;
      saveConfig(config);
      sendJson(res, 200, { ok: true, tracked_flight_id: config.tracked_flight_id });
    } catch (error) {
      sendJson(res, 400, { ok: false, error: String(error.message || error) });
    }
    return;
  }

  if (req.method === 'GET' && url.pathname === '/api/state/current') {
    sendJson(res, 200, await currentState());
    return;
  }

  sendJson(res, 404, { error: 'Not found' });
});

server.listen(PORT, () => {
  console.log(`webapp listening on http://localhost:${PORT}`);
  console.log(`flight provider: ${FLIGHT_PROVIDER}`);
});
