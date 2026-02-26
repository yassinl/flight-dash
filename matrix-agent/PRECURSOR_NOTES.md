# Precursor Notes

This service is intentionally hardware-free.

Current output target:
- Browser preview (`/`) rendered from `64x32` RGB frame buffer.

Future hardware target:
- Replace browser output sink with `rpi-rgb-led-matrix` frame push while keeping:
  - state polling
n  - frame composition logic
  - fallback behavior

Migration plan:
1. Keep `renderFrame(state)` unchanged.
2. Convert returned RGB buffer to matrix canvas pixels.
3. Swap into active panel buffer with VSync.
4. Keep browser preview endpoint as debug mode.
