// test_console.cc — standalone console test for the flight data pipeline.
//
// Does NOT require the rpi-rgb-led-matrix library; links only libcurl.
//
// Usage:
//   export CLIENT_ID=<opensky_username>
//   export CLIENT_SECRET=<opensky_password>
//   make test-console && ./test-console
//
// The program:
//   1. Fetches your approximate location from ip-api.com
//   2. Queries OpenSky /states/all for airborne aircraft in a ~2° bounding box
//   3. Picks the first result and hands it to FlightData for a full refresh
//   4. Prints the populated FlightState to stdout

#include "flight_data.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <vector>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// libcurl write callback
// ---------------------------------------------------------------------------
static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string http_get_anon(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? body : std::string{};
}

// ---------------------------------------------------------------------------
// Step 1: resolve approximate lat/lon from public IP via ip-api.com
// ---------------------------------------------------------------------------
struct LatLon { double lat = 0, lon = 0; bool ok = false; };

static LatLon get_location() {
    std::string body = http_get_anon("http://ip-api.com/json");
    if (body.empty()) return {};
    try {
        auto j = json::parse(body);
        if (j.value("status", "") != "success") return {};
        return { j["lat"].get<double>(), j["lon"].get<double>(), true };
    } catch (...) { return {}; }
}

// ---------------------------------------------------------------------------
// Step 2: find the first airborne aircraft near a lat/lon
// ---------------------------------------------------------------------------
struct NearbyFlight {
    std::string icao24;
    std::string callsign;
    FlightState state;
};

