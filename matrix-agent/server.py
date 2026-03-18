#!/usr/bin/env python3
"""
Flight Dashboard web server.
- Broadcasts flight-dashboard.local via mDNS (zeroconf)
- Serves WiFi config UI at /
- API: GET /api/status, GET /api/networks, POST /api/connect
"""

import json
import os
import socket
import subprocess
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler

try:
    from zeroconf import Zeroconf, ServiceInfo
    MDNS_AVAILABLE = True
except ImportError:
    MDNS_AVAILABLE = False
    print("WARNING: zeroconf not installed — run: pip3 install zeroconf")

HOSTNAME   = "flight-dashboard"
PORT       = 8080
STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")


def get_local_ip() -> str:
    """Return the primary non-loopback IP (works on both WiFi and AP interfaces)."""
    for dest in ("8.8.8.8", "192.168.4.1"):  # try internet, then AP gateway
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect((dest, 80))
            ip = s.getsockname()[0]
            s.close()
            if not ip.startswith("127."):
                return ip
        except OSError:
            pass
    return "127.0.0.1"


def start_mdns() -> "Zeroconf | None":
    if not MDNS_AVAILABLE:
        return None
    ip   = get_local_ip()
    zc   = Zeroconf()
    info = ServiceInfo(
        "_http._tcp.local.",
        f"{HOSTNAME}._http._tcp.local.",
        addresses=[socket.inet_aton(ip)],
        port=PORT,
        properties={"path": "/"},
        server=f"{HOSTNAME}.local.",
    )
    zc.register_service(info)
    print(f"mDNS: {HOSTNAME}.local → {ip}:{PORT}")
    return zc


# ---------------------------------------------------------------------------
# WiFi helpers (nmcli)
# ---------------------------------------------------------------------------

def _nmcli(*args) -> tuple[str, int]:
    r = subprocess.run(["nmcli"] + list(args), capture_output=True, text=True)
    return r.stdout.strip(), r.returncode


def get_wifi_status() -> dict:
    out, _ = _nmcli("-t", "-f", "ACTIVE,SSID,SIGNAL", "dev", "wifi")
    for line in out.splitlines():
        parts = line.split(":")
        if len(parts) >= 3 and parts[0] == "yes":
            return {"connected": True, "ssid": parts[1], "signal": parts[2]}
    # check if we're in AP/hotspot mode
    out2, _ = _nmcli("-t", "-f", "TYPE,STATE,CONNECTION", "dev")
    for line in out2.splitlines():
        parts = line.split(":")
        if parts[0] == "wifi" and parts[1] == "connected" and "hotspot" in parts[2].lower():
            return {"connected": False, "hotspot": True, "ssid": parts[2]}
    return {"connected": False, "hotspot": False}


def scan_networks() -> list[dict]:
    _nmcli("dev", "wifi", "rescan")
    out, _ = _nmcli("-t", "-f", "SSID,SIGNAL,SECURITY", "dev", "wifi", "list")
    seen, networks = set(), []
    for line in out.splitlines():
        parts = line.split(":")
        if len(parts) >= 3 and parts[0] and parts[0] not in seen:
            seen.add(parts[0])
            try:
                sig = int(parts[1])
            except ValueError:
                sig = 0
            networks.append({"ssid": parts[0], "signal": sig, "security": parts[2]})
    return sorted(networks, key=lambda n: -n["signal"])


def connect_wifi(ssid: str, password: str) -> tuple[bool, str]:
    # Remove any existing saved connection for this SSID first
    _nmcli("con", "delete", ssid)
    _, code = _nmcli("dev", "wifi", "connect", ssid, "password", password)
    if code == 0:
        return True, "Connected"
    # Try without password (open network)
    if not password:
        _, code2 = _nmcli("dev", "wifi", "connect", ssid)
        if code2 == 0:
            return True, "Connected"
    return False, "Connection failed — check password"


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=STATIC_DIR, **kwargs)

    def do_GET(self):
        if self.path == "/api/status":
            self._json(get_wifi_status())
        elif self.path == "/api/networks":
            self._json(scan_networks())
        else:
            super().do_GET()

    def do_POST(self):
        if self.path == "/api/connect":
            length = int(self.headers.get("Content-Length", 0))
            body   = json.loads(self.rfile.read(length))
            ok, msg = connect_wifi(body.get("ssid", ""), body.get("password", ""))
            self._json({"ok": ok, "message": msg})
        else:
            self.send_error(404)

    def _json(self, data: dict):
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    zc     = start_mdns()
    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Serving http://0.0.0.0:{PORT}  (static: {STATIC_DIR})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if zc:
            zc.close()


if __name__ == "__main__":
    main()
