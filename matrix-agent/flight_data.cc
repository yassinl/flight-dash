#include "flight_data.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <string.h>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Placeholder data — fill in once API keys are available
// ---------------------------------------------------------------------------
static const char *OPENSKY_BASE    = "https://opensky-network.org/api";
static const char *OPENSKY_TOKEN_URL =
    "https://auth.opensky-network.org/auth/realms/opensky-network"
    "/protocol/openid-connect/token";
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
    state_.icao24        = icao24;
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

    // --- Route (origin/dest ICAO) from yesterday's completed flight ---
    // /flights/aircraft is batch-processed nightly; query 1-3 days ago to get
    // the most recent completed leg for this aircraft.
    time_t now = time(nullptr);
    std::ostringstream route_url;
    route_url << OPENSKY_BASE << "/flights/aircraft"
              << "?icao24=" << icao24_
              << "&begin=" << (now - 3 * 86400)
              << "&end="   << (now - 86400);
    std::string route_body = http_get(route_url.str());
    if (!route_body.empty()) parse_route(route_body);

    // --- Airport positions (for great-circle progress) ---
    if (!state_.origin_icao.empty() && !state_.dest_icao.empty()) {
        float olat, olon, dlat, dlon;
        if (fetch_airport_pos(state_.origin_icao, olat, olon) &&
            fetch_airport_pos(state_.dest_icao,   dlat, dlon)) {
            state_.origin_lat = olat;  state_.origin_lon = olon;
            state_.dest_lat   = dlat;  state_.dest_lon   = dlon;
            state_.has_airport_pos = true;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
bool FlightData::ensure_token() {
    if (!token_.empty() && time(nullptr) < token_expiry_ - 60)
        return true;  // still valid

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    std::string body;
    std::string post = "grant_type=client_credentials"
                       "&client_id="     + client_id_ +
                       "&client_secret=" + client_secret_;

    curl_easy_setopt(curl, CURLOPT_URL,           OPENSKY_TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    post.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "token fetch error: %s\n", curl_easy_strerror(res));
        return false;
    }

    try {
        auto j = json::parse(body);
        token_        = j.at("access_token").get<std::string>();
        long expires_in = j.value("expires_in", 1800L);
        token_expiry_ = time(nullptr) + expires_in;
        return true;
    } catch (const std::exception &e) {
        fprintf(stderr, "token parse error: %s\n", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
std::string FlightData::http_get(const std::string &url) {
    if (!ensure_token()) return {};

    CURL *curl = curl_easy_init();
    if (!curl) return {};

    std::string body;
    std::string auth_header = "Authorization: Bearer " + token_;
    struct curl_slist *headers = curl_slist_append(nullptr, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    long http_code = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK)
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
    else if (http_code < 200 || http_code >= 300)
        fprintf(stderr, "HTTP %ld for %s\n", http_code, url.c_str());

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK && http_code >= 200 && http_code < 300) ? body : std::string{};
}

// ---------------------------------------------------------------------------
// OpenSky /states/all state vector indices (0-based):
//  0  icao24        1  callsign      2  origin_country
//  3  time_position 4  last_contact  5  longitude      6  latitude
//  7  baro_altitude 8  on_ground     9  velocity      10  true_track
// 11  vertical_rate 13 geo_altitude  14 squawk
// ---------------------------------------------------------------------------
bool FlightData::parse_state(const std::string &body) {
    try {
        auto j = json::parse(body);
        auto &states = j.at("states");
        if (states.is_null() || states.empty()) return false;

        auto &s = states[0];

        state_.callsign          = s[1].is_null()  ? icao24_ : s[1].get<std::string>();
        state_.longitude         = s[5].is_null()  ? 0.f     : s[5].get<float>();
        state_.latitude          = s[6].is_null()  ? 0.f     : s[6].get<float>();
        state_.altitude_m        = s[7].is_null()  ? 0.f     : s[7].get<float>();
        state_.on_ground         = s[8].is_null()  ? false   : s[8].get<bool>();
        state_.speed_ms          = s[9].is_null()  ? 0.f     : s[9].get<float>();
        state_.track_deg         = s[10].is_null() ? 0.f     : s[10].get<float>();
        state_.vertical_rate_ms  = s[11].is_null() ? 0.f     : s[11].get<float>();
        state_.geo_altitude_m    = (s.size() > 13 && !s[13].is_null()) ? s[13].get<float>() : 0.f;
        state_.squawk            = (s.size() > 14 && !s[14].is_null()) ? s[14].get<std::string>() : "";
        state_.last_contact_unix = s[4].is_null()  ? 0       : (time_t)s[4].get<long>();
        state_.valid             = true;
        return true;
    } catch (const std::exception &e) {
        fprintf(stderr, "parse_state error: %s\n", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// OpenSky /flights/aircraft — array of completed flight records (batch, nightly).
// We take the most recent entry to get origin/dest ICAO codes.
// ---------------------------------------------------------------------------
bool FlightData::parse_route(const std::string &body) {
    try {
        auto j = json::parse(body);
        if (!j.is_array() || j.empty()) return false;

        auto &f = j.back();
        if (f.contains("estDepartureAirport") && !f["estDepartureAirport"].is_null())
            state_.origin_icao = f["estDepartureAirport"].get<std::string>();
        if (f.contains("estArrivalAirport") && !f["estArrivalAirport"].is_null())
            state_.dest_icao   = f["estArrivalAirport"].get<std::string>();
        if (f.contains("callsign") && !f["callsign"].is_null())
            state_.callsign    = f["callsign"].get<std::string>();

        return true;
    } catch (const std::exception &e) {
        fprintf(stderr, "parse_route error: %s\n", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Nominatim (OpenStreetMap) — free, no API key, returns lat/lon for an ICAO.
// ---------------------------------------------------------------------------
bool FlightData::fetch_airport_pos(const std::string &icao, float &lat, float &lon) {
    std::string url = "https://nominatim.openstreetmap.org/search"
                      "?q=" + icao + "+airport&format=json&limit=1";

    CURL *curl = curl_easy_init();
    if (!curl) return false;
    std::string body;
    // Nominatim requires a User-Agent
    struct curl_slist *headers = curl_slist_append(nullptr,
        "User-Agent: flight-dash/1.0");
    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || body.empty()) return false;

    try {
        auto j = json::parse(body);
        if (!j.is_array() || j.empty()) return false;
        lat = std::stof(j[0].at("lat").get<std::string>());
        lon = std::stof(j[0].at("lon").get<std::string>());
        return true;
    } catch (const std::exception &e) {
        fprintf(stderr, "fetch_airport_pos(%s) error: %s\n", icao.c_str(), e.what());
        return false;
    }
}
