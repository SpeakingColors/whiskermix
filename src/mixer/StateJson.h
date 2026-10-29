// Save and load a mixer state as JSON (workspace files). Small hand-written writer/reader, no dependencies.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include "MixerModel.h"
#include "RoutingMatrix.h"

namespace wm::mixer {

// EQ and compressor settings are stored as flat "fx*" keys inside each channel object.
#define WM_FX_FIELDS(X) X(lowCutHz) X(lowGain) X(lowFreq) X(midGain) X(midFreq) X(midQ) X(highGain) X(highFreq) \
    X(threshDb) X(ratio) X(attackMs) X(releaseMs) X(makeupDb) X(kneeDb)

#define WM_BUS_FIELDS(X) X(reverb.p.decay) X(reverb.p.dampHz) X(reverb.p.preDelayMs) X(reverb.p.size) X(reverb.p.modDepth) X(reverb.p.color) \
    X(reverb.shimmerSemis) X(reverb.shimmerAmount) X(reverb.shimmer2Semis) X(reverb.shimmer2Amount) X(reverb.wetHpHz) X(reverb.wetLpHz) X(reverb.mix) X(delay.timeMs) X(delay.feedback) X(delay.shiftSemis) \
    X(delay.dampHz) X(delay.mix) X(delay.shimmerAmount)

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
        std::string fx;
        {
            char t[64];
            std::snprintf(t, sizeof t, ",\"fxEq\":%d,\"fxComp\":%d", c.fx.eqOn, c.fx.compOn); fx += t;
#define X(k) std::snprintf(t, sizeof t, ",\"fx_" #k "\":%.4g", double(c.fx.k)); fx += t;
            WM_FX_FIELDS(X)
#undef X
            for (int k = 0; k < wm::engine::kFxKinds; ++k) {   // chain FX: "cx<kind>on" and "cx<kind>p0..3"
                std::snprintf(t, sizeof t, ",\"cx%don\":%d", k, c.chain.s[k].on); fx += t;
                for (int q = 0; q < 4; ++q) { std::snprintf(t, sizeof t, ",\"cx%dp%d\":%.4g", k, q, double(c.chain.s[k].p[q])); fx += t; }
            }
        }
        std::snprintf(b, sizeof b, "%s{\"name\":\"%s\",\"gain\":%.3f,\"pan\":%.3f,\"mute\":%d,\"solo\":%d,\"phaseL\":%d,\"phaseR\":%d,\"link\":%d}",
                      i ? "," : "", jsonEscape(c.name).c_str(), c.gainDb, c.pan, c.mute, c.solo, c.phaseL, c.phaseR, c.stereoLinked);
        std::string one = b;
        one.pop_back();            // drop the closing brace, add fx fields, close again
        char ex[192];
        std::snprintf(ex, sizeof ex, ",\"colour\":%d,\"width\":%.3f,\"ms\":%d,\"fxSend\":%.2f,\"cue\":%d,\"trim\":%d,\"hide\":%d,\"hw\":%d,\"pb\":%d,\"emoji\":\"", c.colour, double(c.width), c.midSide, double(c.fxSendDb), c.cue, c.trim, c.hidden, c.hwFirst, c.playback);
        o += one + fx + ex + jsonEscape(c.emoji) + "\"}";
    }
    o += "],\"buses\":[";
    for (size_t i = 0; i < st.buses.size(); ++i) {
        char b[224];
        const auto& bu = st.buses[i];
        std::snprintf(b, sizeof b, "%s{\"name\":\"%s\",\"volume\":%.3f,\"dim\":%d,\"rvOn\":%d,\"dlOn\":%d,\"rvType\":%d,\"hw\":%d,\"st\":%d", i ? "," : "",
                      jsonEscape(bu.name).c_str(), bu.volumeDb, bu.dim, bu.reverbOn, bu.delayOn, int(bu.reverb.type), bu.hwFirst, bu.stereo);
        o += b;
        char t[64];
#define X(k) std::snprintf(t, sizeof t, ",\"bx_" #k "\":%.4g", double(bu.k)); o += t;
        WM_BUS_FIELDS(X)
#undef X
        o += "}";
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
        if (detail::field(s, p, e, "colour", v)) c.colour = std::atoi(v.c_str());
        if (detail::field(s, p, e, "width", v)) c.width = std::strtof(v.c_str(), nullptr);
        if (detail::field(s, p, e, "ms", v)) c.midSide = v == "1";
        if (detail::field(s, p, e, "fxSend", v)) c.fxSendDb = std::strtof(v.c_str(), nullptr);
        if (detail::field(s, p, e, "cue", v)) c.cue = v == "1";
        if (detail::field(s, p, e, "trim", v)) c.trim = v == "1";
        if (detail::field(s, p, e, "hide", v)) c.hidden = v == "1";
        if (detail::field(s, p, e, "hw", v)) c.hwFirst = std::atoi(v.c_str());
        if (detail::field(s, p, e, "pb", v)) c.playback = v == "1";
        if (detail::field(s, p, e, "emoji", v)) c.emoji = v;
        if (detail::field(s, p, e, "fxEq", v)) c.fx.eqOn = v == "1";
        if (detail::field(s, p, e, "fxComp", v)) c.fx.compOn = v == "1";
#define X(k) if (detail::field(s, p, e, "fx_" #k, v)) c.fx.k = std::strtof(v.c_str(), nullptr);
        WM_FX_FIELDS(X)
#undef X
        for (int k = 0; k < wm::engine::kFxKinds; ++k) {
            char key[24];
            std::snprintf(key, sizeof key, "cx%don", k); if (detail::field(s, p, e, key, v)) c.chain.s[k].on = v == "1";
            for (int q = 0; q < 4; ++q) { std::snprintf(key, sizeof key, "cx%dp%d", k, q); if (detail::field(s, p, e, key, v)) c.chain.s[k].p[q] = std::strtof(v.c_str(), nullptr); }
        }
        n.channels.push_back(c); p = e;
    }
    for (size_t p = s.find('{', bs + 9); p != std::string::npos; p = s.find('{', p + 1)) {
        size_t e = s.find('}', p);
        Bus b; std::string v;
        if (detail::field(s, p, e, "name", v)) b.name = v;
        if (detail::field(s, p, e, "volume", v)) b.volumeDb = std::strtof(v.c_str(), nullptr);
        if (detail::field(s, p, e, "dim", v)) b.dim = v == "1";
        if (detail::field(s, p, e, "rvOn", v)) b.reverbOn = v == "1";
        if (detail::field(s, p, e, "dlOn", v)) b.delayOn = v == "1";
        if (detail::field(s, p, e, "rvType", v)) b.reverb.type = wm::engine::ReverbType(std::atoi(v.c_str()));
        if (detail::field(s, p, e, "hw", v)) b.hwFirst = std::atoi(v.c_str());
        if (detail::field(s, p, e, "st", v)) b.stereo = v == "1";
#define X(k) if (detail::field(s, p, e, "bx_" #k, v)) b.k = std::strtof(v.c_str(), nullptr);
        WM_BUS_FIELDS(X)
#undef X
        n.buses.push_back(b); p = e;
    }
    st = n;
    return true;
}

