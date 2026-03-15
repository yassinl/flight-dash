#pragma once
#include <string>

struct RouteInfo {
    std::string origin_icao;
    std::string dest_icao;
    int         progress_pct = -1;   // FA's actual route progress, -1 = unknown
    std::string aircraft_type;           // ICAO type code e.g. "B737", empty if unknown
    time_t      eta_unix     = 0;    // estimated arrival unix timestamp, 0 = unknown
};

// Fetch origin/destination for a flight.
// Tries FlightAware AeroAPI first (if fa_key is non-empty), then falls back
// to OpenSky track-based inference (first waypoint → origin,
// glide-slope projection → destination when on approach).
//
// fa_key:           FlightAware AeroAPI key — leave empty to skip
// opensky_token:    OpenSky Bearer token (used by track fallback)
// callsign:         e.g. "SWA2225"
// icao24:           e.g. "ac2bc6"
// lat/lon:          current aircraft position (decimal degrees)
// altitude_m:       barometric altitude in metres
// vertical_rate_ms: m/s, negative = descending
// track_deg:        true track, degrees clockwise from north
RouteInfo lookup_route(const std::string &fa_key,
                       const std::string &opensky_token,
                       const std::string &callsign,
                       const std::string &icao24,
                       float lat, float lon,
                       float altitude_m,
                       float vertical_rate_ms,
                       float track_deg);
