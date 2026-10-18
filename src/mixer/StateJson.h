// Save and load a mixer state as JSON (workspace files). Small hand-written writer/reader, no dependencies.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include "MixerModel.h"

namespace wm::mixer {

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o.push_back('\\'); o.push_back(c); }
    return o;
}

inline std::string toJson(const State& st) {
    std::string o = "{\"channels\":[";
    for (size_t i = 0; i < st.channels.size(); ++i) {
        const auto& c = st.channels[i];
        char b[256];
        std::snprintf(b, sizeof b, "%s{\"name\":\"%s\",\"gain\":%.3f,\"pan\":%.3f,\"mute\":%d,\"solo\":%d,\"phaseL\":%d,\"phaseR\":%d,\"link\":%d}",
                      i ? "," : "", jsonEscape(c.name).c_str(), c.gainDb, c.pan, c.mute, c.solo, c.phaseL, c.phaseR, c.stereoLinked);
        o += b;
    }
    o += "],\"buses\":[";
    for (size_t i = 0; i < st.buses.size(); ++i) {
        char b[192];
        std::snprintf(b, sizeof b, "%s{\"name\":\"%s\",\"volume\":%.3f,\"dim\":%d}", i ? "," : "",
                      jsonEscape(st.buses[i].name).c_str(), st.buses[i].volumeDb, st.buses[i].dim);
        o += b;
    }
    return o + "]}";
}

namespace detail {
inline bool field(const std::string& s, size_t from, size_t to, const char* key, std::string& out) {
    const std::string k = std::string("\"") + key + "\":";
    size_t p = s.find(k, from);
    if (p == std::string::npos || p >= to) return false;
    p += k.size();
    if (s[p] == '"') {
        ++p; out.clear();
        while (p < s.size() && s[p] != '"') { if (s[p] == '\\') ++p; out.push_back(s[p++]); }
    } else {
        size_t e = s.find_first_of(",}", p);
        out = s.substr(p, e - p);
    }
    return true;
}
}  // namespace detail

inline bool fromJson(const std::string& s, State& st) {
    State n;
    size_t cs = s.find("\"channels\":["), bs = s.find("\"buses\":[");
    if (cs == std::string::npos || bs == std::string::npos) return false;
    for (size_t p = s.find('{', cs + 12); p != std::string::npos && p < bs; p = s.find('{', p + 1)) {
        size_t e = s.find('}', p);
        Channel c; std::string v;
        if (detail::field(s, p, e, "name", v)) c.name = v;
        if (detail::field(s, p, e, "gain", v)) c.gainDb = std::strtof(v.c_str(), nullptr);
        if (detail::field(s, p, e, "pan", v)) c.pan = std::strtof(v.c_str(), nullptr);
        if (detail::field(s, p, e, "mute", v)) c.mute = v == "1";
        if (detail::field(s, p, e, "solo", v)) c.solo = v == "1";
        if (detail::field(s, p, e, "phaseL", v)) c.phaseL = v == "1";
        if (detail::field(s, p, e, "phaseR", v)) c.phaseR = v == "1";
        if (detail::field(s, p, e, "link", v)) c.stereoLinked = v == "1";
        n.channels.push_back(c); p = e;
    }
    for (size_t p = s.find('{', bs + 9); p != std::string::npos; p = s.find('{', p + 1)) {
        size_t e = s.find('}', p);
        Bus b; std::string v;
        if (detail::field(s, p, e, "name", v)) b.name = v;
        if (detail::field(s, p, e, "volume", v)) b.volumeDb = std::strtof(v.c_str(), nullptr);
        if (detail::field(s, p, e, "dim", v)) b.dim = v == "1";
        n.buses.push_back(b); p = e;
    }
    st = n;
    return true;
}

}  // namespace wm::mixer
