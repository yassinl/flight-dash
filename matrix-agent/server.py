#!/usr/bin/env python3
"""
mDNS-broadcasting web server for the flight dashboard config interface.
Registers 'flight-dashboard.local' via Zeroconf and serves a static HTML page.
"""

import os
import socket
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler

try:
    from zeroconf import Zeroconf, ServiceInfo
    MDNS_AVAILABLE = True
except ImportError:
    MDNS_AVAILABLE = False
    print("WARNING: zeroconf not installed — mDNS disabled. Run: pip install zeroconf")

HOSTNAME    = "flight-dashboard"
PORT        = 8080
STATIC_DIR  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")


def get_local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


def start_mdns() -> "Zeroconf | None":
    if not MDNS_AVAILABLE:
        return None

    ip       = get_local_ip()
    zc       = Zeroconf()
    info     = ServiceInfo(
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


class StaticHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=STATIC_DIR, **kwargs)

    def log_message(self, fmt, *args):  # suppress per-request noise
        pass


def main() -> None:
    zc     = start_mdns()
    server = HTTPServer(("0.0.0.0", PORT), StaticHandler)
    print(f"Web server: http://0.0.0.0:{PORT}  (static dir: {STATIC_DIR})")
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
