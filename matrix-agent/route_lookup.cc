#include "route_lookup.h"
#include "flight_data.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <ctime>
#include <string>

using json = nlohmann::json;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string http_get_with_header(const std::string &url,
                                        const std::string &header) {
    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    struct curl_slist *headers = curl_slist_append(nullptr, header.c_str());
    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    long http_code = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "[route] curl error: %s\n", curl_easy_strerror(res));
    else if (http_code == 429)
        fprintf(stderr, "[route] FA rate limited — disabling for this run\n");
    else if (http_code < 200 || http_code >= 300)
        fprintf(stderr, "[route] HTTP %ld for %s\n", http_code, url.c_str());
    return (res == CURLE_OK && http_code >= 200 && http_code < 300)
               ? body : std::string{};
}

// Set true after a 429 to stop burning credits for the rest of this run.
static bool s_fa_rate_limited = false;

// ---------------------------------------------------------------------------
// Primary: FlightAware AeroAPI
// GET /flights/{ident} — returns filed flight plan with real origin + dest
// ---------------------------------------------------------------------------
static RouteInfo flightaware_lookup(const std::string &fa_key,
                                    const std::string &callsign) {
    std::string url = "https://aeroapi.flightaware.com/aeroapi/flights/"
                    + callsign + "?ident_type=designator&max_pages=1";
    std::string body = http_get_with_header(url, "x-apikey: " + fa_key);
    if (body.empty()) return {};

    try {
        auto j = json::parse(body);
        auto &flights = j.at("flights");
        // Only accept en-route flights with 1-99% progress
        for (auto &f : flights) {
            std::string st  = f.value("status", "");
            int pct         = f.value("progress_percent", -1);
            std::string ori = (f.contains("origin")      && !f["origin"].is_null())
                            ? f["origin"].value("code_icao", "")      : "";
            std::string dst = (f.contains("destination") && !f["destination"].is_null())
                            ? f["destination"].value("code_icao", "") : "";
            fprintf(stderr, "[FA]  %s->%s  pct=%d  status=\"%s\"\n",
                    ori.c_str(), dst.c_str(), pct, st.c_str());
            if (st.find("En Route") == std::string::npos) continue;
            if (pct <= 0 || pct >= 100) continue;
            if (ori.empty() || dst.empty()) continue;
            RouteInfo r;
            r.origin_icao   = ori;
            r.dest_icao     = dst;
            r.progress_pct  = pct;
            r.aircraft_type = f.value("aircraft_type", "");
            // Parse estimated_in (ISO8601 "2026-03-14T23:30:00Z") → unix timestamp
            std::string eta_str = f.value("estimated_in", "");
            if (!eta_str.empty()) {
                struct tm tm = {};
                if (strptime(eta_str.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm)) {
                    tm.tm_isdst = 0;
                    r.eta_unix = timegm(&tm);
                }
            }
            return r;
        }
    } catch (...) {}
    return {};
}

// ---------------------------------------------------------------------------
// Fallback: OpenSky track-based inference
//   Origin      — nearest airport to the track's first waypoint
//   Destination — nearest major airport to the projected landing point
//                 (3° ILS glide slope, only when descending below 4000 m)
// ---------------------------------------------------------------------------
static RouteInfo track_lookup(const std::string &opensky_token,
                              const std::string &icao24,
                              float lat, float lon,
                              float altitude_m, float vertical_rate_ms,
                              float track_deg) {
    RouteInfo r;
    std::string url = "https://opensky-network.org/api/tracks/all?icao24="
                    + icao24 + "&time=0";
    std::string body = http_get_with_header(
        url, "Authorization: Bearer " + opensky_token);
    if (body.empty()) return r;

    try {
        auto j2 = json::parse(body);
        auto &path = j2.at("path");
        if (path.empty()) return r;

        auto &wp0 = path[0];
        if (!wp0[1].is_null() && !wp0[2].is_null())
            r.origin_icao = FlightData::nearest_airport_icao(
                wp0[1].get<float>(), wp0[2].get<float>(), 50.0f, false);

        if (vertical_rate_ms < -2.0f && altitude_m < 4000.0f) {
            const float glide_km  = altitude_m / (tanf(3.0f * 0.01745329f) * 1000.0f);
            const float track_rad = track_deg * 0.01745329f;
            const float cos_lat   = cosf(lat * 0.01745329f);
            float proj_lat = lat + (glide_km * cosf(track_rad)) / 111.0f;
            float proj_lon = lon + (glide_km * sinf(track_rad)) / (111.0f * cos_lat);
            r.dest_icao = FlightData::nearest_airport_icao(
                proj_lat, proj_lon, 60.0f, true);
        }
    } catch (...) {}

    return r;
}

// ---------------------------------------------------------------------------
RouteInfo lookup_route(const std::string &fa_key,
                       const std::string &opensky_token,
                       const std::string &callsign,
                       const std::string &icao24,
                       float lat, float lon,
                       float altitude_m,
                       float vertical_rate_ms,
                       float track_deg) {
    if (!fa_key.empty() && !s_fa_rate_limited) {
        RouteInfo r = flightaware_lookup(fa_key, callsign);
        if (!r.origin_icao.empty() && !r.dest_icao.empty()) {
            fprintf(stderr, "[route] FA hit  %s: %s -> %s\n",
                    callsign.c_str(), r.origin_icao.c_str(), r.dest_icao.c_str());
            return r;
        }
        fprintf(stderr, "[route] FA miss %s — falling back to track\n", callsign.c_str());
    }
    return track_lookup(opensky_token, icao24, lat, lon,
                        altitude_m, vertical_rate_ms, track_deg);
}
