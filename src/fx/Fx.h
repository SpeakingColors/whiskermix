// Shared effects: shimmer delay, plate reverb, hall reverb (FDN), shimmer reverb. Pure C++, allocation only in prepare().
// Reverb designs follow published literature: Dattorro "Effect Design Part 1" (JAES 1997, plate topology of the
// Griesinger/Lexicon lineage) and Jot/Stautner-Puckette feedback delay networks. No third-party code is used.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace wm::fx {

constexpr float kTwoPi = 6.28318530718f;

class DelayLine {
public:
    void prepare(int maxSamples) { buf_.assign(size_t(maxSamples) + 4, 0.0f); w_ = 0; }
    void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    void push(float x) { buf_[w_] = x; w_ = (w_ + 1) % buf_.size(); }
    float read(float d) const {   // d samples back, linear interpolation (d >= 1)
        float p = float(w_) - d;
        while (p < 0) p += float(buf_.size());
        size_t i = size_t(p), j = (i + 1) % buf_.size();
        float f = p - float(i);
        return buf_[i] * (1 - f) + buf_[j] * f;
    }
    float readInt(int d) const { return buf_[(w_ + buf_.size() - size_t(d)) % buf_.size()]; }
    size_t size() const { return buf_.size(); }
private:
    std::vector<float> buf_; size_t w_ = 0;
};

struct OnePoleLP {   // y += a*(x-y)
    float y = 0;
    float tick(float x, float a) { y += a * (x - y); return y; }
};

// Schroeder allpass with fixed delay (g = coefficient)
class Allpass {
public:
    void prepare(int d) { d_ = d; line_.prepare(d + 2); }
    float tick(float x, float g) {
        float z = line_.readInt(d_);
        float v = x + g * z;
        line_.push(v);
        return z - g * v;
    }
    void clear() { line_.clear(); }
private:
    DelayLine line_; int d_ = 1;
};

// Pitch shifter: two overlapping grains read at ratio speed from a delay line, crossfaded with a sine window.
class PitchShifter {
public:
    void prepare(float sr, float grainMs = 60.0f) {
        grain_ = std::max(64.0f, sr * grainMs * 0.001f);
        line_.prepare(int(grain_ * 2) + 8);
        phase_ = 0;
    }
    void clear() { line_.clear(); phase_ = 0; }
    float tick(float x, float ratio) {
        line_.push(x);
        float out = 0;
        for (int k = 0; k < 2; ++k) {
            float ph = phase_ + 0.5f * float(k); if (ph >= 1) ph -= 1;
            float delay = 1.0f + ph * grain_ * 2.0f;           // grain sweeps through the line
            float win = std::sin(3.14159265f * ph);
            out += line_.read(std::min(delay, grain_ * 2.0f)) * win * win;
        }
        phase_ += (1.0f - ratio) / grain_;                      // ratio 2 => octave up
        while (phase_ >= 1) phase_ -= 1;
        while (phase_ < 0) phase_ += 1;
        return out;
    }
private:
    DelayLine line_; float grain_ = 2048, phase_ = 0;
};

// ---------------------------------------------------------------- Shimmer delay
struct ShimmerDelayParams {
    float timeMs = 380, feedback = 0.55f, shimmer = 0.6f /*amount of shifted signal in the loop*/,
          semitones = 12, tone = 0.45f /*0 dark..1 bright*/, spread = 0.5f /*stereo ping-pong*/, mix = 0.35f;
};

class ShimmerDelay {
public:
    void prepare(float sr) {
        sr_ = sr; dl_.prepare(int(sr * 2.5f)); dr_.prepare(int(sr * 2.5f));
        ps_[0].prepare(sr); ps_[1].prepare(sr); lp_[0] = lp_[1] = OnePoleLP{}; hp_[0] = hp_[1] = 0;
    }
    void clear() { dl_.clear(); dr_.clear(); ps_[0].clear(); ps_[1].clear(); }
    // Stereo in -> stereo out (wet only; the caller applies mix, or use p.mix via tickMix)
    void tick(float inL, float inR, float& outL, float& outR) {
        const auto& p = params;
        float d = std::clamp(p.timeMs, 20.f, 2000.f) * 0.001f * sr_;
        float a = 0.02f + 0.95f * p.tone * p.tone;                     // one-pole coefficient
        float ratio = std::pow(2.0f, p.semitones / 12.0f);
        float wl = dl_.read(d), wr = dr_.read(d * (1.0f + 0.12f * p.spread));
        float fl = shape(0, wl, a, ratio), fr = shape(1, wr, a, ratio);
        // cross feed for ping-pong
        float cl = fl * (1 - p.spread * 0.5f) + fr * (p.spread * 0.5f);
        float cr = fr * (1 - p.spread * 0.5f) + fl * (p.spread * 0.5f);
        float fb = std::clamp(p.feedback, 0.f, 0.97f);
        dl_.push(inL + cl * fb); dr_.push(inR + cr * fb);
        outL = wl; outR = wr;
    }
    ShimmerDelayParams params;
private:
    float shape(int c, float x, float a, float ratio) {
        float s = ps_[c].tick(x, ratio);
        float m = x * (1 - params.shimmer) + s * params.shimmer;
        float lp = lp_[c].tick(m, a);
        hp_[c] += 0.005f * (lp - hp_[c]);                                // gentle DC / rumble cut in the loop
        return std::tanh(lp - hp_[c]);                                  // soft limit keeps the loop stable
    }
    float sr_ = 48000; DelayLine dl_, dr_; PitchShifter ps_[2]; OnePoleLP lp_[2]; float hp_[2] = {0, 0};
};

