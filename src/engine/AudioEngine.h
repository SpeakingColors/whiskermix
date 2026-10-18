// Software mixer core. Pure C++, no PipeWire or JUCE calls, so it is fully testable.
// Channels are mono inputs. Each bus is a stereo pair. Routing matrix (channel x bus) holds send gain in dB.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "../mixer/MixerModel.h"
#include "Dsp.h"
#include "../mixer/RoutingMatrix.h"

namespace wm::engine {

inline float dbToLin(float db) { return db <= -119.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }

class AudioEngine {
public:
    AudioEngine(mixer::Mixer& m, mixer::RoutingMatrix& r, float sampleRate = 48000.0f)
        : mx_(m), rt_(r) {
        setSampleRate(sampleRate);
        resize();
    }
    void setSampleRate(float sr) { sr_ = sr; coef_ = 1.0f - std::exp(-1.0f / (0.005f * sr)); }  // ~5 ms smoothing, no zipper noise

    // Call after channels or buses are added, or when the device block size changes (NOT from the audio thread).
    void prepare(int maxFrames) { maxFrames_ = maxFrames; resize(); }
    void resize() {
        size_t nc = mx_.state().channels.size(), nb = mx_.state().buses.size();
        cur_.assign(nc * nb * 2, 0.0f);
        peakIn_.assign(nc, 0.0f);
        peakOut_.assign(nb * 2, 0.0f);
        fx_.assign(nc, ChannelFx{});
        buf_.assign(nc, std::vector<float>(size_t(maxFrames_)));
    }

    // in[c] = nframes samples of channel c. out[2*b], out[2*b+1] = bus b left and right.
    void process(const float* const* in, float* const* out, int n) {
        const auto& st = mx_.state();
        size_t nc = st.channels.size(), nb = st.buses.size();
        // Realtime rule: no allocation here. If the setup changed or the block is too big, output silence until prepare().
        if (cur_.size() != nc * nb * 2 || n > maxFrames_) { for (size_t o = 0; o < nb * 2; ++o) std::fill(out[o], out[o] + n, 0.0f); return; }
        NoDenormals nd;
        for (size_t o = 0; o < nb * 2; ++o) std::fill(out[o], out[o] + n, 0.0f);
        std::fill(peakIn_.begin(), peakIn_.end(), 0.0f);
        sr_ = sr_ > 0 ? sr_ : 48000.0f;
        for (size_t c = 0; c < nc; ++c) {
            const auto& ch = st.channels[c];
            for (int i = 0; i < n; ++i) peakIn_[c] = std::max(peakIn_[c], std::fabs(in[c][i]));
            const float* src = in[c];
            if (ch.fx.eqOn || ch.fx.compOn) {
                fx_[c].process(ch.fx, sr_, in[c], buf_[c].data(), n);
                src = buf_[c].data();
            }
            float strip = dbToLin(mx_.effectiveGainDb(int(c))) * (ch.phaseL ? -1.0f : 1.0f);
            float pl = std::cos((ch.pan + 1.0f) * 0.78539816f), pr = std::sin((ch.pan + 1.0f) * 0.78539816f);  // constant power
            for (size_t b = 0; b < nb; ++b) {
                float send = rt_.routed(int(c), int(b)) ? dbToLin(rt_.get(int(c), int(b))) : 0.0f;
                float tl = strip * send * pl, tr = strip * send * pr;
                float& gl = cur_[(c * nb + b) * 2];
                float& gr = cur_[(c * nb + b) * 2 + 1];
                float *ol = out[b * 2], *orr = out[b * 2 + 1];
                const float* x = src;
                for (int i = 0; i < n; ++i) {
                    gl += (tl - gl) * coef_;
                    gr += (tr - gr) * coef_;
                    ol[i] += x[i] * gl;
                    orr[i] += x[i] * gr;
                }
            }
        }
        for (size_t b = 0; b < nb; ++b) {
            const auto& bus = st.buses[b];
            float v = dbToLin(bus.volumeDb + (bus.dim ? digiface::kDimDb : 0.0f));
            for (size_t k = 0; k < 2; ++k) {
                float* o = out[b * 2 + k];
                float pk = 0;
                for (int i = 0; i < n; ++i) { o[i] *= v; pk = std::max(pk, std::fabs(o[i])); }
                peakOut_[b * 2 + k] = pk;
            }
        }
    }
    float gainReductionDb(int c) const { return fx_.at(size_t(c)).gainReductionDb(); }
    float inputPeakDb(int c) const { return toDb(peakIn_.at(size_t(c))); }
    float outputPeakDb(int o) const { return toDb(peakOut_.at(size_t(o))); }

private:
    static float toDb(float v) { return v < 1e-6f ? -120.0f : 20.0f * std::log10(v); }
    mixer::Mixer& mx_;
    mixer::RoutingMatrix& rt_;
    float coef_ = 0.01f, sr_ = 48000.0f;
    int maxFrames_ = 4096;
    std::vector<ChannelFx> fx_;
    std::vector<std::vector<float>> buf_;
    std::vector<float> cur_, peakIn_, peakOut_;
};

}  // namespace wm::engine
