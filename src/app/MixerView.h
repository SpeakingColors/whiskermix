// Mixer window contents (JUCE). NOT compiled yet. Controls write straight into the mixer state
// the engine reads, and meters read the engine back, so every display reacts to its controls.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"

namespace wmui {
namespace col {  // Soda Pop palette
const juce::Colour bg(0xff1b1b1f), panel(0xff26262c), text(0xffe8e8ee), teal(0xff01fcdc), mint(0xff02ebba),
    orange(0xffff9000), yellow(0xffffd100), violet(0xffa43dff), pink(0xffff1383);
}

class Meter : public juce::Component {
public:
    void setDb(float db) { level = juce::jlimit(-60.0f, 6.0f, db); repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(col::bg); g.fillRoundedRectangle(r, 3);
        float h = r.getHeight() * (level + 60.0f) / 66.0f;
        g.setColour(level > 0 ? col::pink : level > -12 ? col::yellow : col::teal);
        g.fillRoundedRectangle(r.removeFromBottom(h), 3);
    }
private:
    float level = -60.0f;
};

class Strip : public juce::Component {
public:
    Strip(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r, int ch, int bus0 = 0)
        : mx(m), rt(r), c(ch), bus(bus0) {
        auto& s = mx.state().channels[size_t(c)];
        name.setText(s.name, juce::dontSendNotification);
        name.setColour(juce::Label::textColourId, col::text); name.setJustificationType(juce::Justification::centred);
        name.setEditable(false, true);
        name.onTextChange = [this] { mx.rename(c, name.getText().toStdString()); };
        fader.setSliderStyle(juce::Slider::LinearVertical); fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        fader.setRange(-60.0, 6.0, 0.1); fader.setSkewFactorFromMidPoint(-12.0);
        fader.setDoubleClickReturnValue(true, 0.0); fader.setValue(s.gainDb, juce::dontSendNotification);
        fader.setTextValueSuffix(" dB"); fader.setColour(juce::Slider::thumbColourId, col::teal);
        fader.onValueChange = [this] { mx.state().channels[size_t(c)].gainDb = float(fader.getValue());
                                       rt.set(c, bus, rt.routed(c, bus) ? rt.get(c, bus) : 0.0f); };
        pan.setSliderStyle(juce::Slider::RotaryVerticalDrag); pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange(-1.0, 1.0, 0.01); pan.setDoubleClickReturnValue(true, 0.0); pan.setValue(s.pan, juce::dontSendNotification);
        pan.setColour(juce::Slider::rotarySliderFillColourId, col::violet);
        pan.onValueChange = [this] { mx.state().channels[size_t(c)].pan = float(pan.getValue()); };
        auto toggle = [this](juce::TextButton& b, const char* t, juce::Colour on, bool wm::mixer::Channel::*f) {
            b.setButtonText(t); b.setClickingTogglesState(true); b.setColour(juce::TextButton::buttonOnColourId, on);
            b.setToggleState(mx.state().channels[size_t(c)].*f, juce::dontSendNotification);
            b.onClick = [this, &b, f] { mx.state().channels[size_t(c)].*f = b.getToggleState(); };
            addAndMakeVisible(b);
        };
        toggle(mute, "M", col::pink, &wm::mixer::Channel::mute);
        toggle(solo, "S", col::yellow, &wm::mixer::Channel::solo);
        toggle(phase, juce::CharPointer_UTF8("\xc3\x98"), col::orange, &wm::mixer::Channel::phaseL);
        for (auto* x : std::initializer_list<juce::Component*>{&name, &fader, &pan, &meter}) addAndMakeVisible(x);
    }
    void setMeter(float db) { meter.setDb(db); }
    void paint(juce::Graphics& g) override { g.setColour(col::panel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2), 6); }
    void resized() override {
        auto r = getLocalBounds().reduced(8);
        name.setBounds(r.removeFromTop(22));
        pan.setBounds(r.removeFromTop(48).withSizeKeepingCentre(44, 44));
        auto b = r.removeFromTop(26); int w = b.getWidth() / 3;
        mute.setBounds(b.removeFromLeft(w).reduced(1)); solo.setBounds(b.removeFromLeft(w).reduced(1)); phase.setBounds(b.reduced(1));
        meter.setBounds(r.removeFromRight(10).reduced(0, 4));
        fader.setBounds(r);
    }
private:
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; int c, bus;
    juce::Label name; juce::Slider fader, pan; juce::TextButton mute, solo, phase; Meter meter;
};

class MixerView : public juce::Component, private juce::Timer {
public:
    MixerView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r, wm::engine::AudioEngine& e) : mx(m), eng(e) {
        for (size_t i = 0; i < m.state().channels.size(); ++i) {
            if (!r.routed(int(i), 0)) r.set(int(i), 0, 0.0f);   // route every channel to bus 1 by default
            strips.push_back(std::make_unique<Strip>(m, r, int(i)));
            addAndMakeVisible(*strips.back());
        }
        main.setSliderStyle(juce::Slider::LinearVertical); main.setRange(-60.0, 6.0, 0.1);
        main.setSkewFactorFromMidPoint(-12.0); main.setDoubleClickReturnValue(true, 0.0); main.setValue(0.0, juce::dontSendNotification);
        main.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18); main.setTextValueSuffix(" dB");
        main.setColour(juce::Slider::thumbColourId, col::orange);
        main.onValueChange = [this] { mx.state().buses[0].volumeDb = float(main.getValue()); };
        dim.setButtonText("DIM"); dim.setClickingTogglesState(true); dim.setColour(juce::TextButton::buttonOnColourId, col::orange);
        dim.onClick = [this] { mx.state().buses[0].dim = dim.getToggleState(); };
        addAndMakeVisible(main); addAndMakeVisible(dim); addAndMakeVisible(mainL); addAndMakeVisible(mainR);
        startTimerHz(30);
    }
    void paint(juce::Graphics& g) override { g.fillAll(col::bg); }
    void resized() override {
        auto r = getLocalBounds().reduced(6);
        auto m = r.removeFromRight(110);
        dim.setBounds(m.removeFromTop(30).reduced(4));
        mainL.setBounds(m.removeFromRight(10).reduced(0, 4)); mainR.setBounds(m.removeFromRight(10).reduced(0, 4));
        main.setBounds(m);
        int w = strips.empty() ? 0 : juce::jmin(110, r.getWidth() / int(strips.size()));
        for (auto& s : strips) s->setBounds(r.removeFromLeft(w));
    }
private:
    void timerCallback() override {   // meters read straight from the engine
        for (size_t i = 0; i < strips.size(); ++i) strips[i]->setMeter(eng.inputPeakDb(int(i)));
        mainL.setDb(eng.outputPeakDb(0)); mainR.setDb(eng.outputPeakDb(1));
    }
    wm::mixer::Mixer& mx; wm::engine::AudioEngine& eng;
    std::vector<std::unique_ptr<Strip>> strips; juce::Slider main; juce::TextButton dim; Meter mainL, mainR;
};
}  // namespace wmui