static std::string fetch_token(const std::string &client_id,
                               const std::string &client_secret) {
    const char *token_url =
        "https://auth.opensky-network.org/auth/realms/opensky-network"
        "/protocol/openid-connect/token";
    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    std::string post = "grant_type=client_credentials"
                       "&client_id="     + client_id +
                       "&client_secret=" + client_secret;
    curl_easy_setopt(curl, CURLOPT_URL,           token_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    post.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return {};
    try {
        return json::parse(body).at("access_token").get<std::string>();
    } catch (...) { return {}; }
}

static std::vector<NearbyFlight> find_nearby(double lat, double lon,
                                             const std::string &token,
                                             int max_results = 50) {
    // 100 miles ≈ 1.449° latitude; longitude degrees shrink with cos(lat)
    const double dlat = 100.0 / 69.0;
    const double dlon = 100.0 / (69.0 * std::cos(lat * M_PI / 180.0));
    std::ostringstream url;
    url << "https://opensky-network.org/api/states/all"
        << "?lamin=" << (lat - dlat)
        << "&lomin=" << (lon - dlon)
        << "&lamax=" << (lat + dlat)
        << "&lomax=" << (lon + dlon)
        << "&extended=1";  // include category field (index 17)

    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    std::string auth_header = "Authorization: Bearer " + token;
    struct curl_slist *headers = curl_slist_append(nullptr, auth_header.c_str());
    curl_easy_setopt(curl, CURLOPT_URL,           url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || body.empty()) return {};

    std::vector<NearbyFlight> results;
    try {
        auto j = json::parse(body);
        auto &states = j.at("states");
        if (states.is_null() || states.empty()) return {};

        for (auto &s : states) {
            if ((int)results.size() >= max_results) break;

            bool on_ground = s[8].is_null() ? true : s[8].get<bool>();
            if (on_ground) continue;

            // Skip obviously non-commercial categories (rotorcraft, gliders, UAVs, etc.)
            // Allow 0 (unknown) — many airliners don't broadcast category
            int category = (s.size() > 17 && !s[17].is_null()) ? s[17].get<int>() : 0;
            if (category >= 8 && category <= 15) continue;

std::string icao24   = s[0].is_null() ? "" : s[0].get<std::string>();
            std::string callsign = s[1].is_null() ? "" : s[1].get<std::string>();
            if (icao24.empty() || callsign.empty()) continue;

            while (!callsign.empty() && callsign.back() == ' ')
                callsign.pop_back();

            // 3 uppercase letters + digits, prefix must be a known airline
            if (callsign.size() < 4) continue;
            bool valid = true;
            for (int i = 0; i < 3; ++i)
                if (callsign[i] < 'A' || callsign[i] > 'Z') { valid = false; break; }
            if (!valid) continue;
            for (size_t i = 3; i < callsign.size(); ++i)
                if (callsign[i] < '0' || callsign[i] > '9') { valid = false; break; }
            if (!valid) continue;
            if (FlightData::airline_name(callsign.substr(0, 3)).empty()) continue;

            FlightState st;
            st.icao24           = icao24;
            st.callsign         = callsign;
            st.longitude        = s[5].is_null()  ? 0.f : s[5].get<float>();
            st.latitude         = s[6].is_null()  ? 0.f : s[6].get<float>();
            st.altitude_m       = s[7].is_null()  ? 0.f : s[7].get<float>();
            st.on_ground        = s[8].is_null()  ? false : s[8].get<bool>();
            st.speed_ms         = s[9].is_null()  ? 0.f : s[9].get<float>();
            st.track_deg        = s[10].is_null() ? 0.f : s[10].get<float>();
            st.vertical_rate_ms = s[11].is_null() ? 0.f : s[11].get<float>();
            st.geo_altitude_m   = (s.size() > 13 && !s[13].is_null()) ? s[13].get<float>() : 0.f;
            st.squawk           = (s.size() > 14 && !s[14].is_null()) ? s[14].get<std::string>() : "";
            st.last_contact_unix = s[4].is_null() ? 0 : (time_t)s[4].get<long>();
            st.valid            = true;
            results.push_back({ icao24, callsign, st });
        }
    } catch (const std::exception &e) {
        std::cerr << "find_nearby parse error: " << e.what() << "\n";
    }
    return results;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string fmt_unix(time_t t) {
    if (t == 0) return "unknown";
    char buf[32];
    struct tm *tm = gmtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", tm);
    return buf;
}

static std::string fmt_elapsed(long secs) {
    if (secs < 0) secs = 0;
    long h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ldh %02ldm %02lds", h, m, s);
    return buf;
}


static double haversine_km(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0;
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat/2)*sin(dlat/2)
             + cos(lat1*M_PI/180)*cos(lat2*M_PI/180)*sin(dlon/2)*sin(dlon/2);
    return R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

static void print_progress_bar(double pct, int width = 40) {
    int filled = (int)(pct / 100.0 * width + 0.5);
    std::cout << "  [";
    for (int i = 0; i < width; ++i) std::cout << (i < filled ? '=' : '-');
    std::cout << "] " << std::fixed << std::setprecision(1) << pct << "%\n";
}

// ---------------------------------------------------------------------------
// Step 3: pretty-print a FlightState
// ---------------------------------------------------------------------------
static void print_state(const FlightState &s) {
    std::cout << std::fixed << std::setprecision(1);

    std::cout << "\n========================================\n";
    std::cout << "  IDENTIFICATION\n";
    std::cout << "  icao24       : " << s.icao24 << "\n";
    std::cout << "  callsign     : " << s.callsign << "\n";
    if (s.callsign.size() >= 3) {
        std::string logo = s.callsign.substr(0, 3) + ".png";
        std::cout << "  logo         : " << logo << "\n";
    }
    std::cout << "  squawk       : " << (s.squawk.empty() ? "n/a" : s.squawk) << "\n";
    std::cout << "  airline      : " << (s.airline.empty() ? "unknown" : s.airline) << "\n";
    std::cout << "  aircraft     : " << (s.aircraft_type.empty() ? "unknown" : s.aircraft_type) << "\n";
    std::cout << "  on ground    : " << (s.on_ground ? "yes" : "no") << "\n";
    std::cout << "  data valid   : " << (s.valid    ? "yes" : "no") << "\n";

    std::cout << "\n  POSITION\n";
    std::cout << std::setprecision(4);
    std::cout << "  lat/lon      : " << s.latitude << " / " << s.longitude << "\n";
    std::cout << std::setprecision(1);
    std::cout << "  baro alt     : " << (s.altitude_m     * 3.28084f) << " ft"
              << "  (" << s.altitude_m << " m)\n";
    std::cout << "  geo  alt     : " << (s.geo_altitude_m * 3.28084f) << " ft"
              << "  (" << s.geo_altitude_m << " m)\n";
    std::cout << "  speed        : " << (s.speed_ms * 1.94384f) << " kts"
              << "  (" << s.speed_ms << " m/s)\n";
    std::cout << "  track        : " << s.track_deg << " deg\n";
    std::cout << "  vertical rate: " << s.vertical_rate_ms << " m/s  ("
              << (s.vertical_rate_ms >= 0 ? "climbing" : "descending") << ")\n";

    std::cout << "\n  ROUTE\n";
    auto fmt_airport = [](const std::string &icao) -> std::string {
        if (icao.empty()) return "unknown";
        std::string name = FlightData::airport_name(icao);
        return name.empty() ? icao : icao + "  (" + name + ")";
    };
    std::cout << "  origin       : " << fmt_airport(s.origin_icao) << "\n";
    std::cout << "  destination  : " << fmt_airport(s.dest_icao)   << "\n";

    std::cout << "\n  TIMING\n";
    std::cout << "  last contact : " << fmt_unix(s.last_contact_unix) << "\n";

    std::cout << "\n  PROGRESS\n";
    double pct = -1.0;
    if (s.fa_progress_pct >= 0) {
        // Use FA's actual route progress
        pct = s.fa_progress_pct;
    } else if (s.has_airport_pos) {
        // Fall back to haversine approximation
        double total_km   = haversine_km(s.origin_lat, s.origin_lon, s.dest_lat, s.dest_lon);
        double covered_km = haversine_km(s.origin_lat, s.origin_lon, s.latitude, s.longitude);
        pct = (total_km > 0) ? std::min(100.0, std::max(0.0, covered_km / total_km * 100.0)) : 0.0;
    }
    if (pct >= 0) {
        print_progress_bar(pct);
        // Remaining time: prefer FA's ETA, fall back to speed-based estimate
        if (s.fa_eta_unix > 0) {
            long remaining = (long)(s.fa_eta_unix - time(nullptr));
            if (remaining > 0)
                std::cout << "  est. remaining : " << fmt_elapsed(remaining) << "\n";
        } else if (s.has_airport_pos && s.speed_ms > 0) {
            double total_km   = haversine_km(s.origin_lat, s.origin_lon, s.dest_lat, s.dest_lon);
            double covered_km = haversine_km(s.origin_lat, s.origin_lon, s.latitude, s.longitude);
            long eta_secs = (long)((total_km - covered_km) * 1000.0 / s.speed_ms);
            if (eta_secs > 0)
                std::cout << "  est. remaining : " << fmt_elapsed(eta_secs) << "\n";
        }
    }

    std::cout << "========================================\n\n";
}

// ---------------------------------------------------------------------------
int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const char *client_id     = getenv("CLIENT_ID");
    const char *client_secret = getenv("CLIENT_SECRET");
    const char *fa_key        = getenv("FLIGHTAWARE_KEY");  // optional
    if (!client_id || !client_secret || !*client_id || !*client_secret) {
        std::cerr << "Set CLIENT_ID and CLIENT_SECRET environment variables.\n";
        return 1;
    }
    if (!fa_key || !*fa_key) {
        std::cerr << "[WARN] FLIGHTAWARE_KEY not set\n"
                     "       Route/progress data will be unavailable.\n"
                     "       Export it before running:\n"
                     "         export FLIGHTAWARE_KEY=your_key_here\n";
    }

    // 1. Locate the machine
    std::cout << "[1/3] Resolving location from IP...\n";
    LatLon loc = get_location();
    if (!loc.ok) {
        std::cerr << "Could not determine location from IP.\n";
        return 1;
    }
    std::cout << "      lat=" << loc.lat << "  lon=" << loc.lon << "\n";

    // 2. Find nearby commercial flights
    std::cout << "[2/3] Querying OpenSky for nearby commercial flights...\n";
    std::string token = fetch_token(client_id, client_secret);
    if (token.empty()) {
        std::cerr << "Failed to obtain OAuth2 token.\n";
        return 1;
    }
    auto candidates = find_nearby(loc.lat, loc.lon, token);
    if (candidates.empty()) {
        std::cerr << "No nearby commercial flights found.\n";
        return 1;
    }
    std::cout << "      Found " << candidates.size() << " candidates, searching for one with full route...\n";

    // 3. Linear search — stop at first flight with both origin and destination.
    // FA is only tried for the first 3 candidates to avoid burning rate limit.
    std::cout << "[3/3] Fetching flight data...\n";
    bool found = false;
    int fa_attempts = 0;
    for (auto &c : candidates) {
        std::cout << "      trying " << c.callsign << "...\n";
        FlightData fd(c.icao24, client_id, client_secret);
        fd.prime(c.state);
        if (fa_key && *fa_key && fa_attempts < 1) {
            fd.set_flightaware_key(fa_key);
            ++fa_attempts;
        }
        fd.refresh();
        const FlightState &s = fd.state();
        if (!s.origin_icao.empty() && !s.dest_icao.empty()) {
            double pct = -1.0;
            if (s.fa_progress_pct >= 0) {
                pct = s.fa_progress_pct;
            } else if (s.has_airport_pos) {
                double total_km   = haversine_km(s.origin_lat, s.origin_lon, s.dest_lat, s.dest_lon);
                double covered_km = haversine_km(s.origin_lat, s.origin_lon, s.latitude, s.longitude);
                pct = total_km > 0 ? covered_km / total_km * 100.0 : 0.0;
            }
            if (pct <= 0.0 || pct >= 100.0) continue;
            print_state(s);
            found = true;
            break;
        }
    }
    if (!found)
        std::cerr << "No nearby flight with a complete route found.\n";

    curl_global_cleanup();
    return 0;
}
