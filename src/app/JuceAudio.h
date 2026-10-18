// Cross-platform audio path (Windows/macOS/Linux) using JUCE's audio device. NOT built yet.
// Lets the mixer be tested on Windows (WASAPI/ASIO) before Bazzite. PipeWire path stays Linux-only.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "../engine/AudioEngine.h"

class JuceAudio : public juce::AudioIODeviceCallback {
public:
    explicit JuceAudio(wm::engine::AudioEngine& e) : eng(e) {}
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
        eng.setSampleRate(float(d->getCurrentSampleRate()));
        maxN = d->getCurrentBufferSizeSamples() * 2;          // headroom; all allocation happens here, not in the callback
        eng.prepare(maxN);
        zero.assign(size_t(maxN), 0.0f);
        ins.assign(size_t(wantIn), nullptr);
        tmp.assign(size_t(wantOut), std::vector<float>(size_t(maxN)));   // sized for the maximum channels and buses
        outs.clear(); for (auto& v : tmp) outs.push_back(v.data());
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut,
                                          int n, const juce::AudioIODeviceCallbackContext&) override {
        // Engine wants one input per channel and two outputs per bus; pad missing ones with silence.
        if (n > maxN) { for (int o = 0; o < nOut; ++o) if (out[o]) std::fill(out[o], out[o] + n, 0.0f); return; }
        for (int i = 0; i < wantIn; ++i) ins[size_t(i)] = (i < nIn && in[i]) ? in[i] : zero.data();
        eng.process(ins.data(), outs.data(), n);
        for (int o = 0; o < nOut; ++o) if (out[o]) for (int i = 0; i < n; ++i) out[o][i] = o < wantOut ? tmp[size_t(o)][size_t(i)] : 0.0f;
    }
    int wantIn = int(wm::engine::kMaxChannels), wantOut = int(wm::engine::kMaxBuses) * 2, maxN = 0;
private:
    wm::engine::AudioEngine& eng;
    std::vector<float> zero; std::vector<const float*> ins; std::vector<std::vector<float>> tmp; std::vector<float*> outs;
};
