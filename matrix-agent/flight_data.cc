#include "flight_data.h"
#include "route_lookup.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <unordered_map>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Forward declarations for static data loaded lazily
// ---------------------------------------------------------------------------
static std::unordered_map<std::string, std::string> s_airlines;
static bool s_airlines_loaded = false;
static void load_airlines();

static std::string nearest_airport(float lat, float lon, float max_km,
                                   bool major_only = false);

// ---------------------------------------------------------------------------
// Placeholder data — fill in once API keys are available
// ---------------------------------------------------------------------------
static const char *OPENSKY_BASE    = "https://opensky-network.org/api";
static const char *OPENSKY_TOKEN_URL =
    "https://auth.opensky-network.org/auth/realms/opensky-network"
    "/protocol/openid-connect/token";
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
}

// ---------------------------------------------------------------------------
bool FlightData::refresh() {
    // --- Live state (position, speed, altitude, track, vertical rate) ---
    // Skip if already primed from a bounding-box query (saves 4 API credits).
    if (!state_.valid) {
        std::string state_url = std::string(OPENSKY_BASE)
                              + "/states/all?icao24=" + icao24_;
        std::string state_body = http_get(state_url);
        if (state_body.empty()) return false;
        if (!parse_state(state_body)) return false;
    }

    // Derive airline name from the 3-letter ICAO callsign prefix
    if (!s_airlines_loaded) load_airlines();
    state_.airline.clear();
    if (state_.callsign.size() >= 3) {
        auto it = s_airlines.find(state_.callsign.substr(0, 3));
        if (it != s_airlines.end()) state_.airline = it->second;
    }

    // --- Route (origin/dest) ---
    // FlightAware is primary (real filed flight plan); track-based inference is fallback.
    ensure_token();
    RouteInfo route = lookup_route(fa_key_, token_,
                                   state_.callsign, icao24_,
                                   state_.latitude, state_.longitude,
                                   state_.altitude_m, state_.vertical_rate_ms,
                                   state_.track_deg);
    state_.origin_icao    = route.origin_icao;
    state_.dest_icao      = route.dest_icao;
    state_.fa_progress_pct = route.progress_pct;
    state_.fa_eta_unix    = route.eta_unix;

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
// Airport database — loaded once from airports.csv (OurAirports public data).
// Set AIRPORTS_CSV env var to override the path; defaults to "airports.csv"
// in the working directory.
// ---------------------------------------------------------------------------
struct AirportPos { float lat, lon; std::string name; std::string type; };
static std::unordered_map<std::string, AirportPos> s_airports;
static bool s_airports_loaded = false;

static std::string next_csv_field(const std::string &line, size_t &pos) {
    std::string field;
    if (pos >= line.size()) return field;
    if (line[pos] == '"') {
        ++pos;
        while (pos < line.size() && line[pos] != '"') field += line[pos++];
        if (pos < line.size()) ++pos; // closing quote
    } else {
        while (pos < line.size() && line[pos] != ',') field += line[pos++];
    }
    if (pos < line.size() && line[pos] == ',') ++pos;
    return field;
}

// ---------------------------------------------------------------------------
// Airline database — loaded once from airlines.dat (OpenFlights public data).
// Set AIRLINES_DAT env var to override path; defaults to "airlines.dat".
// Format: ID, Name, Alias, IATA, ICAO, Callsign, Country, Active
// ---------------------------------------------------------------------------
static void load_airlines() {
    const char *path = getenv("AIRLINES_DAT");
    if (!path) path = "airlines.dat";
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "airlines.dat not found — set AIRLINES_DAT env var\n");
        s_airlines_loaded = true;
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        size_t pos = 0;
        std::string fields[5];
        for (int i = 0; i < 5; ++i) fields[i] = next_csv_field(line, pos);
        // col 1 = name, col 4 = ICAO prefix
        const std::string &name = fields[1];
        const std::string &icao = fields[4];
        if (icao.empty() || icao == "N/A" || icao == "\\N") continue;
        if (name.empty() || name == "\\N") continue;
        s_airlines[icao] = name;
    }
    fprintf(stderr, "loaded %zu airlines from %s\n", s_airlines.size(), path);
    s_airlines_loaded = true;
}

static void load_airports() {
    const char *path = getenv("AIRPORTS_CSV");
    if (!path) path = "airports.csv";
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "airports.csv not found — set AIRPORTS_CSV env var\n");
        s_airports_loaded = true;
        return;
    }
    std::string line;
    std::getline(f, line); // skip header
    while (std::getline(f, line)) {
        size_t pos = 0;
        std::string fields[6];
        for (int i = 0; i < 6; ++i) fields[i] = next_csv_field(line, pos);
        // col 1 = ident, col 2 = type, col 3 = name, col 4 = lat, col 5 = lon
        if (fields[2] == "heliport" || fields[2] == "balloonport" || fields[2] == "closed") continue;
        if (fields[1].empty() || fields[4].empty() || fields[5].empty()) continue;
        try {
            s_airports[fields[1]] = { std::stof(fields[4]), std::stof(fields[5]), fields[3], fields[2] };
        } catch (...) {}
    }
    fprintf(stderr, "loaded %zu airports from %s\n", s_airports.size(), path);
    s_airports_loaded = true;
}

// Find the nearest airport to (lat, lon) within max_km.
// If major_only, restricts to large_airport and medium_airport types.
static std::string nearest_airport(float lat, float lon, float max_km,
                                   bool major_only) {
    if (!s_airports_loaded) load_airports();
    std::string best;
    float best_d = max_km;
    const float cos_lat = cosf(lat * 0.01745329f);
    for (auto &kv : s_airports) {
        if (major_only && kv.second.type != "large_airport"
                       && kv.second.type != "medium_airport") continue;
        // Exclude military/government fields — commercial aircraft don't land there
        const std::string &n = kv.second.name;
        if (n.find("Air Force")   != std::string::npos) continue;
        if (n.find("AFB")         != std::string::npos) continue;
        if (n.find("Air Base")    != std::string::npos) continue;
        if (n.find("Naval")       != std::string::npos) continue;
        if (n.find("Army")        != std::string::npos) continue;
        if (n.find("Joint Base")  != std::string::npos) continue;
        if (n.find("Military")    != std::string::npos) continue;
        float dlat = (kv.second.lat - lat) * 111.0f;
        float dlon = (kv.second.lon - lon) * 111.0f * cos_lat;
        float d = sqrtf(dlat*dlat + dlon*dlon);
        if (d < best_d) { best_d = d; best = kv.first; }
    }
    return best;
}

std::string FlightData::nearest_airport_icao(float lat, float lon,
                                             float max_km, bool major_only) {
    return nearest_airport(lat, lon, max_km, major_only);
}

std::string FlightData::airline_name(const std::string &prefix) {
    if (!s_airlines_loaded) load_airlines();
    auto it = s_airlines.find(prefix);
    return (it != s_airlines.end()) ? it->second : "";
}

std::string FlightData::airport_name(const std::string &icao) {
    if (!s_airports_loaded) load_airports();
    auto it = s_airports.find(icao);
    return (it != s_airports.end()) ? it->second.name : "";
}

bool FlightData::fetch_airport_pos(const std::string &icao, float &lat, float &lon) {
    if (!s_airports_loaded) load_airports();
    auto it = s_airports.find(icao);
    if (it == s_airports.end()) {
        fprintf(stderr, "fetch_airport_pos: unknown airport %s\n", icao.c_str());
        return false;
    }
    lat = it->second.lat;
    lon = it->second.lon;
    return true;
}
