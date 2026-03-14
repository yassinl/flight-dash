# matrix-agent

Renders live flight information to a 3×2 panel RGB LED matrix (192×64 px) attached to a Raspberry Pi. Also ships a console-only test binary for iterating on the API pipeline without needing the Pi or the matrix hardware.

---

## Source files

### `flight_data.h` / `flight_data.cc`

Handles all network I/O and data parsing. No dependency on the LED matrix library — safe to compile and test anywhere `libcurl` and `nlohmann/json` are available.

**`FlightState` struct** — plain data bag written by `FlightData::refresh()`:
| field | type | description |
|---|---|---|
| `callsign` | string | ICAO callsign, e.g. `DAL442` |
| `airline` | string | Human-readable airline name (placeholder until a lookup DB is added) |
| `aircraft_type` | string | ICAO type code, e.g. `B739` (placeholder) |
| `origin_icao` / `origin_name` | string | Departure airport ICAO code + display name |
| `dest_icao` / `dest_name` | string | Arrival airport ICAO code + display name |
| `altitude_m` | float | Barometric altitude in metres |
| `speed_ms` | float | Ground speed in m/s |
| `track_deg` | float | True track in degrees (0 = N, 90 = E) |
| `vertical_rate_ms` | float | Climb rate in m/s; negative = descending |
| `valid` | bool | `false` until the first successful `refresh()` |

**`FlightData` class** — takes an ICAO24 transponder hex (`icao24`), an OpenSky username (`client_id`), and password (`client_secret`) at construction.

`refresh()` makes two calls:
1. `GET /api/states/all?icao24=<hex>` — live position / telemetry. Populates callsign, altitude, speed, track, vertical rate.
2. `GET /api/flights/aircraft?icao24=<hex>&begin=<t-2h>&end=<t>` — recent flight record. Populates origin/destination ICAO codes and corrects the callsign. Non-fatal if the flight record is too old or absent.

Both calls use HTTP Basic Auth (OpenSky free-tier credentials).

---

### `driver.cc`

The Pi production binary. Links against `rpi-rgb-led-matrix`.

- Reads `CLIENT_ID` and `CLIENT_SECRET` from the environment (set via the systemd `EnvironmentFile`).
- Constructs a `FlightData` for a hardcoded `icao24` (edit line 43 to target your aircraft).
- Spawns a background thread that calls `FlightData::refresh()` every 30 seconds, guarded by a mutex.
- Main loop runs at ~60 fps: locks the mutex, snapshots `FlightState`, renders three columns to the LED canvas, then swaps on VSync.

**Canvas layout (192 × 64 px, 3 chained 64×32 panels):**
```
[ LEFT 64px        ][ MIDDLE 64px       ][ RIGHT 64px        ]
  Airline name        Origin ICAO          ALT  xxxxx ft
  Callsign            Origin name          SPD  xxxx kts
  ──────────          >>>>                 TRK  xxx deg
  Aircraft type       Dest ICAO            V/R  ±x.x m/s
  LIVE / WAIT
```

Colors: yellow = airline, white = callsign / route, cyan = altitude / speed, green = climb / LIVE, red = descent.

---

### `test_console.cc`

Standalone console test — **no matrix library required**. Useful for validating the full API pipeline from any machine.

What it does:
1. Resolves your approximate lat/lon via `ip-api.com/json`.
2. Queries OpenSky `/states/all` with a ±2° bounding box around that location, picks the first airborne aircraft.
3. Constructs a `FlightData` for that ICAO24, calls `refresh()`.
4. Prints the full `FlightState` to stdout.

---

## Build

### Console test (no Pi required)

```bash
export CLIENT_ID=<your_opensky_username>
export CLIENT_SECRET=<your_opensky_password>
make test-console
./test-console
```

Dependencies: `libcurl-dev`, `nlohmann-json3-dev` (or vendored header).

### Pi production binary

```bash
# On the Pi, after building rpi-rgb-led-matrix
make
sudo ./driver
```

Full Pi setup (Python bindings for the web preview server):

```bash
sudo apt-get update && sudo apt-get install -y python3-dev cython3 \
  && make clean && make build-python && sudo make install-python \
  && python3 -c "import rgbmatrix; print('rgbmatrix import OK')"
```

---

## Environment variables

| variable | description |
|---|---|
| `CLIENT_ID` | OpenSky Network username |
| `CLIENT_SECRET` | OpenSky Network password |

Set them in `.env` (sourced by systemd `EnvironmentFile`) or export them in your shell for local testing.