// Workspace file = state + routing matrix. "routing" comes first and holds no nested braces.
inline std::string toJson(const State& st, const RoutingMatrix& rt) {
    std::string g;
    for (size_t i = 0; i < rt.data().size(); ++i) { char t[32]; std::snprintf(t, sizeof t, "%s%.2f", i ? "," : "", double(rt.data()[i])); g += t; }
    std::string body = toJson(st);   // starts with {"channels"
    return "{\"routing\":{\"in\":" + std::to_string(rt.inputs()) + ",\"out\":" + std::to_string(rt.outputs()) + ",\"g\":[" + g + "]}," + body.substr(1);
}
inline bool fromJson(const std::string& s, State& st, RoutingMatrix& rt) {
    if (!fromJson(s, st)) return false;
    size_t r = s.find("\"routing\":{");
    if (r == std::string::npos) return true;   // older file: keep current routing
    size_t re = s.find('}', r);
    std::string v; int in = 0, out = 0;
    if (!detail::field(s, r, re, "in", v)) return false;
    in = std::atoi(v.c_str());
    if (!detail::field(s, r, re, "out", v)) return false;
    out = std::atoi(v.c_str());
    size_t gs = s.find("\"g\":[", r), ge = s.find(']', gs);
    if (gs == std::string::npos || ge == std::string::npos) return false;
    RoutingMatrix n(in, out);
    size_t k = 0;
    for (size_t p = gs + 5; p < ge && k < size_t(in) * size_t(out);) {
        char* end; float f = std::strtof(s.c_str() + p, &end);
        n.set(int(k / size_t(out)), int(k % size_t(out)), f); ++k;
        p = size_t(end - s.c_str()) + 1;
    }
    rt = n;
    return true;
}

}  // namespace wm::mixer
