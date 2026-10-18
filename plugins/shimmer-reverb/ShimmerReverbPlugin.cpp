// Shimmer Reverb: plate, hall, shimmer plate and shimmer hall in one plugin. Uses the shared DSP in src/engine.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../common/FxEditor.h"
#include "../../src/engine/Reverb.h"

using namespace wmplug;

class ShimmerReverbProcessor : public juce::AudioProcessor {
public:
    ShimmerReverbProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", layout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"type", 1}, "Type",
              juce::StringArray{"Plate", "Hall", "Shimmer Plate", "Shimmer Hall", "Lush"}, 3));
        l.add(floatParam("decay", "Decay", 0, 1, 0.6f));
        l.add(floatParam("damp", "Damping (Hz)", 500, 16000, 6000, 3000));
        l.add(floatParam("predelay", "Pre-delay (ms)", 0, 250, 20, 40));
        l.add(floatParam("size", "Size", 0.5f, 1.6f, 1.0f));
        l.add(floatParam("mod", "Modulation", 0, 1, 0.5f));
        l.add(floatParam("shift", "Shimmer (semitones)", -12, 12, 12));
        l.add(floatParam("shimmer", "Shimmer amount", 0, 1, 0.5f));
        l.add(floatParam("mix", "Mix", 0, 1, 0.35f));
        return l;
    }
    void prepareToPlay(double sr, int) override { fx.prepare(float(sr)); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }
    wm::engine::ReverbSettings settings() const {
        wm::engine::ReverbSettings s;
        s.type = wm::engine::ReverbType(int(*apvts.getRawParameterValue("type")));
        s.p.decay = *apvts.getRawParameterValue("decay"); s.p.dampHz = *apvts.getRawParameterValue("damp");
        s.p.preDelayMs = *apvts.getRawParameterValue("predelay"); s.p.size = *apvts.getRawParameterValue("size");
        s.p.modDepth = *apvts.getRawParameterValue("mod"); s.shimmerSemis = *apvts.getRawParameterValue("shift");
        s.shimmerAmount = *apvts.getRawParameterValue("shimmer"); s.mix = *apvts.getRawParameterValue("mix");
        return s;
    }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd;
        auto s = settings();
        auto* l = b.getWritePointer(0); auto* r = b.getNumChannels() > 1 ? b.getWritePointer(1) : l;
        for (int i = 0; i < b.getNumSamples(); ++i) { float ol, orr; fx.process(l[i], r[i], ol, orr, s); l[i] = ol; r[i] = orr; }
    }
    juce::AudioProcessorEditor* createEditor() override {
        auto* e = new FxEditor(*this, apvts, {{"decay", "Decay"}, {"damp", "Damping"}, {"predelay", "Pre-delay"}, {"size", "Size"},
                               {"mod", "Modulation"}, {"shift", "Shimmer pitch"}, {"shimmer", "Shimmer amount"}, {"mix", "Mix"}}, "Shimmer Reverb");
        e->graph.draw = [this](juce::Graphics& g, juce::Rectangle<float> r) {   // decay curve (dB over time) from the same numbers the DSP uses
            auto s = settings();
            bool plate = s.type == wm::engine::ReverbType::Plate || s.type == wm::engine::ReverbType::ShimmerPlate;
            float rt = plate ? 0.27f * 60.f / std::max(0.1f, -20.f * std::log10(std::min(0.97f, 0.2f + 0.77f * s.p.decay)))
                             : 0.3f + 9.7f * s.p.decay * s.p.decay;
            juce::Path p;
            for (float x = 0; x < r.getWidth(); x += 2) {
                float t = 12.f * x / r.getWidth(), db = std::max(-70.f, -60.f * t / rt);
                float y = r.getY() + r.getHeight() * (-db) / 70.f;
                x == 0 ? p.startNewSubPath(r.getX(), y) : p.lineTo(r.getX() + x, y);
            }
            g.setColour(wmplug::col::teal); g.strokePath(p, juce::PathStrokeType(2.0f));
            g.setColour(wmplug::col::text); g.setFont(12.f);
            g.drawText("RT60 " + juce::String(rt, 1) + " s" + (plate ? " (approx)" : ""), r.toNearestInt(), juce::Justification::topRight);
        };
        return e;
    }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Shimmer Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& d) override { if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, d); }
    void setStateInformation(const void* d, int n) override { if (auto xml = getXmlFromBinary(d, n)) apvts.replaceState(juce::ValueTree::fromXml(*xml)); }
private:
    juce::AudioProcessorValueTreeState apvts;
    wm::engine::ReverbUnit fx;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ShimmerReverbProcessor(); }
