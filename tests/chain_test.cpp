#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "../src/engine/AudioEngine.h"
#include "../src/mixer/StateJson.h"
using namespace wm;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static std::vector<float> tone(int n, float amp, float hz = 220.0f) { std::vector<float> v; v.resize(size_t(n)); for (int i = 0; i < n; ++i) v[size_t(i)] = amp * std::sin(2 * 3.14159265f * hz * float(i) / 48000.0f) + 0.2f * amp * std::sin(2 * 3.14159265f * 3100.0f * float(i) / 48000.0f); return v; }
// Renders 1 channel through the engine with the given chain (pan hard left so out[0] = channel at unity)
static std::vector<float> render(engine::FxChainParams chain, const std::vector<float>& sig, int blocks, float* grOut = nullptr) {
    mixer::Mixer m(1, 1); mixer::RoutingMatrix r(1, 1); r.set(0, 0, 0.0f); m.state().channels[0].pan = -1.0f; m.state().channels[0].chain = chain;
    engine::AudioEngine e(m, r); e.prepare(int(sig.size()));
    std::vector<float> o0(sig.size()), o1(sig.size()); const float* ip[1] = {sig.data()}; float* op[2] = {o0.data(), o1.data()};
    e.sync(); for (int b = 0; b < blocks; ++b) e.process(ip, op, int(sig.size()));
    if (grOut) *grOut = e.gainReductionDb(0);
    return o0;
}
static float rms(const std::vector<float>& v, size_t from = 0) { double a = 0; for (size_t i = from; i < v.size(); ++i) a += double(v[i]) * v[i]; return float(std::sqrt(a / double(v.size() - from))); }
static float maxdiff(const std::vector<float>& a, const std::vector<float>& b) { float d = 0; for (size_t i = 0; i < a.size(); ++i) d = std::max(d, std::fabs(a[i] - b[i])); return d; }

