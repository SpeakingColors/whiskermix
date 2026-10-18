// App preferences (saved as settings.json in the app data folder). Only options the app really honours are listed here.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

namespace wm::mixer {

struct Settings {
    bool tooltips = true;            // info bubbles on hover
    bool doubleClickReset = true;    // double-click a fader or knob to reset it
    bool autosave = true;            // write the workspace to the app data folder every minute
    bool loadAutosaveOnStart = true; // open that autosave at start
    float peakHoldSec = 2.0f;        // 0 = off
    bool showGrOnStrips = true;      // compressor gain reduction readout in the strip

    std::string toJson() const {
        char b[256];
        std::snprintf(b, sizeof b, "{\"tooltips\":%d,\"dbl\":%d,\"auto\":%d,\"onstart\":%d,\"hold\":%.2f,\"gr\":%d}", tooltips, doubleClickReset, autosave,
                      loadAutosaveOnStart, double(peakHoldSec), showGrOnStrips);
        return b;
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
        if (peakHoldSec < 0) peakHoldSec = 0;
        if (peakHoldSec > 10) peakHoldSec = 10;
        return true;
    }
};

inline Settings& settings() { static Settings s; return s; }

}  // namespace wm::mixer
