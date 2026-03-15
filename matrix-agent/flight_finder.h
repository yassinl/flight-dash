#pragma once
// Shared flight discovery logic — used by both test_console and driver.
// Functions are identical to what test_console.cc uses (and proved to work).

#include "flight_data.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
using json = nlohmann::json;

struct LatLon { double lat = 0, lon = 0; bool ok = false; };
struct NearbyFlight { std::string icao24; std::string callsign; FlightState state; };

static size_t ff_write_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    static_cast<std::string *>(ud)->append(ptr, size * nmemb);
    return size * nmemb;
}

static LatLon get_location() {
    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL,            "http://ip-api.com/json");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  ff_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || body.empty()) return {};
    try {
        auto j = json::parse(body);
        if (j.value("status", "") != "success") return {};
        return { j["lat"].get<double>(), j["lon"].get<double>(), true };
    } catch (...) { return {}; }
}

static std::string fetch_token(const std::string &client_id,
                               const std::string &client_secret) {
    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    std::string post = "grant_type=client_credentials"
                       "&client_id="     + client_id +
                       "&client_secret=" + client_secret;
    curl_easy_setopt(curl, CURLOPT_URL,
        "https://auth.opensky-network.org/auth/realms/opensky-network"
        "/protocol/openid-connect/token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    post.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ff_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return {};
    try { return json::parse(body).at("access_token").get<std::string>(); }
    catch (...) { return {}; }
}

static std::vector<NearbyFlight> find_nearby(double lat, double lon,
                                             const std::string &token,
                                             int max_results = 50) {
    const double dlat = 100.0 / 69.0;
    const double dlon = 100.0 / (69.0 * std::cos(lat * M_PI / 180.0));
    std::ostringstream url;
    url << "https://opensky-network.org/api/states/all"
        << "?lamin=" << (lat - dlat) << "&lomin=" << (lon - dlon)
        << "&lamax=" << (lat + dlat) << "&lomax=" << (lon + dlon)
        << "&extended=1";
    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    std::string auth = "Authorization: Bearer " + token;
    struct curl_slist *hdrs = curl_slist_append(nullptr, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_URL,            url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  ff_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    fprintf(stderr, "[find_nearby] HTTP %ld  body_len=%zu  curl=%s\n",
            http_code, body.size(), curl_easy_strerror(res));
    if (res != CURLE_OK || body.empty()) return {};
    if (body.size() < 500) fprintf(stderr, "[find_nearby] body: %s\n", body.c_str());
    std::vector<NearbyFlight> results;
    try {
        auto j_root = json::parse(body);
        if (!j_root.contains("states") || j_root["states"].is_null()) {
            fprintf(stderr, "[find_nearby] states missing/null — full body: %.300s\n", body.c_str());
            return {};
        }
        auto &states = j_root["states"];
        if (states.empty()) { fprintf(stderr, "[find_nearby] states empty\n"); return {}; }
        for (auto &s : states) {
            if ((int)results.size() >= max_results) break;
            if (s[8].is_null() ? true : s[8].get<bool>()) continue;
            int cat = (s.size() > 17 && !s[17].is_null()) ? s[17].get<int>() : 0;
            if (cat >= 8 && cat <= 15) continue;
            std::string icao24   = s[0].is_null() ? "" : s[0].get<std::string>();
            std::string callsign = s[1].is_null() ? "" : s[1].get<std::string>();
            if (icao24.empty() || callsign.empty()) continue;
            while (!callsign.empty() && callsign.back() == ' ') callsign.pop_back();
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
            st.icao24            = icao24;  st.callsign         = callsign;
            st.longitude         = s[5].is_null()  ? 0.f : s[5].get<float>();
            st.latitude          = s[6].is_null()  ? 0.f : s[6].get<float>();
            st.altitude_m        = s[7].is_null()  ? 0.f : s[7].get<float>();
            st.on_ground         = s[8].is_null()  ? false : s[8].get<bool>();
            st.speed_ms          = s[9].is_null()  ? 0.f : s[9].get<float>();
            st.track_deg         = s[10].is_null() ? 0.f : s[10].get<float>();
            st.vertical_rate_ms  = s[11].is_null() ? 0.f : s[11].get<float>();
            st.geo_altitude_m    = (s.size() > 13 && !s[13].is_null()) ? s[13].get<float>() : 0.f;
            st.squawk            = (s.size() > 14 && !s[14].is_null()) ? s[14].get<std::string>() : "";
            st.last_contact_unix = s[4].is_null()  ? 0   : (time_t)s[4].get<long>();
            st.valid             = true;
            results.push_back({ icao24, callsign, st });
        }
    } catch (...) {}
    return results;
}
