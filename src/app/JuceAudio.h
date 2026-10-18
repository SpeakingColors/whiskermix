// Cross-platform audio path (Windows/macOS/Linux) using JUCE's audio device. NOT built yet.
// Lets the mixer be tested on Windows (WASAPI/ASIO) before Bazzite. PipeWire path stays Linux-only.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "../engine/AudioEngine.h"

class JuceAudio : public juce::AudioIODeviceCallback {
public:
    explicit JuceAudio(wm::engine::AudioEngine& e) : eng(e) {}
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override { eng.setSampleRate(float(d->getCurrentSampleRate())); }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut,
                                          int n, const juce::AudioIODeviceCallbackContext&) override {
        // Engine wants one input per channel and two outputs per bus; pad missing ones with silence.
        zero.assign(size_t(n), 0.0f);
        ins.assign(size_t(wantIn), zero.data());
        for (int i = 0; i < nIn && i < wantIn; ++i) if (in[i]) ins[size_t(i)] = in[i];
        tmp.assign(size_t(wantOut), std::vector<float>(size_t(n)));
        outs.clear(); for (auto& v : tmp) outs.push_back(v.data());
        eng.process(ins.data(), outs.data(), n);
        for (int o = 0; o < nOut; ++o) if (out[o]) for (int i = 0; i < n; ++i) out[o][i] = o < wantOut ? tmp[size_t(o)][size_t(i)] : 0.0f;
    }
    int wantIn = 8, wantOut = 4;
private:
    wm::engine::AudioEngine& eng;
    std::vector<float> zero; std::vector<const float*> ins; std::vector<std::vector<float>> tmp; std::vector<float*> outs;
};
