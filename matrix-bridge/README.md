# matrix-bridge

Raspberry Pi hardware driver for the flight-dash LED matrix display.

Polls the webapp state API, renders a 64×32 RGB frame using the built-in
3×5 pixel font, and pushes each frame to the HUB75 LED panel via the
[rpi-led-matrix](https://github.com/alexeden/rpi-led-matrix) Node.js bindings
(which wrap hzeller's `rpi-rgb-led-matrix` C++ library).

When run on a machine without compiled native bindings (e.g. during
development on a laptop) the driver falls back to **stub mode** and logs a
status summary to stdout on each tick.

---

## Prerequisites (Raspberry Pi only)

```
sudo apt-get install -y build-essential libgraphicsmagick++-dev libwebp-dev
npm install        # triggers native compilation via node-gyp
```

---

## Configuration

All options are set via environment variables:

| Variable         | Default                                      | Description                         |
|------------------|----------------------------------------------|-------------------------------------|
| `STATE_URL`      | `http://localhost:8080/api/state/current`    | Webapp state endpoint               |
| `ROWS`           | `32`                                         | LED panel rows                      |
| `COLS`           | `64`                                         | LED panel columns                   |
| `CHAIN`          | `1`                                          | Daisy-chain length                  |
| `PARALLEL`       | `1`                                          | Parallel chains                     |
| `BRIGHTNESS`     | `60`                                         | Panel brightness (0–100)            |
| `HARDWARE_PULSE` | `1`                                          | Set to `0` to disable hardware PWM  |
| `SLOWDOWN_GPIO`  | `3`                                          | GPIO slowdown (increase if flickering) |
| `POLL_MS`        | `5000`                                       | State poll interval in milliseconds |

---

## Running

```bash
# Start the webapp backend first (provides /api/state/current)
npm start --workspace=webapp

# Then start the matrix bridge (on the Pi)
npm start --workspace=matrix-bridge
```

For a 3-panel daisy-chain with anti-flicker flags (see `commands.md`):

```bash
CHAIN=3 PARALLEL=2 SLOWDOWN_GPIO=3 npm start --workspace=matrix-bridge
```

---

## Running as a systemd service

Create `/etc/systemd/system/matrix-bridge.service`:

```ini
[Unit]
Description=Flight Dash LED Matrix Bridge
After=network.target

[Service]
WorkingDirectory=/home/pi/flight-dash
ExecStart=/usr/bin/node matrix-bridge/index.js
Restart=on-failure
RestartSec=5
Environment=STATE_URL=http://localhost:8080/api/state/current
Environment=CHAIN=3
Environment=PARALLEL=2
Environment=SLOWDOWN_GPIO=3

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now matrix-bridge
```

---

## Scene layout (64×32)

```
┌──────────────────────────────────────────────────────────────────┐
│ ┌──────┐  CALLSIGN                                               │
│ │ IATA │  e.g. DL2301                                            │
│ └──────┘                                                         │
│                                                                  │
│  ████████████████████████░░░░░░░░░░░░░░░░░  (progress bar)       │
│                       42%                                        │
│  W:14                                                            │
└──────────────────────────────────────────────────────────────────┘
```

Background colour reflects the current alert level:
- **normal** — dark green
- **caution** — dark amber
- **alert** — dark red
