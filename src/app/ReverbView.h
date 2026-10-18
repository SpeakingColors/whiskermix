// Reverb and shimmer delay page for one bus. Every slider moves the bus settings the audio thread reads
// (via AudioEngine::sync), and the decay and damping graphs redraw from the same numbers.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../mixer/MixerModel.h"
#include "MixerView.h"

namespace wmui {

class DecayGraph : public juce::Component {
public:
    std::function<const wm::mixer::Bus*()> bus;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(col::bg); g.fillRoundedRectangle(r, 6);
        auto* b = bus ? bus() : nullptr; if (!b) return;
        const auto& s = b->reverb;
        float rt = rt60(s);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        for (int db : {-20, -40, -60}) g.drawHorizontalLine(int(r.getHeight() * float(-db) / 70.f), 0, r.getRight());
        // energy decay curve over 0..12 s
        juce::Path p; const float tMax = 12.f;
        for (int x = 0; x < getWidth(); x += 2) {
            float t = tMax * float(x) / float(getWidth());
            float db = std::max(-70.f, -60.f * t / rt);
            float y = r.getHeight() * (-db) / 70.f;
            x == 0 ? p.startNewSubPath(0, y) : p.lineTo(float(x), y);
        }
        g.setColour(b->reverbOn ? col::teal : col::teal.withAlpha(0.3f)); g.strokePath(p, juce::PathStrokeType(2.0f));
        // damping lowpass response 100 Hz..20 kHz, drawn small at the bottom
        juce::Path q; auto box = r.removeFromBottom(r.getHeight() * 0.35f).reduced(6);
        for (int x = 0; x < int(box.getWidth()); x += 2) {
            float f = 100.f * std::pow(200.f, float(x) / box.getWidth());
            float a = 1.f / std::sqrt(1.f + (f / s.p.dampHz) * (f / s.p.dampHz));
            float y = box.getBottom() - box.getHeight() * std::max(0.f, (20.f * std::log10(a) + 24.f) / 24.f);
            x == 0 ? q.startNewSubPath(box.getX(), y) : q.lineTo(box.getX() + float(x), y);
        }
        g.setColour(col::violet); g.strokePath(q, juce::PathStrokeType(1.5f));
        g.setColour(col::text); g.setFont(12.f);
        g.drawText("RT60 " + juce::String(rt, 1) + " s" + (isPlate(s) ? " (approx)" : ""), getLocalBounds().removeFromTop(18).reduced(6, 0), juce::Justification::right);
    }
    static bool isPlate(const wm::engine::ReverbSettings& s) { return s.type == wm::engine::ReverbType::Plate || s.type == wm::engine::ReverbType::ShimmerPlate; }
    static float rt60(const wm::engine::ReverbSettings& s) {
        if (!isPlate(s)) return 0.3f + 9.7f * s.p.decay * s.p.decay;   // same formula the hall uses
        float d = std::min(0.97f, 0.2f + 0.77f * s.p.decay);          // plate loop gain; one pass of the tank is about 0.27 s
        return 0.27f * 60.f / std::max(0.1f, -20.f * std::log10(d));
    }
};

