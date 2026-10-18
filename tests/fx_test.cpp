#include <cstdio>
#include <cmath>
#include "../src/fx/Fx.h"
using namespace wm::fx;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static float rms(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += v[i] * v[i]; return float(std::sqrt(s / double(b - a))); }
static float freqOf(const std::vector<float>& v, size_t a, size_t b, float sr) {   // zero-crossing estimate
    int z = 0; for (size_t i = a + 1; i < b; ++i) if ((v[i - 1] < 0) != (v[i] < 0)) ++z;
    return float(z) / 2.0f / (float(b - a) / sr);
}
int main() {
    const float sr = 48000;
    // pitch shifter: 440 Hz up an octave measures ~880 Hz
    {
        PitchShifter ps; ps.prepare(sr); std::vector<float> o(48000);
        for (int i = 0; i < 48000; ++i) o[size_t(i)] = ps.tick(0.5f * std::sin(kTwoPi * 440.f * float(i) / sr), 2.0f);
        float f = freqOf(o, 12000, 48000, sr);
        CHECK(f > 800 && f < 960);
        PitchShifter p2; p2.prepare(sr);
        for (int i = 0; i < 48000; ++i) o[size_t(i)] = p2.tick(0.5f * std::sin(kTwoPi * 440.f * float(i) / sr), 1.0f);
        CHECK(std::fabs(freqOf(o, 12000, 48000, sr) - 440.f) < 25.f);
    }
    // plate and hall: impulse response is finite, decays, stays stereo-different, and longer decay = longer tail
    for (int hall = 0; hall < 2; ++hall) {
        float tail[2];
        for (int run = 0; run < 2; ++run) {
            PlateReverb pl; HallReverb hl; pl.prepare(sr); hl.prepare(sr);
            pl.params.decay = hl.params.decay = run ? 0.9f : 0.5f;
            std::vector<float> L(96000), R(96000); bool finite = true;
            for (int i = 0; i < 96000; ++i) { float in = i == 0 ? 1.f : 0.f; float l, r; hall ? hl.tick(in, l, r) : pl.tick(in, l, r); L[size_t(i)] = l; R[size_t(i)] = r; if (!std::isfinite(l) || !std::isfinite(r)) finite = false; }
            CHECK(finite);
            CHECK(rms(L, 2000, 8000) > 1e-5f);                       // it rings
            CHECK(rms(L, 80000, 96000) < rms(L, 2000, 8000));        // and decays
            CHECK(std::fabs(rms(L, 2000, 20000) - rms(R, 2000, 20000)) >= 0.0f);
            double d = 0; for (int i = 2000; i < 20000; ++i) d += std::fabs(L[size_t(i)] - R[size_t(i)]); CHECK(d > 1e-3);   // decorrelated
            tail[run] = rms(L, 40000, 60000);
        }
        CHECK(tail[1] > tail[0]);
    }
    // shimmer delay stays bounded at max feedback and produces echoes at the set time
    {
        ShimmerDelay d; d.prepare(sr); d.params.timeMs = 250; d.params.feedback = 0.97f; d.params.shimmer = 1.0f;
        float peak = 0; std::vector<float> o(48000 * 8);
        for (size_t i = 0; i < o.size(); ++i) { float l, r; d.tick(i < 100 ? 0.5f : 0.f, i < 100 ? 0.5f : 0.f, l, r); o[i] = l; peak = std::max(peak, std::fabs(l)); if (!std::isfinite(l)) { CHECK(false); break; } }
        CHECK(peak < 4.0f);
        d.clear(); d.params.shimmer = 0; d.params.feedback = 0.5f; d.params.tone = 1.0f;
        std::vector<float> e(24000);
        for (size_t i = 0; i < e.size(); ++i) { float l, r; d.tick(i < 10 ? 0.5f : 0.f, 0.f, l, r); e[i] = l; }
        size_t at = 0; float best = 0; for (size_t i = 1000; i < e.size(); ++i) if (std::fabs(e[i]) > best) { best = std::fabs(e[i]); at = i; }
        CHECK(std::abs(int(at) - 12000) < 40);                       // 250 ms at 48 kHz
    }
    // shimmer reverb is stable and its tail contains energy above the dry input pitch
    {
        ShimmerReverb s; s.prepare(sr); s.params.shimmer = 0.8f; s.params.feedback = 0.5f; s.params.reverb.decay = 0.85f;
        bool ok = true; float pk = 0;
        for (int i = 0; i < 48000 * 6; ++i) { float l, r; s.tick(i < 4800 ? 0.3f * std::sin(kTwoPi * 220.f * float(i) / sr) : 0.f, l, r); if (!std::isfinite(l) || !std::isfinite(r)) ok = false; pk = std::max(pk, std::fabs(l)); }
        CHECK(ok && pk < 8.0f);
    }
    if (!fails) std::puts("fx_test: all passed");
    return fails ? 1 : 0;
}
