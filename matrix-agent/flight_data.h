#pragma once

#include <string>

// All live state for a single tracked flight.
struct FlightState {
    // Identification
    std::string icao24;         // e.g. "a0b1c2"
    std::string callsign;       // e.g. "DAL442"
    std::string airline;        // e.g. "Delta Air Lines"
    std::string aircraft_type;  // e.g. "B739"
    std::string squawk;         // transponder squawk code, e.g. "1234"

    // Route
    std::string origin_icao;    // e.g. "KSEA"
    std::string origin_name;    // e.g. "Seattle-Tacoma"
    std::string dest_icao;      // e.g. "LFPG"
    std::string dest_name;      // e.g. "Paris Charles de Gaulle"

    // Live position
    float latitude  = 0.f;     // decimal degrees
    float longitude = 0.f;
    float altitude_m     = 0.f; // barometric altitude in metres
    float geo_altitude_m = 0.f; // GPS/geometric altitude in metres
    bool  on_ground = false;

    // Live telemetry
    float speed_ms          = 0.f; // ground speed in m/s
    float track_deg         = 0.f; // true track, degrees (0=N, 90=E)
    float vertical_rate_ms  = 0.f; // climb rate m/s (negative = descending)

    // Timing (unix timestamps; 0 = unknown)
    time_t last_contact_unix = 0;  // last ADS-B ping received

    // Airport positions — populated by fetch_airport_pos(); enables progress calc
    float origin_lat = 0.f, origin_lon = 0.f;
    float dest_lat   = 0.f, dest_lon   = 0.f;
    bool  has_airport_pos = false;

    bool valid = false;         // false until first successful refresh
};

class FlightData {
public:
    // icao24: 6-char hex transponder address of the aircraft to track
    FlightData(const std::string &icao24,
               const std::string &client_id,
               const std::string &client_secret);

    // Fetch latest state + route + airport positions from OpenSky.
    bool refresh();

    const FlightState &state() const { return state_; }

private:
    std::string icao24_;
    std::string client_id_;
    std::string client_secret_;
    FlightState state_;

    std::string token_;
    long        token_expiry_ = 0;  // unix timestamp when token expires

    // Exchange client credentials for a Bearer token; caches result.
    bool ensure_token();

    // Bearer-authenticated GET; returns raw response body.
    std::string http_get(const std::string &url);

    // Parse /states/all response into state_.
    bool parse_state(const std::string &body);

    // Parse /flights/aircraft response into origin/dest ICAO codes.
    bool parse_route(const std::string &body);

    // Fetch lat/lon for an airport ICAO; returns false if unavailable.
    bool fetch_airport_pos(const std::string &icao, float &lat, float &lon);
};
