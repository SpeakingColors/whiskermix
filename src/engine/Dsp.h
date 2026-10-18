// Channel EQ (RBJ biquads) and compressor. Pure C++. Response math is shared with the UI graphs.
#pragma once
#include <cmath>
#include <algorithm>
#include <complex>

namespace wm::engine {

constexpr float kPi = 3.14159265358979f;

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad peak(float sr, float f, float gDb, float q) {
        float A = std::pow(10.0f, gDb / 40.0f), w = 2 * kPi * f / sr, al = std::sin(w) / (2 * q), c = std::cos(w);
        return norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    static Biquad lowShelf(float sr, float f, float gDb) {
        float A = std::pow(10.0f, gDb / 40.0f), w = 2 * kPi * f / sr, c = std::cos(w), s = std::sin(w);
        float al = s / 2 * std::sqrt(2.0f), t = 2 * std::sqrt(A) * al;
        return norm(A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
                    (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
    static Biquad highShelf(float sr, float f, float gDb) {
        float A = std::pow(10.0f, gDb / 40.0f), w = 2 * kPi * f / sr, c = std::cos(w), s = std::sin(w);
        float al = s / 2 * std::sqrt(2.0f), t = 2 * std::sqrt(A) * al;
        return norm(A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
                    (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }
    static Biquad highPass(float sr, float f, float q = 0.7071f) {
        float w = 2 * kPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        return norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    float tick(float x) {  // transposed direct form II
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    float magDb(float sr, float f) const {  // |H| at f, for the UI graph
        float w = 2 * kPi * f / sr;
        std::complex<float> z1_(std::cos(w), -std::sin(w)), z2_ = z1_ * z1_;
        auto num = b0 + b1 * z1_ + b2 * z2_, den = 1.0f + a1 * z1_ + a2 * z2_;
        return 20 * std::log10(std::abs(num / den) + 1e-12f);
    }
private:
    static Biquad norm(float B0, float B1, float B2, float A0, float A1, float A2) {
        Biquad q; q.b0 = B0 / A0; q.b1 = B1 / A0; q.b2 = B2 / A0; q.a1 = A1 / A0; q.a2 = A2 / A0; return q;
    }
};

struct FxParams {
    bool eqOn = false, compOn = false;
    float lowCutHz = 0;                       // 0 = off
    float lowGain = 0, lowFreq = 100;         // shelf
    float midGain = 0, midFreq = 1000, midQ = 1;
    float highGain = 0, highFreq = 8000;      // shelf
    float threshDb = -18, ratio = 3, attackMs = 10, releaseMs = 120, makeupDb = 0, kneeDb = 6;
};

inline bool operator==(const FxParams& a, const FxParams& b) {
    return a.eqOn == b.eqOn && a.compOn == b.compOn && a.lowCutHz == b.lowCutHz && a.lowGain == b.lowGain &&
           a.lowFreq == b.lowFreq && a.midGain == b.midGain && a.midFreq == b.midFreq && a.midQ == b.midQ &&
           a.highGain == b.highGain && a.highFreq == b.highFreq && a.threshDb == b.threshDb && a.ratio == b.ratio &&
           a.attackMs == b.attackMs && a.releaseMs == b.releaseMs && a.makeupDb == b.makeupDb && a.kneeDb == b.kneeDb;
}


// Static compressor curve: output level for an input level (UI graph and the DSP use the same function).
inline float compOutDb(const FxParams& p, float inDb) {
    float over = inDb - p.threshDb, k = p.kneeDb;
    if (2 * over < -k) return inDb;
    if (2 * over > k) return p.threshDb + over / p.ratio;
    float t = over + k / 2;
    return inDb + (1 / p.ratio - 1) * t * t / (2 * k);
}

class ChannelFx {
public:
    void process(const FxParams& p, float sr, const float* in, float* out, int n) {
        if (sr != sr_ || !(p == last_)) { rebuild(p, sr); }
        float atk = std::exp(-1.0f / (0.001f * std::max(p.attackMs, 0.1f) * sr)), rel = std::exp(-1.0f / (0.001f * std::max(p.releaseMs, 1.0f) * sr));
        float mk = std::pow(10.0f, p.makeupDb / 20.0f);
        for (int i = 0; i < n; ++i) {
            float x = in[i];
            if (p.eqOn) { if (p.lowCutHz > 0) x = hp_.tick(x); x = lo_.tick(x); x = mid_.tick(x); x = hi_.tick(x); }
            if (p.compOn) {
                float lvl = std::fabs(x) + 1e-9f;
                env_ = lvl > env_ ? atk * env_ + (1 - atk) * lvl : rel * env_ + (1 - rel) * lvl;
                float inDb = 20 * std::log10(env_);
                grDb_ = compOutDb(p, inDb) - inDb;
                x *= std::pow(10.0f, grDb_ / 20.0f) * mk;
            }
            out[i] = x;
        }
    }
    float gainReductionDb() const { return grDb_; }
private:
    void rebuild(const FxParams& p, float sr) {
        auto keep = [](Biquad& dst, Biquad src) { src.z1 = dst.z1; src.z2 = dst.z2; dst = src; };
        keep(hp_, p.lowCutHz > 0 ? Biquad::highPass(sr, p.lowCutHz) : Biquad{});
        keep(lo_, Biquad::lowShelf(sr, p.lowFreq, p.lowGain));
        keep(mid_, Biquad::peak(sr, p.midFreq, p.midGain, p.midQ));
        keep(hi_, Biquad::highShelf(sr, p.highFreq, p.highGain));
        sr_ = sr; last_ = p;
    }
    Biquad hp_, lo_, mid_, hi_;
    float env_ = 0, grDb_ = 0, sr_ = 0;
    FxParams last_;
};

}  // namespace wm::engine
