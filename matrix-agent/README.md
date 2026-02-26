# matrix-agent

Pi service that renders and pushes 64x32 frames to HUB75 matrix via `rpi-rgb-led-matrix`.

## Responsibilities
- Poll `webapp` state endpoint.
- Render minimal matrix scene.
- Display fallback states on errors.
- Keep smooth frame refresh independent of network polling.

## Render cadence
- Data poll: every 5-15s
- Frame update: 20-30 FPS

## Fallback screens
- `SETUP` (no config)
- `NO WIFI`
- `NO DATA`
- `API LIMIT`
