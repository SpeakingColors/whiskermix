#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "../src/engine/Reverb.h"
using namespace wm::engine;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static float goertzel(const std::vector<float>& x, float f, float sr) {
    float w = 2 * 3.14159265f * f / sr, c = 2 * std::cos(w), s1 = 0, s2 = 0;
    for (float v : x) { float s = v + c * s1 - s2; s2 = s1; s1 = s; }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / float(x.size());
}
// Strongest frequency (Hz) between lo and hi, scanning in 5 Hz steps.
static float peakFreq(const std::vector<float>& x, float lo, float hi, float sr) {
    float best = 0, bf = 0;
    for (float f = lo; f <= hi; f += 5) { float a = goertzel(x, f, sr); if (a > best) { best = a; bf = f; } }
    return bf;
}
static float energy(const std::vector<float>& v, size_t a, size_t b) { float e = 0; for (size_t i = a; i < b && i < v.size(); ++i) e += v[i] * v[i]; return e; }

int main() {
    const float sr = 48000;
    // 1. Pitch shifter: 440 Hz in, ratio 2 -> energy at 880 Hz dominates 440 Hz
    {
        PitchShifter ps; ps.prepare(sr);
        std::vector<float> out;
        for (int i = 0; i < 48000; ++i) { float y = ps.tick(std::sin(2 * 3.14159265f * 440.0f * float(i) / sr), 2.0f); if (i > 9600) out.push_back(y); }
        float pf = peakFreq(out, 200, 2000, sr);
        CHECK(pf > 840 && pf < 920);                          // an octave up (the two-head method adds some warble)
        CHECK(goertzel(out, 440, sr) < 0.05f * goertzel(out, pf, sr));   // original pitch is gone
    }
    // 2. Reverbs: impulse response decays, no NaN, tail is long, higher decay setting = longer tail
    for (int type = 0; type < 3; ++type) {
        for (int pass = 0; pass < 2; ++pass) {
            ReverbUnit r; r.prepare(sr);
            ReverbSettings s; s.type = type == 2 ? ReverbType::Velvet : type ? ReverbType::Hall : ReverbType::Plate; s.mix = 1.0f; s.p.decay = pass ? 0.9f : 0.3f;
            std::vector<float> L(96000), R(96000);
            for (size_t i = 0; i < L.size(); ++i) r.process(i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, L[i], R[i], s);
            bool finite = true; for (float v : L) finite = finite && std::isfinite(v);
            CHECK(finite);
            float e1 = energy(L, 2000, 12000), e2 = energy(L, 40000, 50000), e3 = energy(L, 86000, 96000);
            CHECK(e1 > 1e-6f);
            CHECK(e2 < e1 && e3 < e2);
            static float tails[3][2];
            tails[type][pass] = e2;
            if (pass == 1) CHECK(tails[type][1] > tails[type][0]);
            CHECK(std::fabs(energy(L, 0, 96000) - energy(R, 0, 96000)) / energy(L, 0, 96000) < 0.9f);   // both channels alive
        }
    }
    // 3. Stability: loud noise through all four types, then silence: output must settle and stay bounded
    for (int t = 0; t < 5; ++t) {
        ReverbUnit r; r.prepare(sr);
        ReverbSettings s; s.type = ReverbType(t); s.mix = 1.0f; s.p.decay = 0.95f; s.shimmerAmount = 0.9f; s.shimmer2Amount = 1.0f; s.shimmer2Semis = 19;
        float peak = 0, l, rr; std::srand(1);
        for (int i = 0; i < 96000; ++i) { float n = (std::rand() / float(RAND_MAX) - 0.5f) * 2; r.process(n, n, l, rr, s); peak = std::max(peak, std::fabs(l)); }
        CHECK(std::isfinite(peak) && peak < 20.0f);
        float endPeak = 0; for (int i = 0; i < 480000; ++i) { r.process(0, 0, l, rr, s); if (i > 470000) endPeak = std::max(endPeak, std::fabs(l)); }
        CHECK(std::isfinite(endPeak) && endPeak < 1.0f); if (!(endPeak < 1.0f)) std::printf("type %d endPeak %g\n", t, endPeak);
    }
    // 4. Shimmer delay: echo arrives at the set time, shifted up an octave on the second repeat
    {
        ShimmerDelay d; d.prepare(sr); ShimmerDelayParams p; p.timeMs = 250; p.mix = 1.0f; p.feedback = 0.7f; p.shiftSemis = 12;
        std::vector<float> out;
        for (int i = 0; i < 96000; ++i) { float l, r2; float x = i < 4800 ? std::sin(2 * 3.14159265f * 440.0f * float(i) / sr) : 0.0f; d.process(x, x, l, r2, p); out.push_back(l); }
        CHECK(energy(out, 12000 + 200, 12000 + 4600) > 0.1f);              // first echo near 250 ms
        CHECK(energy(out, 6000, 11000) < 0.001f);                            // quiet before it
        std::vector<float> second(out.begin() + 24000 + 3000, out.begin() + 24000 + 4800);   // second echo window
        { float pf2 = peakFreq(second, 300, 1500, sr); CHECK(pf2 > 800 && pf2 < 950); }
    }
    // 5. Velvet colour: bright setting has more high-frequency content in the tail than dark
    {
        float hf[2];
        for (int k = 0; k < 2; ++k) {
            ReverbUnit r; r.prepare(sr); ReverbSettings s; s.type = ReverbType::Velvet; s.mix = 1.0f; s.p.color = k ? 1.0f : -1.0f;
            float l, rr, prev = 0, d = 0, tot = 0; std::srand(3);
            for (int i = 0; i < 48000; ++i) { float n = i < 2400 ? (std::rand() / float(RAND_MAX) - 0.5f) : 0.0f; r.process(n, n, l, rr, s);
                if (i > 6000) { d += std::fabs(l - prev); tot += std::fabs(l); } prev = l; }
            hf[k] = d / (tot + 1e-9f);
        }
        CHECK(hf[1] > hf[0] * 1.3f);
    }
    // 6. Wet filters: low-pass at 1 kHz leaves less high-frequency content than open; high-pass removes low end
    {
        float hf[2], lo[2];
        for (int k = 0; k < 2; ++k) {
            ReverbUnit r; r.prepare(sr); ReverbSettings s; s.type = ReverbType::Hall; s.mix = 1.0f;
            if (k) { s.wetLpHz = 1000.0f; }
            float l, rr, prev = 0, d = 0, tot = 0; std::srand(5);
            for (int i = 0; i < 48000; ++i) { float n = i < 2400 ? (std::rand() / float(RAND_MAX) - 0.5f) : 0.0f; r.process(n, n, l, rr, s);
                if (i > 6000) { d += std::fabs(l - prev); tot += std::fabs(l); } prev = l; }
            hf[k] = d / (tot + 1e-9f);
        }
        CHECK(hf[1] < hf[0] * 0.6f);
        for (int k = 0; k < 2; ++k) {
            ReverbUnit r; r.prepare(sr); ReverbSettings s; s.type = ReverbType::Hall; s.mix = 1.0f;
            if (k) { s.wetHpHz = 1500.0f; }
            float l, rr, e = 0, e2 = 0; std::srand(5);
            for (int i = 0; i < 48000; ++i) { float n = i < 2400 ? (std::rand() / float(RAND_MAX) - 0.5f) : 0.0f; r.process(n, n, l, rr, s); if (i > 6000) { e += l * l; } }
            (void)e2; lo[k] = e;
        }
        CHECK(lo[1] < lo[0] * 0.6f);
    }
    // 7. Second shimmer voice adds energy at its own pitch: with amount 0 it is exactly the same as before
    {
        ReverbUnit a, b; a.prepare(sr); b.prepare(sr);
        ReverbSettings s; s.type = ReverbType::ShimmerHall; s.mix = 1.0f; s.shimmer2Semis = -12; s.shimmer2Amount = 0.0f;
        ReverbSettings s2 = s; s2.shimmer2Amount = 1.0f;
        float la, lb, rr, diff = 0;
        for (int i = 0; i < 48000; ++i) { float x = i < 2400 ? std::sin(2*3.14159265f*440.0f*float(i)/sr) : 0.0f; a.process(x, x, la, rr, s); b.process(x, x, lb, rr, s2); if (i > 20000) diff += std::fabs(la - lb); }
        CHECK(diff > 0.01f);
    }
    if (!fails) std::puts("reverb_test: all passed");
    return fails ? 1 : 0;
}
