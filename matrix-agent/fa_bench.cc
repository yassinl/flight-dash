// fa_bench.cc — standalone FlightAware AeroAPI test
//
// Usage:
//   export FLIGHTAWARE_KEY=your_key
//   g++ -std=c++17 fa_bench.cc -o fa_bench -lcurl && ./fa_bench UAL1234
//
// Prints the raw JSON response then parses and displays origin, destination,
// status, and progress_percent for the first matching en-route flight.

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using json = nlohmann::json;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    static_cast<std::string *>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string http_get(const std::string &url, const std::string &api_key) {
    CURL *curl = curl_easy_init();
    if (!curl) { std::cerr << "curl_easy_init failed\n"; return {}; }

    std::string body;
    std::string auth = "x-apikey: " + api_key;
    struct curl_slist *headers = curl_slist_append(nullptr, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    long http_code = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    std::cerr << "HTTP " << http_code << "  (" << url << ")\n";
    if (res != CURLE_OK)
        std::cerr << "curl error: " << curl_easy_strerror(res) << "\n";

    return body;
}

int main(int argc, char **argv) {
    const char *key = getenv("FLIGHTAWARE_KEY");
    if (!key || !*key) {
        std::cerr << "Set FLIGHTAWARE_KEY env var\n";
        return 1;
    }

    const char *ident = (argc > 1) ? argv[1] : "UAL1";
    std::string url = std::string("https://aeroapi.flightaware.com/aeroapi/flights/")
                    + ident
                    + "?ident_type=designator&max_pages=1";

    std::cout << "Querying: " << url << "\n\n";
    std::string body = http_get(url, key);

    if (body.empty()) {
        std::cerr << "Empty response.\n";
        return 1;
    }

    // Print raw JSON
    std::cout << "--- RAW RESPONSE ---\n" << body << "\n\n";

    // Parse and print key fields
    try {
        auto j = json::parse(body);
        auto &flights = j.at("flights");
        std::cout << "--- PARSED (" << flights.size() << " flights) ---\n";
        for (auto &f : flights) {
            std::string status = f.value("status", "");
            std::string origin = f.contains("origin") && !f["origin"].is_null()
                               ? f["origin"].value("code_icao", "?") : "null";
            std::string dest   = f.contains("destination") && !f["destination"].is_null()
                               ? f["destination"].value("code_icao", "?") : "null";
            int pct            = f.value("progress_percent", -1);
            std::cout << "  " << f.value("ident", "?")
                      << "  " << origin << " -> " << dest
                      << "  pct=" << pct << "%"
                      << "  status=\"" << status << "\"\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "JSON parse error: " << e.what() << "\n";
    }

    return 0;
}