int main() {
    using namespace engine;
    auto sig = tone(4800, 0.4f);
    auto plain = render(FxChainParams{}, sig, 3);
    CHECK(maxdiff(plain, sig) < 1e-4f);                                   // all off: untouched (only the equal-power pan gain: hard left = 1)
    for (int k = 0; k < kFxKinds; ++k) {
        FxChainParams c; c.s[k].on = true;
        if (k == kPurr) { c.s[k].p[0] = 70; c.s[k].p[1] = 40; c.s[k].p[2] = 20; }
        if (k == kLimiter) { c.s[k].p[0] = -12; }
        if (k == kMbComp) { c.s[k].p[0] = c.s[k].p[1] = c.s[k].p[2] = -30; c.s[k].p[3] = 100; }
        if (k == kOpto) { c.s[k].p[0] = 80; }
        if (k == kMidSide) continue;                                       // needs a pair, tested below
        auto out = render(c, sig, 6);
        bool finite = true; for (float v : out) finite = finite && std::isfinite(v) && std::fabs(v) < 8.0f;
        if (!finite) std::printf("not finite: %s\n", fxSpec(k).name);
        CHECK(finite);
        if (maxdiff(out, sig) <= 1e-3f) std::printf("no effect: %s\n", fxSpec(k).name);
        CHECK(maxdiff(out, sig) > 1e-3f);                                  // on = it does something
        c.s[k].on = false;
        auto off = render(c, sig, 6);
        CHECK(maxdiff(off, sig) < 1e-4f);                                  // off again = bypass, identical to the input
    }
    // Off after being on, in the same engine: the tail does not leak and the signal is untouched
    {
        mixer::Mixer m(1, 1); mixer::RoutingMatrix r(1, 1); r.set(0, 0, 0.0f); m.state().channels[0].pan = -1.0f;
        auto& ch = m.state().channels[0].chain; ch.s[kReverb].on = true; ch.s[kEcho].on = true; ch.s[kReverb].p[3] = 100; ch.s[kEcho].p[3] = 50;
        engine::AudioEngine e(m, r); e.prepare(4800);
        std::vector<float> o0(4800), o1(4800); const float* ip[1] = {sig.data()}; float* op[2] = {o0.data(), o1.data()};
        e.sync(); for (int b = 0; b < 8; ++b) e.process(ip, op, 4800);
        CHECK(e.fxOutPeak(0, kReverb) > 0.0f && e.fxInPeak(0, kEcho) > 0.0f);
        ch.s[kReverb].on = false; ch.s[kEcho].on = false; e.sync(); e.process(ip, op, 4800);
        CHECK(maxdiff(o0, sig) < 1e-4f);
        CHECK(e.fxInPeak(0, kReverb) == 0.0f && e.fxOutPeak(0, kReverb) == 0.0f && e.fxInPeak(0, kEcho) == 0.0f);   // meters idle when off
        ch.s[kEcho].on = true; ch.s[kEcho].p[3] = 100; e.sync(); std::vector<float> zero(4800, 0.0f); const float* iz[1] = {zero.data()}; e.process(iz, op, 4800);
        CHECK(rms(o0) < 1e-6f);                                            // switching on again starts clean: no old tail
    }
    // Specific behaviour
    {
        FxChainParams rv; rv.s[kReverb].on = true; rv.s[kReverb].p[3] = 100; auto wet = render(rv, sig, 8);
        float wp = 0; for (float v : wet) wp = std::max(wp, std::fabs(v)); CHECK(wp < 1.0f && wp > 0.01f);   // fully wet reverb stays sane on a sustained tone
        FxChainParams c; c.s[kLimiter].on = true; c.s[kLimiter].p[0] = -6; c.s[kLimiter].p[3] = 12;
        auto out = render(c, tone(4800, 0.9f), 4); float pk = 0; for (float v : out) pk = std::max(pk, std::fabs(v));
        CHECK(pk <= std::pow(10.0f, -6.0f / 20.0f) + 1e-4f);               // never above the ceiling
        FxChainParams p; p.s[kPurr].on = true; p.s[kPurr].p[0] = 60;
        auto lo = tone(4800, 0.05f), hi = tone(4800, 0.5f); auto a = render(p, lo, 4), b = render(p, hi, 4);
        CHECK(rms(b, 2400) / rms(hi, 2400) < 1.6f && rms(b, 2400) / rms(hi, 2400) > 0.4f);   // makeup keeps the level near the input
        FxChainParams mb; mb.s[kMbComp].on = true; mb.s[kMbComp].p[0] = mb.s[kMbComp].p[1] = mb.s[kMbComp].p[2] = -40; mb.s[kMbComp].p[3] = 100;
        float gr = 0; render(mb, tone(4800, 0.6f), 4, &gr); (void)gr;
        auto loud = render(mb, tone(4800, 0.6f), 4); CHECK(rms(loud, 2400) < rms(tone(4800, 0.6f), 2400) * 0.8f);   // bands are turned down
    }
    // M/S on a linked pair: width 0 makes the pair mono, off leaves it alone
    {
        mixer::Mixer m(2, 1); mixer::RoutingMatrix r(2, 1); r.set(0, 0, 0.0f); r.set(1, 0, 0.0f);
        m.state().channels[0].stereoLinked = true; m.state().channels[0].pan = 0; m.state().channels[0].chain.s[kMidSide].on = true;
        m.state().channels[0].chain.s[kMidSide].p[2] = 0; m.state().channels[0].chain.s[kMidSide].p[3] = 0;
        engine::AudioEngine e(m, r); e.prepare(4800);
        std::vector<float> l = tone(4800, 0.4f), rr = tone(4800, 0.4f, 330.0f), o0(4800), o1(4800);
        const float* ip[2] = {l.data(), rr.data()}; float* op[2] = {o0.data(), o1.data()};
        e.sync(); for (int b = 0; b < 4; ++b) e.process(ip, op, 4800);
        CHECK(maxdiff(o0, o1) < 1e-4f && rms(o0) > 0.05f);
        m.state().channels[0].chain.s[kMidSide].on = false; e.sync(); e.process(ip, op, 4800);
        CHECK(maxdiff(o0, o1) > 0.05f);
    }
    // Saved state round trip
    {
        mixer::Mixer m(1, 1); auto& c = m.state().channels[0].chain; c.s[kPhaser].on = true; c.s[kPhaser].p[2] = 8; c.s[kOpto].on = true; c.s[kOpto].p[1] = 9.5f;
        auto js = mixer::toJson(m.state()); mixer::State back; CHECK(mixer::fromJson(js, back));
        const auto& b = back.channels[0].chain;
        CHECK(b.s[kPhaser].on && b.s[kPhaser].p[2] == 8.0f && b.s[kOpto].on && std::fabs(b.s[kOpto].p[1] - 9.5f) < 1e-3f && !b.s[kReverb].on && b.s[kEcho].p[0] == 375.0f);
    }
    std::printf(fails ? "chain_test: %d FAILED\n" : "chain_test: all passed\n", fails);
    return fails ? 1 : 0;
}
