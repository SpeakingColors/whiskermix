// App preferences (saved as settings.json in the app data folder). Only options the app really honours are listed here.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace wm::mixer {

struct Settings {
    bool tooltips = true;            // info bubbles on hover
    bool doubleClickReset = true;    // double-click a fader or knob to reset it
    bool autosave = true;            // write the workspace to the app data folder every minute
    bool loadAutosaveOnStart = true; // open that autosave at start
    float peakHoldSec = 2.0f;        // 0 = off
    bool showGrOnStrips = true;      // compressor gain reduction readout in the strip
    int aggClock = 0;                // Routing page, audio device aggregation: master clock 0 = Digiface, 1 = GPU, 2 = Auto (stored; the aggregation engine comes later)
    int aggDrift = 0;                // 0 = adaptive resample, 1 = off
    std::map<std::string, float> opt;   // Settings page options that are stored only (backed later), saved as "x_<name>"
    float getOpt(const std::string& k, float d) const { auto it = opt.find(k); return it == opt.end() ? d : it->second; }
    void setOpt(const std::string& k, float v) { opt[k] = v; }

    std::string toJson() const {
        char b[256];
        std::snprintf(b, sizeof b, "{\"tooltips\":%d,\"dbl\":%d,\"auto\":%d,\"onstart\":%d,\"hold\":%.2f,\"gr\":%d,\"aggClock\":%d,\"aggDrift\":%d}", tooltips, doubleClickReset, autosave,
                      loadAutosaveOnStart, double(peakHoldSec), showGrOnStrips, aggClock, aggDrift);
        std::string out = b; out.pop_back();
        for (const auto& kv : opt) { char x[96]; std::snprintf(x, sizeof x, ",\"x_%s\":%.3f", kv.first.c_str(), double(kv.second)); out += x; }
        return out + "}";
    }
    bool fromJson(const std::string& s) {
        auto g = [&](const char* k, std::string& v) {
            std::string key = std::string("\"") + k + "\":"; size_t p = s.find(key); if (p == std::string::npos) return false;
            p += key.size(); size_t e = s.find_first_of(",}", p); v = s.substr(p, e - p); return true; };
        if (s.find('{') == std::string::npos) return false;
        std::string v;
        if (g("tooltips", v)) tooltips = v == "1";
        if (g("dbl", v)) doubleClickReset = v == "1";
        if (g("auto", v)) autosave = v == "1";
        if (g("onstart", v)) loadAutosaveOnStart = v == "1";
        if (g("hold", v)) peakHoldSec = std::strtof(v.c_str(), nullptr);
        if (g("gr", v)) showGrOnStrips = v == "1";
        if (g("aggClock", v)) aggClock = std::atoi(v.c_str());
        if (g("aggDrift", v)) aggDrift = std::atoi(v.c_str());
        for (size_t p = s.find("\"x_"); p != std::string::npos; p = s.find("\"x_", p + 1)) {
            size_t q = s.find('"', p + 3); if (q == std::string::npos) break;
            size_t e = s.find_first_of(",}", q); if (e == std::string::npos || s[q + 1] != ':') continue;
            opt[s.substr(p + 3, q - p - 3)] = std::strtof(s.substr(q + 2, e - q - 2).c_str(), nullptr);
        }
        if (peakHoldSec < 0) peakHoldSec = 0;
        if (peakHoldSec > 10) peakHoldSec = 10;
        return true;
    }
};

inline Settings& settings() { static Settings s; return s; }

}  // namespace wm::mixer
