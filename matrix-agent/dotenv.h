#pragma once

#include <fstream>
#include <string>
#include <unordered_map>
#include <algorithm>

// Parses a .env file and returns key->value pairs.
// Handles:  KEY=value  |  KEY = "value"  |  # comments  |  blank lines
inline std::unordered_map<std::string, std::string> load_dotenv(const std::string &path) {
    std::unordered_map<std::string, std::string> env;
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "dotenv: could not open '%s'\n", path.c_str());
        return env;
    }

    auto trim = [](std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
        return s;
    };

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        // Strip surrounding quotes
        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }

        env[key] = val;
    }
    return env;
}
