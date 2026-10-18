// Small shared editor for effect plugins: one slider per parameter plus a live graph supplied by the plugin.
// Moving a slider changes the sound and the graph together (the standing rule: displays react to controls).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace wmplug {
namespace col {
const juce::Colour bg(0xff1b1b1f), panel(0xff26262c), text(0xffe8e8ee), teal(0xff01fcdc), orange(0xffff9000), violet(0xffa43dff);
}

class GraphBox : public juce::Component {
public:
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> draw;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(col::bg); g.fillRoundedRectangle(r, 8);
        if (draw) draw(g, r.reduced(8));
    }
};

class FxEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    struct Spec { juce::String id, label; };
    FxEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts, std::vector<Spec> specs, juce::String title)
        : AudioProcessorEditor(p), title_(std::move(title)) {
        for (auto& s : specs) {
            auto row = std::make_unique<Row>();
            row->label.setText(s.label, juce::dontSendNotification);
            row->label.setColour(juce::Label::textColourId, col::text);
            row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
            row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
            row->slider.setColour(juce::Slider::thumbColourId, col::orange);
            row->slider.setColour(juce::Slider::trackColourId, col::violet);
            row->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, s.id, row->slider);
            addAndMakeVisible(row->label); addAndMakeVisible(row->slider);
            rows_.push_back(std::move(row));
        }
        addAndMakeVisible(graph);
        setSize(720, 80 + int(rows_.size()) * 34 + 240);
        startTimerHz(30);   // redraw the graph as parameters (or automation) change
    }
    GraphBox graph;
    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg); g.setColour(col::teal); g.setFont(juce::FontOptions(22.0f)); g.drawText(title_, 16, 8, getWidth() - 32, 30, juce::Justification::left);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(16); r.removeFromTop(36);
        graph.setBounds(r.removeFromBottom(220));
        r.removeFromBottom(8);
        for (auto& row : rows_) { auto line = r.removeFromTop(34); row->label.setBounds(line.removeFromLeft(150)); row->slider.setBounds(line); }
    }
private:
    void timerCallback() override { graph.repaint(); }
    struct Row {
        juce::Label label; juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    std::vector<std::unique_ptr<Row>> rows_;
    juce::String title_;
};

inline std::unique_ptr<juce::RangedAudioParameter> floatParam(const juce::String& id, const juce::String& name, float lo, float hi, float def, float skewMid = 0) {
    juce::NormalisableRange<float> r(lo, hi);
    if (skewMid > 0) r.setSkewForCentre(skewMid);
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name, r, def);
}
}  // namespace wmplug
