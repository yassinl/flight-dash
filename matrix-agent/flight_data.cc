#include "flight_data.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <sstream>
#include <stdexcept>
#include <stdio.h>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Placeholder data — fill in once API keys are available
// ---------------------------------------------------------------------------
static const char *OPENSKY_BASE    = "https://opensky-network.org/api";
static const char *PLACEHOLDER_AIRLINE       = "Delta Air Lines";
static const char *PLACEHOLDER_AIRCRAFT_TYPE = "B739";
static const char *PLACEHOLDER_ORIGIN_NAME   = "Seattle-Tacoma";
static const char *PLACEHOLDER_DEST_NAME     = "Paris Charles de Gaulle";

// ---------------------------------------------------------------------------
// libcurl write callback
// ---------------------------------------------------------------------------
static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// ---------------------------------------------------------------------------
FlightData::FlightData(const std::string &icao24,
                       const std::string &client_id,
                       const std::string &client_secret)
    : icao24_(icao24), client_id_(client_id), client_secret_(client_secret)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Pre-fill static placeholders so the display has something to show
    // even before the first successful API call.
    state_.callsign      = icao24;
    state_.airline       = PLACEHOLDER_AIRLINE;
    state_.aircraft_type = PLACEHOLDER_AIRCRAFT_TYPE;
    state_.origin_name   = PLACEHOLDER_ORIGIN_NAME;
    state_.dest_name     = PLACEHOLDER_DEST_NAME;
}

// ---------------------------------------------------------------------------
bool FlightData::refresh() {
    // --- Live state (position, speed, altitude, track, vertical rate) ---
    std::string state_url = std::string(OPENSKY_BASE)
                          + "/states/all?icao24=" + icao24_;
    std::string state_body = http_get(state_url);
    if (state_body.empty()) return false;
    if (!parse_state(state_body)) return false;

    // --- Route info (origin / destination airports) ---
    // OpenSky only keeps flight records for a time window; query last 2 hours.
    time_t now  = time(nullptr);
    time_t begin = now - 7200;  // 2 hours ago
    std::ostringstream route_url;
    route_url << OPENSKY_BASE << "/flights/aircraft"
              << "?icao24=" << icao24_
              << "&begin=" << begin
              << "&end="   << now;
    std::string route_body = http_get(route_url.str());
    if (!route_body.empty()) parse_route(route_body);  // non-fatal if missing

    return true;
}

// ---------------------------------------------------------------------------
std::string FlightData::http_get(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl) return {};

    std::string body;
    std::string auth = client_id_ + ":" + client_secret_;

    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERPWD,       auth.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));

    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? body : std::string{};
}

// ---------------------------------------------------------------------------
// OpenSky /states/all response shape:
// { "time": <int>, "states": [ [icao24, callsign, ..., baro_alt, on_ground,
//   velocity, true_track, vertical_rate, ...], ... ] }
// State vector indices (0-based):
//   0  icao24   1  callsign   7  baro_altitude   8  on_ground
//   9  velocity  10 true_track  11 vertical_rate
// ---------------------------------------------------------------------------
bool FlightData::parse_state(const std::string &body) {
    try {
        auto j = json::parse(body);
        auto &states = j.at("states");
        if (states.is_null() || states.empty()) return false;

        // Find our aircraft (there may be multiple if no icao24 filter matched,
        // but with the filter there should be exactly one).
        auto &s = states[0];

        state_.callsign      = s[1].is_null() ? icao24_ : s[1].get<std::string>();
        state_.altitude_m    = s[7].is_null() ? 0.f     : s[7].get<float>();
        state_.speed_ms      = s[9].is_null() ? 0.f     : s[9].get<float>();
        state_.track_deg     = s[10].is_null() ? 0.f    : s[10].get<float>();
        state_.vertical_rate_ms = s[11].is_null() ? 0.f : s[11].get<float>();
        state_.valid         = true;
        return true;
    } catch (const std::exception &e) {
        fprintf(stderr, "parse_state error: %s\n", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// OpenSky /flights/aircraft response is an array of flight records.
// We take the most recent one and pull estDepartureAirport / estArrivalAirport.
// ---------------------------------------------------------------------------
bool FlightData::parse_route(const std::string &body) {
    try {
        auto j = json::parse(body);
        if (!j.is_array() || j.empty()) return false;

        // Most recent flight is last in the array
        auto &f = j.back();
        if (!f["estDepartureAirport"].is_null())
            state_.origin_icao = f["estDepartureAirport"].get<std::string>();
        if (!f["estArrivalAirport"].is_null())
            state_.dest_icao   = f["estArrivalAirport"].get<std::string>();
        if (!f["callsign"].is_null())
            state_.callsign    = f["callsign"].get<std::string>();

        return true;
    } catch (const std::exception &e) {
        fprintf(stderr, "parse_route error: %s\n", e.what());
        return false;
    }
}
