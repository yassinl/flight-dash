#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string>

using json = nlohmann::json;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string http_get(const std::string &url,
                             const std::string &user,
                             const std::string &pass) {
    CURL *curl = curl_easy_init();
    if (!curl) return {};
    std::string body;
    std::string auth = user + ":" + pass;
    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERPWD,       auth.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? body : std::string{};
}

int main() {
    const char *api_base      = getenv("OPENSKY_API_KEY");
    const char *client_id     = getenv("CLIENT_ID");
    const char *client_secret = getenv("CLIENT_SECRET");

    if (!api_base || !client_id || !client_secret) {
        fprintf(stderr, "Missing env vars. Source /etc/matrix-agent/env first.\n");
        return 1;
    }

    // Fetch all states (no icao24 filter = returns many flights)
    std::string url = std::string(api_base) + "/states/all";
    printf("Fetching: %s\n\n", url.c_str());

    std::string body = http_get(url, client_id, client_secret);
    if (body.empty()) { fprintf(stderr, "Empty response\n"); return 1; }

    auto j = json::parse(body);
    auto &states = j.at("states");

    int count = 0;
    for (auto &s : states) {
        if (count >= 10) break;

        std::string icao24   = s[0].is_null() ? "?" : s[0].get<std::string>();
        std::string callsign = s[1].is_null() ? "?" : s[1].get<std::string>();
        std::string country  = s[2].is_null() ? "?" : s[2].get<std::string>();
        float lon            = s[5].is_null() ? 0.f : s[5].get<float>();
        float lat            = s[6].is_null() ? 0.f : s[6].get<float>();
        float alt_m          = s[7].is_null() ? 0.f : s[7].get<float>();
        bool  on_ground      = s[8].is_null() ? false : s[8].get<bool>();
        float speed_ms       = s[9].is_null() ? 0.f : s[9].get<float>();
        // float track_deg   = s[10].is_null() ? 0.f : s[10].get<float>();  // not needed yet
        // float vert_rate   = s[11].is_null() ? 0.f : s[11].get<float>();  // not needed yet

        printf("[%d] icao24=%-8s callsign=%-10s country=%-20s "
               "lat=%-9.4f lon=%-10.4f alt=%.0fm speed=%.1fm/s ground=%s\n",
               count + 1,
               icao24.c_str(), callsign.c_str(), country.c_str(),
               lat, lon, alt_m, speed_ms,
               on_ground ? "yes" : "no");
        ++count;
    }

    printf("\nTotal states in response: %zu\n", states.size());
    return 0;
}
