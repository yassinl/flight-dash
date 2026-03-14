#pragma once

#include <string>

// All live state for a single tracked flight.
struct FlightState {
    // Identification
    std::string callsign;       // e.g. "DAL442"
    std::string airline;        // e.g. "Delta Air Lines"
    std::string aircraft_type;  // e.g. "B739"

    // Route
    std::string origin_icao;    // e.g. "KSEA"
    std::string origin_name;    // e.g. "Seattle-Tacoma"
    std::string dest_icao;      // e.g. "LFPG"
    std::string dest_name;      // e.g. "Paris Charles de Gaulle"

    // Live telemetry
    float altitude_m;           // barometric altitude in metres
    float speed_ms;             // ground speed in m/s
    float track_deg;            // true track, degrees (0=N, 90=E)
    float vertical_rate_ms;     // climb rate m/s (negative = descending)

    bool valid = false;         // false until first successful refresh
};

class FlightData {
public:
    // icao24: 6-char hex transponder address of the aircraft to track
    FlightData(const std::string &icao24,
               const std::string &client_id,
               const std::string &client_secret);

    // Fetch latest state from OpenSky. Returns true on success.
    bool refresh();

    const FlightState &state() const { return state_; }

private:
    std::string icao24_;
    std::string client_id_;
    std::string client_secret_;
    FlightState state_;

    // Perform a Basic-authenticated GET; returns raw response body.
    std::string http_get(const std::string &url);

    // Parse /states/all response into state_.
    bool parse_state(const std::string &body);

    // Parse /flights/aircraft response into route fields.
    bool parse_route(const std::string &body);
};
