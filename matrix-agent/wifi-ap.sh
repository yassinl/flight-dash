#!/usr/bin/env bash
# wifi-ap.sh — run at boot; if no WiFi after 20s, bring up a hotspot
# so users can always reach flight-dashboard.local to configure WiFi.

HOTSPOT_CON="FlightDash-Hotspot"
HOTSPOT_SSID="FlightDash-Setup"
HOTSPOT_PASS="flightdash"
IFACE="wlan0"
WAIT=20

echo "[wifi-ap] Waiting up to ${WAIT}s for WiFi connection..."
for i in $(seq 1 $WAIT); do
    if nmcli -t -f TYPE,STATE dev | grep -q "wifi:connected"; then
        echo "[wifi-ap] WiFi connected. No hotspot needed."
        exit 0
    fi
    sleep 1
done

echo "[wifi-ap] No WiFi after ${WAIT}s — starting hotspot '${HOTSPOT_SSID}'"

# Create the hotspot connection profile if it does not exist yet
if ! nmcli con show "$HOTSPOT_CON" &>/dev/null; then
    nmcli con add type wifi ifname "$IFACE" \
        con-name  "$HOTSPOT_CON" \
        ssid      "$HOTSPOT_SSID" \
        802-11-wireless.mode ap \
        ipv4.method shared \
        wifi-sec.key-mgmt wpa-psk \
        wifi-sec.psk "$HOTSPOT_PASS"
    echo "[wifi-ap] Hotspot profile created."
fi

nmcli con up "$HOTSPOT_CON"
echo "[wifi-ap] Hotspot up. Connect to '${HOTSPOT_SSID}' (pw: ${HOTSPOT_PASS}) and open flight-dashboard.local"
