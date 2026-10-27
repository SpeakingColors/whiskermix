// Per-channel FX chain (everything besides the EQ and Dynamics in Dsp.h): Reverb, Echo, Chorus, Phaser, Flanger, M/S,
// MB Comp, Limiter, Purr Trio, Opto. Pure C++, no allocation after prepare().
// RULE (Off = bypass): a slot that is off is never run, keeps no state (reset when off) and its meters read idle.
// The parameter table below is the single source for the DSP, the saved state and the UI knobs (same ranges as the mockup).
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include "Dsp.h"

namespace wm::engine {

enum FxKind { kReverb, kEcho, kChorus, kPhaser, kFlanger, kMidSide, kMbComp, kLimiter, kPurr, kOpto, kFxKinds };

struct FxKnob { const char* name; const char* unit; float lo, hi, def; };
struct FxSpec { const char* name; FxKnob k[4]; };

inline const FxSpec& fxSpec(int kind) {
    static const FxSpec t[kFxKinds] = {
        {"Reverb",    {{"Size", "s", 0.3f, 6, 1.8f}, {"Pre-dly", "ms", 0, 120, 20}, {"Damp", "%", 0, 100, 50}, {"Mix", "%", 0, 100, 25}}},
        {"Echo",      {{"Time", "ms", 40, 1000, 375}, {"Feedback", "%", 0, 90, 35}, {"Hi cut", "kHz", 1, 16, 6}, {"Mix", "%", 0, 100, 20}}},
        {"Chorus",    {{"Rate", "Hz", 0.1f, 5, 0.6f}, {"Depth", "%", 0, 100, 40}, {"Width", "%", 0, 100, 100}, {"Mix", "%", 0, 100, 30}}},
        {"Phaser",    {{"Rate", "Hz", 0.05f, 4, 0.4f}, {"Depth", "%", 0, 100, 60}, {"Stages", "", 2, 12, 6}, {"Feedback", "%", 0, 90, 30}}},
        {"Flanger",   {{"Rate", "Hz", 0.05f, 3, 0.25f}, {"Depth", "ms", 0.5f, 10, 3}, {"Feedback", "%", -90, 90, 40}, {"Mix", "%", 0, 100, 50}}},
        {"M/S",       {{"Mid", "dB", -12, 12, 0}, {"Side", "dB", -12, 12, 0}, {"Width", "%", 0, 200, 100}, {"Bass mono", "Hz", 0, 300, 120}}},
        {"MB Comp",   {{"Low", "dB", -24, 0, -12}, {"Mid", "dB", -24, 0, -12}, {"High", "dB", -24, 0, -12}, {"Mix", "%", 0, 100, 50}}},   // thresholds per band
        {"Limiter",   {{"Ceiling", "dB", -6, 0, -0.3f}, {"Release", "ms", 5, 500, 60}, {"Lookahead", "ms", 0, 10, 2}, {"Gain", "dB", 0, 12, 0}}},
        {"Purr Trio", {{"Drive", "%", 0, 100, 0}, {"Transistor", "%", 0, 100, 0}, {"Fuzz", "%", 0, 100, 0}, {"Fizz tamer", "kHz", 4, 14, 14}}},
        {"Opto",      {{"Peak reduction", "%", 0, 100, 45}, {"Gain", "dB", 0, 20, 6}, {"Comp/Limit", "", 0, 1, 0}, {"Mix", "%", 0, 100, 100}}},
    };
    return t[kind];
}

struct FxSlot {
    bool on = false;
    float p[4];
    FxSlot() { for (int i = 0; i < 4; ++i) p[i] = 0; }
};
inline FxSlot fxDefault(int kind) { FxSlot s; for (int i = 0; i < 4; ++i) s.p[i] = fxSpec(kind).k[i].def; return s; }
struct FxChainParams {
    FxSlot s[kFxKinds];
    FxChainParams() { for (int k = 0; k < kFxKinds; ++k) s[k] = fxDefault(k); }
};

namespace chain_detail {
inline float lin(float db) { return std::pow(10.0f, db / 20.0f); }
struct OnePole { float z = 0; float a = 0; void lp(float sr, float fc) { a = 1.0f - std::exp(-2.0f * kPi * std::min(fc, 0.45f * sr) / sr); } float tick(float x) { z += a * (x - z); return z; } };
struct Delay {
    std::vector<float> b; int w = 0;
    void prepare(int n) { b.assign(size_t(n) + 4, 0.0f); w = 0; }
    void clear() { std::fill(b.begin(), b.end(), 0.0f); w = 0; }
    void push(float x) { b[size_t(w)] = x; if (++w >= int(b.size())) w = 0; }
    float at(float d) const {   // d samples back, linear interpolation
        int n = int(b.size()); float pos = float(w) - 1.0f - d; while (pos < 0) pos += float(n);
        if (d < 0) d = 0;
        if (d > float(n) - 2.0f) d = float(n) - 2.0f;
        pos = float(w) - 1.0f - d; if (pos < 0) pos += float(n);
        int i = int(pos); if (i >= n) i -= n; float f = pos - float(i); int j = i + 1 >= n ? 0 : i + 1;
        return b[size_t(i)] * (1 - f) + b[size_t(j)] * f;
    }
};
// PurrOne colour stages (from purrone/include/purrone/Engine.h)
inline float sstep(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3 - 2 * t); }
inline float asymTanh(float u, float nl) { return u >= 0 ? std::tanh(u) : nl * std::tanh(u / nl); }
inline float purrShaper(float x, float drive, float xs, float fuzz) {
    const float dn = sstep((drive - 0.8f) * 5), xn = sstep((xs - 0.8f) * 5), fn = sstep((fuzz - 0.8f) * 5);
    float y = x;
    if (drive > 0.001f) { y = std::tanh(x * (1 + drive * 5 + dn * 30)); if (dn > 0) y += dn * (std::tanh(y * (2 + 10 * dn)) - y); }
    if (xs > 0.001f) {
        const float nl = 1 - 0.3f * xs - 0.25f * xn, u = y * (1 + xs * 5 + xn * 30);
        y = asymTanh(u, nl); y += (0.3f * xs + 0.25f * xn) * y * y;
        if (xn > 0) { const float y2 = asymTanh(y * (2 + 10 * xn) + 0.15f * xn, nl); y += xn * (y2 - y); }
    }
    if (fuzz > 0.001f) {
        const float fz = 1 + fuzz * 14 + fn * 70, bf = 0.5f * fuzz;
        y = std::tanh(y * fz + bf) - std::tanh(bf);
        if (fn > 0) { const float r = std::sqrt(y * y + 0.02f) - 0.1414f; y += fn * 0.7f * (r - 0.3f); y = std::tanh(y * (1 + 0.8f * fn)); }
    }
    return y;
}
inline float purrMakeup(float drive, float xs, float fuzz, float ref) {
    double a = 0, b = 0;
    for (int i = 0; i < 64; ++i) { float x = ref * std::sin(6.2831853f * float(i) / 64.0f); float y0 = purrShaper(x, 0, 0, 0), y1 = purrShaper(x, drive, xs, fuzz); a += double(y0) * y0; b += double(y1) * y1; }
    float m = b > 1e-9 ? float(std::sqrt(a / b)) : 1.0f; return m < 0.25f ? 0.25f : (m > 4 ? 4 : m);
}
struct Env { float e = 0; float tick(float x, float aA, float aR) { float v = std::fabs(x); float a = v > e ? aA : aR; e = a * e + (1 - a) * v; return e; } };
}  // namespace chain_detail