// ---------------------------------------------------------------- Plate reverb (Dattorro topology)
struct ReverbParams {
    float decay = 0.7f;        // 0..0.98 tank feedback
    float damping = 0.4f;      // 0 bright .. 1 dark
    float preDelayMs = 10;
    float size = 1.0f;         // scales all delay lengths (0.5..1.5)
    float modDepth = 0.5f;     // tank modulation
    float inputBandwidth = 0.9f;
};

class PlateReverb {
public:
    void prepare(float sr) {
        sr_ = sr; float k = sr / 29761.0f;                              // delay lengths in Dattorro are for 29761 Hz
        auto L = [&](int n) { return std::max(2, int(std::lround(float(n) * k))); };
        const int ia[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) inAp_[i].prepare(L(ia[i]));
        pre_.prepare(int(sr * 0.25f) + 4);
        const int apL[2] = {672, 908}, dl1[2] = {4453, 4217}, apS[2] = {1800, 2656}, dl2[2] = {3720, 3163};
        for (int s = 0; s < 2; ++s) {
            mapDelay_[s] = L(apL[s]);
            modAp_[s].prepare(int(mapDelay_[s] * 1.5f) + 40);
            d1_[s].prepare(int(L(dl1[s]) * 1.6f) + 8); d2_[s].prepare(int(L(dl2[s]) * 1.6f) + 8);
            ap2_[s].prepare(L(apS[s]));
            len1_[s] = L(dl1[s]); len2_[s] = L(dl2[s]);
            lp_[s] = OnePoleLP{}; state_[s] = 0;
        }
        bw_ = OnePoleLP{}; lfoPh_[0] = 0; lfoPh_[1] = 0.5f;
    }
    void clear() { for (auto& a : inAp_) a.clear(); pre_.clear(); for (int s = 0; s < 2; ++s) { modAp_[s].clear(); d1_[s].clear(); d2_[s].clear(); ap2_[s].clear(); state_[s] = 0; lp_[s].y = 0; } }
    // mono in, stereo out (wet)
    void tick(float in, float& outL, float& outR) {
        const auto& p = params;
        pre_.push(in);
        float x = pre_.read(std::max(1.0f, p.preDelayMs * 0.001f * sr_));
        x = bw_.tick(x, std::clamp(p.inputBandwidth, 0.05f, 1.0f));
        x = inAp_[0].tick(x, 0.75f); x = inAp_[1].tick(x, 0.75f);
        x = inAp_[2].tick(x, 0.625f); x = inAp_[3].tick(x, 0.625f);
        float decay = std::clamp(p.decay, 0.f, 0.98f), damp = 1.0f - std::clamp(p.damping, 0.f, 0.98f);
        float sz = std::clamp(p.size, 0.4f, 1.5f);
        float tankIn[2] = {x + state_[1] * decay, x + state_[0] * decay};   // cross-coupled tanks
        float yl = 0, yr = 0;
        for (int s = 0; s < 2; ++s) {
            lfoPh_[s] += 0.8f / sr_; if (lfoPh_[s] >= 1) lfoPh_[s] -= 1;
            float mod = std::sin(kTwoPi * lfoPh_[s]) * 8.0f * p.modDepth * sr_ / 29761.0f;
            // modulated decay-diffusion allpass (coefficient 0.7)
            float md = std::max(2.0f, float(mapDelay_[s]) * sz + mod);
            float zread = readFrac(modAp_[s], md);
            float v = tankIn[s] + 0.7f * zread;
            modAp_[s].push(v);
            float a = zread - 0.7f * v;
            d1_[s].push(a);
            float t = d1_[s].read(std::max(2.0f, float(len1_[s]) * sz));
            float lp = lp_[s].tick(t, damp);
            float g = lp * decay;
            g = ap2_[s].tick(g, 0.5f);
            d2_[s].push(g);
            float o = d2_[s].read(std::max(2.0f, float(len2_[s]) * sz));
            state_[s] = o;
            // output taps from inside both tanks (positions scaled with sample rate)
            float k = sr_ / 29761.0f;
            float tapA = d1_[s].read(std::max(2.0f, 266.0f * k)) + d1_[s].read(std::max(2.0f, 2974.0f * k));
            float tapB = d2_[s].read(std::max(2.0f, 1913.0f * k)) - d2_[s].read(std::max(2.0f, 187.0f * k));
            float y = tapA + tapB;
            (s == 0 ? yl : yr) = y;
        }
        outL = yl * 0.6f; outR = yr * 0.6f;
    }
    ReverbParams params;
private:
    static float readFrac(Allpass&, float) = delete;
    float readFrac(DelayLine& d, float x) const { return d.read(x); }
    // The modulated allpass needs a fractional tap, so it uses DelayLine directly.
    struct ModAp : DelayLine { void prepare(int n) { DelayLine::prepare(n); } };
    float sr_ = 48000; Allpass inAp_[4]; DelayLine pre_; OnePoleLP bw_;
    ModAp modAp_[2]; DelayLine d1_[2], d2_[2]; Allpass ap2_[2]; OnePoleLP lp_[2];
    int mapDelay_[2] = {1, 1}, len1_[2] = {1, 1}, len2_[2] = {1, 1}; float state_[2] = {0, 0}, lfoPh_[2] = {0, 0.5f};
};

