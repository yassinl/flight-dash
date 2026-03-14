# scratch.md — deferred / out-of-scope items

## 1. OpenSky API integration
`driver.py` uses hardcoded dummy variables in `FLIGHT_DATA`.
Once you have an OpenSky API key, replace that dict with a live fetch:
- Endpoint: `https://opensky-network.org/api/states/all` or the `/flights` endpoint
- Auth: Basic HTTP auth (user:pass or anonymous with rate-limits)
- Poll on an interval (e.g. 30 s) and update the display data in-place
- Consider caching last-good data so the display never goes blank on API failure

## 2. Hardware-specific matrix configuration
`driver.py` assumes a 32×64 panel with `hardware_mapping = "regular"`.
Adjust in `build_matrix()` to match your physical panel:
- `options.rows` / `options.cols`
- `options.chain_length` (if chaining multiple panels)
- `options.hardware_mapping` — common values: `"adafruit-hat"`, `"adafruit-hat-pwm"`, `"regular"`
- `options.gpio_slowdown` — raise (1–4) if flickering, lower for performance

## 3. Font installation
`driver.py` looks for fonts at `./fonts/5x8.bdf` relative to the script.
The hzeller library ships BDF fonts at `rpi-rgb-led-matrix/fonts/`.
On the Pi, either:
- Symlink: `ln -s /path/to/rpi-rgb-led-matrix/fonts ./fonts`
- Or copy the fonts dir next to the scripts

## 4. hzeller Python bindings installation
The `rgbmatrix` module must be compiled on the Pi itself — it cannot be pip-installed from PyPI.
Steps on the Pi:
```
git clone https://github.com/hzeller/rpi-rgb-led-matrix
cd rpi-rgb-led-matrix/bindings/python
make build-python
sudo make install-python
```

## 5. Zeroconf / mDNS dependency
`server.py` requires the `zeroconf` Python package:
```
pip install zeroconf
```
Alternatively, you can skip the Python mDNS code entirely and configure
`/etc/avahi/services/flight-dashboard.service` (Avahi is pre-installed on Pi OS)
— this may be more reliable at boot than a Python process.

## 6. Systemd service deployment
The `.service` files in `systemd/` assume the project lives at `/home/pi/matrix-agent/`.
To deploy on the Pi:
```
sudo cp systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable matrix-driver matrix-web
sudo systemctl start  matrix-driver matrix-web
```
Adjust `User=` and `WorkingDirectory=` if the install path differs.

## 7. Web config UI (future)
`static/index.html` is currently a static status page.
A real config UI should allow:
- Changing display brightness
- Toggling scroll speed
- Entering/updating OpenSky API credentials
- Selecting a tracked flight or route
This will require a backend (e.g. Flask) writing to a shared config file
that `driver.py` watches and hot-reloads.

## 8. Error handling / watchdog
Neither service currently writes a health file or exposes a status endpoint.
Consider adding:
- A `/status` JSON endpoint in `server.py` showing last data refresh time
- Systemd `WatchdogSec=` + `sd_notify` calls in both scripts for hardware watchdog support