// One channel's chain. process() runs the mono slots in a fixed order; M/S is applied to a linked pair by processMidSide().
class ChannelChain {
public:
    void prepare(float sr) {
        sr_ = sr; using namespace chain_detail;
        echo_.prepare(int(sr * 1.05f)); chor_.prepare(int(sr * 0.05f)); flg_.prepare(int(sr * 0.02f)); pre_.prepare(int(sr * 0.13f));
        const int cl[4] = {1116, 1188, 1277, 1356}, al[2] = {556, 441};
        for (int i = 0; i < 4; ++i) comb_[i].prepare(int(cl[i] * sr / 44100.0f) + 2);
        for (int i = 0; i < 2; ++i) ap_[i].prepare(int(al[i] * sr / 44100.0f) + 2);
        reset();
    }
    void reset() {
        using namespace chain_detail;
        echo_.clear(); chor_.clear(); flg_.clear(); pre_.clear(); for (auto& d : comb_) d.clear(); for (auto& d : ap_) d.clear();
        for (auto& v : cdamp_) v = 0;
        echoLp_.z = 0; revLp_.z = 0; ph_[0] = ph_[1] = ph_[2] = 0; for (auto& v : apz_) v = 0; apFb_ = 0; flgFb_ = 0;
        mbLo_.z = mbHi_.z = 0; for (auto& e : mbEnv_) e.e = 0; limG_ = 1; optoG_ = 1; optoEnv_ = 0; dc_ = 0; sideLp_ = 0; fizz_.z = 0;
    }