// ---------------------------------------------------------------- Hall reverb (8-line FDN, Householder mixing)
class HallReverb {
public:
    static constexpr int N = 8;
    void prepare(float sr) {
        sr_ = sr;
        const int base[N] = {1117, 1259, 1409, 1553, 1721, 1877, 2029, 2203};   // mutually non-harmonic, at 48 kHz
        for (int i = 0; i < N; ++i) {
            len_[i] = std::max(8, int(std::lround(float(base[i]) * sr / 48000.0f)));
            line_[i].prepare(int(len_[i] * 3.2f) + 8); lp_[i] = OnePoleLP{}; state_[i] = 0;
        }
        pre_.prepare(int(sr * 0.25f) + 4);
        for (int i = 0; i < 4; ++i) diff_[i].prepare(std::max(4, int(std::lround(float((int[]){113, 167, 223, 307}[i]) * sr / 48000.0f))));
    }
    void clear() { for (auto& l : line_) l.clear(); pre_.clear(); for (auto& d : diff_) d.clear(); for (int i = 0; i < N; ++i) { state_[i] = 0; lp_[i].y = 0; } }
    // mono in, stereo out. rt60 in seconds is derived from p.decay (0..1 maps 0.4..12 s)
    void tick(float in, float& outL, float& outR) {
        const auto& p = params;
        pre_.push(in);
        float x = pre_.read(std::max(1.0f, p.preDelayMs * 0.001f * sr_));
        for (auto& d : diff_) x = d.tick(x, 0.6f);
        float rt60 = 0.4f + std::clamp(p.decay, 0.f, 1.f) * std::clamp(p.decay, 0.f, 1.f) * 11.6f;
        float sz = std::clamp(p.size, 0.4f, 1.5f), damp = 1.0f - std::clamp(p.damping, 0.f, 0.95f);
        float rd[N], sum = 0;
        for (int i = 0; i < N; ++i) {
            rd[i] = line_[i].read(std::max(2.0f, float(len_[i]) * sz));
            float g = std::pow(10.0f, -3.0f * float(len_[i]) * sz / (rt60 * sr_));   // per-line gain for the chosen RT60
            rd[i] = lp_[i].tick(rd[i], damp) * g;
            sum += rd[i];
        }
        float mix = sum * (2.0f / N);                                   // Householder reflection: v - 2/N * sum
        for (int i = 0; i < N; ++i) line_[i].push(x * 0.35f + rd[i] - mix);
        float l = 0, r = 0;
        for (int i = 0; i < N; ++i) { (i & 1 ? r : l) += rd[i] * (i & 2 ? -1.f : 1.f); }
        outL = l * 0.5f; outR = r * 0.5f;
    }
    ReverbParams params;
private:
    float sr_ = 48000; int len_[N]; DelayLine line_[N]; OnePoleLP lp_[N]; float state_[N]; DelayLine pre_; Allpass diff_[4];
};

// ---------------------------------------------------------------- Shimmer reverb: octave shifted tail fed back into the tank
struct ShimmerReverbParams {
    ReverbParams reverb; float shimmer = 0.4f; float semitones = 12; float feedback = 0.35f; bool hall = false;
};

class ShimmerReverb {
public:
    void prepare(float sr) { plate_.prepare(sr); hall_.prepare(sr); ps_[0].prepare(sr, 80); ps_[1].prepare(sr, 80); }
    void clear() { plate_.clear(); hall_.clear(); ps_[0].clear(); ps_[1].clear(); fbl_ = fbr_ = 0; }
    void tick(float in, float& outL, float& outR) {
        plate_.params = hall_.params = params.reverb;
        float ratio = std::pow(2.0f, params.semitones / 12.0f);
        float x = in + std::tanh(fbl_ + fbr_) * params.shimmer * 0.5f;
        float l, r;
        params.hall ? hall_.tick(x, l, r) : plate_.tick(x, l, r);
        float sl = ps_[0].tick(l, ratio), sr = ps_[1].tick(r, ratio);
        fbl_ = sl * params.feedback; fbr_ = sr * params.feedback;
        outL = l + sl * params.shimmer * 0.5f; outR = r + sr * params.shimmer * 0.5f;
    }
    ShimmerReverbParams params;
private:
    PlateReverb plate_; HallReverb hall_; PitchShifter ps_[2]; float fbl_ = 0, fbr_ = 0;
};

}  // namespace wm::fx
