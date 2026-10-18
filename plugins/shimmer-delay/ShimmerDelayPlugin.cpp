// Shimmer Delay: stereo delay whose repeats are pitch shifted (octave up by default). Uses the shared DSP in src/engine.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../common/FxEditor.h"
#include "../../src/engine/Reverb.h"

using namespace wmplug;

class ShimmerDelayProcessor : public juce::AudioProcessor {
public:
    ShimmerDelayProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", layout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add(floatParam("time", "Time (ms)", 20, 2000, 450, 400));
        l.add(floatParam("feedback", "Feedback", 0, 0.95f, 0.55f));
        l.add(floatParam("shift", "Shift (semitones)", -12, 12, 12));
        l.add(floatParam("shimmer", "Shimmer amount", 0, 1, 1));
        l.add(floatParam("damp", "Damping (Hz)", 500, 16000, 5000, 3000));
        l.add(floatParam("mix", "Mix", 0, 1, 0.4f));
        return l;
    }

    void prepareToPlay(double sr, int) override { fx.prepare(float(sr)); time.reset(sr, 0.25); time.setCurrentAndTargetValue(*apvts.getRawParameterValue("time")); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd;
        wm::engine::ShimmerDelayParams p;
        p.feedback = *apvts.getRawParameterValue("feedback"); p.shiftSemis = *apvts.getRawParameterValue("shift");
        p.shimmerAmount = *apvts.getRawParameterValue("shimmer"); p.dampHz = *apvts.getRawParameterValue("damp");
        p.mix = *apvts.getRawParameterValue("mix");
        time.setTargetValue(*apvts.getRawParameterValue("time"));   // smoothed so moving the time does not click
        auto* l = b.getWritePointer(0); auto* r = b.getNumChannels() > 1 ? b.getWritePointer(1) : l;
        for (int i = 0; i < b.getNumSamples(); ++i) {
            p.timeMs = time.getNextValue();
            float ol, orr; fx.process(l[i], r[i], ol, orr, p);
            l[i] = ol; r[i] = orr;
        }
    }
    juce::AudioProcessorEditor* createEditor() override {
        auto* e = new FxEditor(*this, apvts, {{"time", "Time"}, {"feedback", "Feedback"}, {"shift", "Shift"}, {"shimmer", "Shimmer"}, {"damp", "Damping"}, {"mix", "Mix"}}, "Shimmer Delay");
        e->graph.draw = [this](juce::Graphics& g, juce::Rectangle<float> r) {   // the echo train: height of each repeat from feedback, damping and shimmer
            float fb = *apvts.getRawParameterValue("feedback"), t = *apvts.getRawParameterValue("time");
            float semis = *apvts.getRawParameterValue("shift") * *apvts.getRawParameterValue("shimmer");
            float damp = *apvts.getRawParameterValue("damp");
            float total = 8000.0f;   // ms shown
            float level = 1.0f;
            for (int k = 1; k * t < total && k < 40; ++k) {
                level *= fb * (1.0f - 0.06f * std::fabs(semis) / 12.0f * (5000.0f / damp));   // each shifted repeat loses highs
                if (level < 0.01f) break;
                float x = r.getX() + r.getWidth() * (k * t) / total, h = r.getHeight() * std::min(1.0f, level);
                g.setColour(wmplug::col::teal.withAlpha(0.9f)); g.fillRect(x - 2, r.getBottom() - h, 4.0f, h);
                g.setColour(wmplug::col::violet); g.setFont(11.0f);
                g.drawText(juce::String(juce::roundToInt(semis * k)) + "st", juce::Rectangle<float>(x - 20, r.getY(), 40, 14), juce::Justification::centred);
            }
        };
        return e;
    }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Shimmer Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& d) override { if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, d); }
    void setStateInformation(const void* d, int n) override { if (auto xml = getXmlFromBinary(d, n)) apvts.replaceState(juce::ValueTree::fromXml(*xml)); }

private:
    juce::AudioProcessorValueTreeState apvts;
    wm::engine::ShimmerDelay fx;
    juce::SmoothedValue<float> time;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ShimmerDelayProcessor(); }