    // Runs every slot that is on, in place. Off slots are not run and are cleared. in/out peaks (linear) per slot are written
    // to inPk/outPk (0 = idle) when given.
    void process(const FxChainParams& P, float* x, int n, float* inPk = nullptr, float* outPk = nullptr) {
        static const int order[] = {kChorus, kPhaser, kFlanger, kEcho, kReverb, kMbComp, kPurr, kOpto, kLimiter};
        for (int k = 0; k < kFxKinds; ++k) { if (inPk) inPk[k] = 0; if (outPk) outPk[k] = 0; }
        for (int k : order) {
            const FxSlot& s = P.s[k];
            if (!s.on) { if (wasOn_[k]) { resetOne(k); wasOn_[k] = false; } continue; }
            wasOn_[k] = true;
            float pi = 0; if (inPk) for (int i = 0; i < n; ++i) pi = std::max(pi, std::fabs(x[i]));
            run(k, s.p, x, n);
            if (inPk) { inPk[k] = pi; float po = 0; for (int i = 0; i < n; ++i) po = std::max(po, std::fabs(x[i])); outPk[k] = po; }
        }
    }
    // M/S on a stereo pair (both blocks in place). Off = nothing touched.
    void processMidSide(const FxSlot& s, float* l, float* r, int n) {
        if (!s.on) { sideLp_ = 0; wasOn_[kMidSide] = false; return; }
        wasOn_[kMidSide] = true;
        const float gm = chain_detail::lin(s.p[0]), gs = chain_detail::lin(s.p[1]) * s.p[2] / 100.0f;
        const float a = s.p[3] > 0 ? 1.0f - std::exp(-2.0f * kPi * s.p[3] / sr_) : 0.0f;
        for (int i = 0; i < n; ++i) {
            float m = 0.5f * (l[i] + r[i]) * gm, sd = 0.5f * (l[i] - r[i]) * gs;
            if (a > 0) { sideLp_ += a * (sd - sideLp_); sd -= sideLp_; }   // bass mono: no side below the corner
            l[i] = m + sd; r[i] = m - sd;
        }
    }
    float gainReductionDb() const { return grDb_; }
private:
    void resetOne(int k) {
        using namespace chain_detail;
        switch (k) {
            case kEcho: echo_.clear(); echoLp_.z = 0; break;
            case kReverb: pre_.clear(); for (auto& d : comb_) d.clear(); for (auto& d : ap_) d.clear(); for (auto& v : cdamp_) v = 0; revLp_.z = 0; break;
            case kChorus: chor_.clear(); break;
            case kFlanger: flg_.clear(); flgFb_ = 0; break;
            case kPhaser: for (auto& v : apz_) v = 0; apFb_ = 0; break;
            case kMbComp: mbLo_.z = mbHi_.z = 0; for (auto& e : mbEnv_) e.e = 0; break;
            case kLimiter: limG_ = 1; break;
            case kPurr: dc_ = 0; fizz_.z = 0; break;
            case kOpto: optoG_ = 1; optoEnv_ = 0; break;
        }
    }
    void run(int k, const float* p, float* x, int n) {
        using namespace chain_detail;
        switch (k) {
        case kEcho: {
            float d = p[0] * 0.001f * sr_, fb = p[1] / 100.0f, mix = p[3] / 100.0f; echoLp_.lp(sr_, p[2] * 1000.0f);
            for (int i = 0; i < n; ++i) { float w = echo_.at(d); echo_.push(x[i] + echoLp_.tick(w) * fb); x[i] = x[i] * (1 - mix) + w * mix; }
        } break;
        case kReverb: {   // Schroeder: pre-delay, 4 damped combs, 2 allpass
            float dec = p[0], damp = 0.15f + 0.8f * p[2] / 100.0f, mix = p[3] / 100.0f, pd = p[1] * 0.001f * sr_;
            const int cl[4] = {1116, 1188, 1277, 1356};
            for (int i = 0; i < n; ++i) {
                pre_.push(x[i]); float in = pre_.at(pd) * 0.012f, acc = 0;
                for (int c = 0; c < 4; ++c) {
                    float len = cl[c] * sr_ / 44100.0f, g = std::pow(10.0f, -3.0f * len / (dec * sr_));   // RT60 = Size seconds
                    float y = comb_[c].at(len - 1.0f); cdamp_[c] += (1 - damp) * (y - cdamp_[c]);
                    comb_[c].push(in + cdamp_[c] * g); acc += y;
                }
                for (int a = 0; a < 2; ++a) { float len = (a ? 441 : 556) * sr_ / 44100.0f, z = ap_[a].at(len - 1.0f); float v = acc + 0.5f * z; ap_[a].push(v); acc = z - 0.5f * v; }
                x[i] = x[i] * (1 - mix) + acc * mix;
            }
        } break;
        case kChorus: {
            float rate = p[0], depth = p[1] / 100.0f * 0.006f * sr_, base = 0.012f * sr_, mix = p[3] / 100.0f * 0.5f;
            for (int i = 0; i < n; ++i) {
                ph_[0] += rate / sr_; if (ph_[0] >= 1) ph_[0] -= 1;
                chor_.push(x[i]); float w = chor_.at(base + depth * std::sin(2 * kPi * ph_[0]));
                x[i] = x[i] * (1 - mix) + w * mix;
            }
        } break;
        case kFlanger: {
            float rate = p[0], dep = p[1] * 0.001f * sr_, fb = p[2] / 100.0f, mix = p[3] / 100.0f, base = dep + 2.0f;
            for (int i = 0; i < n; ++i) {
                ph_[1] += rate / sr_; if (ph_[1] >= 1) ph_[1] -= 1;
                float d = base + dep * 0.5f * (1 + std::sin(2 * kPi * ph_[1]));
                flg_.push(x[i] + flgFb_ * fb); float w = flg_.at(d); flgFb_ = w;
                x[i] = x[i] * (1 - mix * 0.5f) + w * mix * 0.5f;
            }
        } break;
        case kPhaser: {
            int st = std::max(2, std::min(12, int(p[2] + 0.5f))) / 2 * 2; float rate = p[0], dep = p[1] / 100.0f, fb = p[3] / 100.0f;
            for (int i = 0; i < n; ++i) {
                ph_[2] += rate / sr_; if (ph_[2] >= 1) ph_[2] -= 1;
                float fc = 300.0f * std::pow(20.0f, 0.5f * (1 + std::sin(2 * kPi * ph_[2])) * dep);   // 300 Hz .. 6 kHz sweep
                float tn = std::tan(kPi * std::min(fc, 0.45f * sr_) / sr_), a = (tn - 1) / (tn + 1);
                float y = x[i] + apFb_ * fb;
                for (int s = 0; s < st; ++s) { float o = a * y + apz_[s]; apz_[s] = y - a * o; y = o; }
                apFb_ = y; x[i] = 0.5f * (x[i] + y);
            }
        } break;
        case kMbComp: {   // 3 bands split with complementary one-pole filters (they sum back to the input), per-band thresholds, ratio 3
            mbLo_.lp(sr_, 200.0f); mbHi_.lp(sr_, 2500.0f);
            float aA = std::exp(-1.0f / (0.005f * sr_)), aR = std::exp(-1.0f / (0.1f * sr_)), mix = p[3] / 100.0f;
            for (int i = 0; i < n; ++i) {
                float in = x[i], lo = mbLo_.tick(in), hl = mbHi_.tick(in), mid = hl - lo, hi = in - hl, b[3] = {lo, mid, hi}, o = 0, mgr = 0;
                for (int j = 0; j < 3; ++j) {
                    float e = mbEnv_[j].tick(b[j], aA, aR), db = 20 * std::log10(e + 1e-9f), over = db - p[j];
                    float gr = over > 0 ? -over * (1 - 1.0f / 3.0f) : 0; o += b[j] * lin(gr); mgr = std::min(mgr, gr);
                }
                grDb_ = mgr; x[i] = in * (1 - mix) + o * mix;
            }
        } break;
        case kLimiter: {
            float ceil = lin(p[0]), pre = lin(p[3]), rel = std::exp(-1.0f / (0.001f * std::max(p[1], 1.0f) * sr_));
            for (int i = 0; i < n; ++i) {
                float v = x[i] * pre, a = std::fabs(v), need = a > ceil ? ceil / a : 1.0f;
                limG_ = need < limG_ ? need : rel * limG_ + (1 - rel) * need; if (limG_ > need) limG_ = need;   // instant attack: never above the ceiling
                x[i] = v * limG_; grDb_ = 20 * std::log10(limG_ + 1e-9f);
            }
        } break;
        case kPurr: {
            float dr = p[0] / 100.0f, xs = p[1] / 100.0f, fz = p[2] / 100.0f; if (dr + xs + fz < 0.001f) break;
            float mk = purrMakeup(dr, xs, fz, 0.5f); fizz_.lp(sr_, p[3] * 1000.0f);
            float nast = std::max(sstep((dr - 0.8f) * 5), std::max(sstep((xs - 0.8f) * 5), sstep((fz - 0.8f) * 5)));
            for (int i = 0; i < n; ++i) {
                float in = x[i], y = 0;
                for (int h = 0; h < 2; ++h) {   // 2x oversampling (linear up, average down)
                    float u = h ? 0.5f * (prevIn_ + in) : in; y += purrShaper(std::max(-8.0f, std::min(8.0f, u * 1.5f)), dr, xs, fz) * mk / 1.5f;
                }
                prevIn_ = in; y *= 0.5f;
                if (nast > 0) y = fizz_.tick(y);
                dc_ += 0.0005f * (y - dc_); x[i] = y - dc_;
            }
        } break;
        case kOpto: {   // LA-2A style: slow program-dependent release, soft knee; Comp/Limit picks the ratio
            float pr = p[0] / 100.0f, mk = lin(p[1]), mix = p[3] / 100.0f, lim = p[2] >= 0.5f ? 1.0f : 0.0f;
            float th = -6.0f - pr * 30.0f, ratio = lim > 0 ? 10.0f : 3.0f;
            float aA = std::exp(-1.0f / (0.010f * sr_));
            for (int i = 0; i < n; ++i) {
                float in = x[i], v = std::fabs(in);
                optoEnv_ = v > optoEnv_ ? aA * optoEnv_ + (1 - aA) * v : std::exp(-1.0f / ((0.06f + 0.9f * std::min(1.0f, optoEnv_ * 4)) * sr_)) * optoEnv_ + 0 * v;   // release slows with level
                float db = 20 * std::log10(optoEnv_ + 1e-9f), over = db - th, gr = over > 0 ? -over * (1 - 1 / ratio) : 0;
                float tg = lin(gr), kc = tg < optoG_ ? 0.01f : 0.0004f; optoG_ += kc * (tg - optoG_);
                grDb_ = 20 * std::log10(optoG_ + 1e-9f);
                x[i] = in * (1 - mix) + in * optoG_ * mk * mix;
            }
        } break;
        default: break;
        }
    }
    float sr_ = 48000.0f, grDb_ = 0;
    chain_detail::Delay echo_, chor_, flg_, pre_, comb_[4], ap_[2];
    chain_detail::OnePole echoLp_, revLp_, mbLo_, mbHi_, fizz_;
    chain_detail::Env mbEnv_[3];
    float cdamp_[4] = {0, 0, 0, 0}, ph_[3] = {0, 0, 0}, apz_[12] = {}, apFb_ = 0, flgFb_ = 0, limG_ = 1, optoG_ = 1, optoEnv_ = 0, dc_ = 0, sideLp_ = 0, prevIn_ = 0;
    bool wasOn_[kFxKinds] = {};
};

}  // namespace wm::engine
