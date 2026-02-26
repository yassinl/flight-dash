# Weather + Flight LED Matrix Plan (64x32)

## Goal
Build a web app that monitors local weather + flights, then render a 64x32 visual scene and display it on a HUB75 LED matrix as the final output.

## High-Level Architecture
1. **Data Layer (Web App Backend)**
   - Fetch flight + weather data on a schedule.
   - Normalize into a compact internal model.
   - Compute a simple status/risk summary.

2. **UI Layer (Web App Frontend)**
   - Display full-resolution dashboard for human interaction.
   - Show the same data used by the matrix pipeline.

3. **Render Layer (Raylib 64x32 Scene)**
   - Draw a matrix-native scene at exactly `64x32`.
   - Keep visuals minimal: status color, flight count, weather icon, short text.
   - Export each frame as RGB pixel buffer (`64 * 32 * 3`).

4. **Hardware Output Layer (Raspberry Pi + hzeller)**
   - Receive the RGB frame buffer.
   - Copy buffer to matrix canvas.
   - Present using double buffering (`SwapOnVSync`) to avoid tearing/flicker.

## Why This Flow Works
- Web app handles rich data and UX.
- Raylib gives deterministic 64x32 rendering (what you draw is what panel shows).
- hzeller library handles difficult low-level panel timing and multiplexing.

## Data Contract (Renderer Input)
Use a compact object from backend to renderer, for example:
- `timestamp`
- `flight_count_nearby`
- `top_flight_callsign` (optional)
- `visibility_m`
- `wind_kts`
- `precip_level` (none/light/heavy)
- `alert_level` (`normal | caution | alert`)

## Matrix Scene Spec (MVP)
For v1, keep only these elements:
- Background color = alert level
- Top row mini weather icon (8x8)
- Large number for nearby flights
- One short ticker/label max
- Optional blink/pulse only in alert state

## Implementation Phases

### Phase 1: Backend + Web Dashboard (No Hardware)
- Set up APIs and caching.
- Build minimal dashboard.
- Define and freeze renderer input schema.

### Phase 2: Raylib 64x32 Renderer (No Hardware)
- Build render loop for fixed `64x32` target.
- Add scene templates (`normal/caution/alert`).
- Save frame snapshots for visual verification.

### Phase 3: Pi Matrix Driver Integration
- Build a small bridge app that takes RGB frame buffer and pushes to matrix.
- Use hzeller double buffering and brightness controls.
- Verify stable refresh and no major ghosting.

### Phase 4: End-to-End Pipeline
- Connect backend state -> renderer -> hardware output.
- Add update cadence (e.g., data every 15s, render at 20-30 FPS).
- Add watchdog/reconnect behavior for reliability.

## Practical Milestones
1. Dashboard shows live weather + flights.
2. Renderer shows same state in `64x32` simulation window.
3. Matrix displays static test image from renderer buffer.
4. Matrix displays live animated state updates.

## Risks and Mitigations
- **Power instability / panel artifacts**
  - Use adequate 5V supply, shared ground, conservative brightness first.
- **Unreadable visuals at 64x32**
  - Prioritize iconography + short labels; avoid dense text.
- **Data jitter / API failures**
  - Cache latest good payload; show stale indicator.
- **Too much scope early**
  - Keep MVP to 3 alert modes and 4 core metrics.

## Recommended Repo Layout
- `web/` : frontend + backend API logic
- `renderer/` : raylib 64x32 scene code
- `matrix-bridge/` : hzeller output app on Pi
- `shared/` : schema/types for state payload

## First Build Target (1-week)
By end of week 1:
- Dashboard running
- 64x32 raylib scene rendering from mock data
- One command to push a test frame to matrix hardware