class ReverbView : public juce::Component {
public:
    ReverbView(wm::mixer::Mixer& m, std::function<void()> changed) : mx(m), onChanged(std::move(changed)) {
        busBox.addItem("Bus 1", 1); busBox.addItem("Bus 2", 2); busBox.setSelectedId(1, juce::dontSendNotification);
        busBox.onChange = [this] { refresh(); };
        addAndMakeVisible(busBox);
        const char* names[] = {"Plate", "Hall", "Shimmer Plate", "Shimmer Hall", "Lush"};
        for (int i = 0; i < 5; ++i) {
            auto& b = typeB[i]; b.setButtonText(names[i]); b.setRadioGroupId(77); b.setClickingTogglesState(true);
            b.setColour(juce::TextButton::buttonOnColourId, col::violet);
            b.onClick = [this, i] { bus().reverb.type = wm::engine::ReverbType(i); edited(); };
            addAndMakeVisible(b);
        }
        auto tog = [this](juce::TextButton& b, const char* t, bool wm::mixer::Bus::*f) {
            b.setButtonText(t); b.setClickingTogglesState(true); b.setColour(juce::TextButton::buttonOnColourId, col::teal);
            b.onClick = [this, &b, f] { bus().*f = b.getToggleState(); edited(); }; addAndMakeVisible(b); };
        tog(revOn, "Reverb ON", &wm::mixer::Bus::reverbOn); tog(dlyOn, "Delay ON", &wm::mixer::Bus::delayOn);
        add("Decay", 0, 1, 0.01, [](wm::mixer::Bus& b) -> float& { return b.reverb.p.decay; });
        add("Damping Hz", 500, 16000, 10, [](wm::mixer::Bus& b) -> float& { return b.reverb.p.dampHz; }, 3.0);
        add("Pre-delay ms", 0, 250, 1, [](wm::mixer::Bus& b) -> float& { return b.reverb.p.preDelayMs; });
        add("Size", 0.5, 1.6, 0.01, [](wm::mixer::Bus& b) -> float& { return b.reverb.p.size; });
        add("Modulation", 0, 1, 0.01, [](wm::mixer::Bus& b) -> float& { return b.reverb.p.modDepth; });
        add("Shimmer semitones", -12, 12, 1, [](wm::mixer::Bus& b) -> float& { return b.reverb.shimmerSemis; });
        add("Shimmer amount", 0, 1, 0.01, [](wm::mixer::Bus& b) -> float& { return b.reverb.shimmerAmount; });
        add("Reverb mix", 0, 1, 0.01, [](wm::mixer::Bus& b) -> float& { return b.reverb.mix; });
        add("Delay time ms", 20, 2000, 1, [](wm::mixer::Bus& b) -> float& { return b.delay.timeMs; }, 2.0);
        add("Delay feedback", 0, 0.95, 0.01, [](wm::mixer::Bus& b) -> float& { return b.delay.feedback; });
        add("Delay shift semi", -12, 12, 1, [](wm::mixer::Bus& b) -> float& { return b.delay.shiftSemis; });
        add("Delay damping Hz", 500, 16000, 10, [](wm::mixer::Bus& b) -> float& { return b.delay.dampHz; }, 3.0);
        add("Delay shimmer", 0, 1, 0.01, [](wm::mixer::Bus& b) -> float& { return b.delay.shimmerAmount; });
        add("Delay mix", 0, 1, 0.01, [](wm::mixer::Bus& b) -> float& { return b.delay.mix; });
        graph.bus = [this]() -> const wm::mixer::Bus* { return &bus(); };
        addAndMakeVisible(graph);
        refresh();
    }
    void refresh() {   // after undo, recall, load
        auto& b = bus();
        for (int i = 0; i < 5; ++i) typeB[i].setToggleState(int(b.reverb.type) == i, juce::dontSendNotification);
        revOn.setToggleState(b.reverbOn, juce::dontSendNotification); dlyOn.setToggleState(b.delayOn, juce::dontSendNotification);
        for (auto& r : rows) r->slider.setValue(double(r->ref(b)), juce::dontSendNotification);
        graph.repaint();
    }
    void paint(juce::Graphics& g) override { g.fillAll(col::bg); }
    void resized() override {
        auto r = getLocalBounds().reduced(10);
        auto top = r.removeFromTop(30);
        busBox.setBounds(top.removeFromLeft(90).reduced(2));
        for (auto& b : typeB) b.setBounds(top.removeFromLeft(110).reduced(2));
        revOn.setBounds(top.removeFromLeft(100).reduced(2)); dlyOn.setBounds(top.removeFromLeft(100).reduced(2));
        graph.setBounds(r.removeFromRight(r.getWidth() / 3).reduced(6));
        int rowH = 30;
        for (auto& row : rows) { auto line = r.removeFromTop(rowH); row->label.setBounds(line.removeFromLeft(150)); row->slider.setBounds(line); }
    }
private:
    struct Row { juce::Label label; juce::Slider slider; std::function<float&(wm::mixer::Bus&)> ref; };
    wm::mixer::Bus& bus() { return mx.state().buses[size_t(std::min(busBox.getSelectedId() - 1, int(mx.state().buses.size()) - 1))]; }
    void edited() { graph.repaint(); if (onChanged) onChanged(); }
    void add(const char* name, double lo, double hi, double step, std::function<float&(wm::mixer::Bus&)> ref, double skew = 1.0) {
        auto row = std::make_unique<Row>();
        row->ref = ref;
        row->label.setText(name, juce::dontSendNotification); row->label.setColour(juce::Label::textColourId, col::text);
        row->slider.setSliderStyle(juce::Slider::LinearHorizontal); row->slider.setRange(lo, hi, step);
        if (skew != 1.0) row->slider.setSkewFactorFromMidPoint(lo + (hi - lo) / (skew * 2.0 + 0.0));
        row->slider.setColour(juce::Slider::thumbColourId, col::orange);
        auto* s = &row->slider; auto fn = row->ref;
        s->onValueChange = [this, s, fn] { fn(bus()) = float(s->getValue()); edited(); };
        s->setDoubleClickReturnValue(true, lo);
        addAndMakeVisible(row->label); addAndMakeVisible(row->slider);
        rows.push_back(std::move(row));
    }
    wm::mixer::Mixer& mx; std::function<void()> onChanged;
    juce::ComboBox busBox; juce::TextButton typeB[5], revOn, dlyOn;
    std::vector<std::unique_ptr<Row>> rows; DecayGraph graph;
};
}
