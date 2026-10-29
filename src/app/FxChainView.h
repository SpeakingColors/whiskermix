// FX panel of one channel (the mockup's panel that opens beside the channel). Twelve FX: EQ, Dynamics and the ten chain FX
// in engine/Chain.h. RULE: Off = bypass. An FX that is off shows idle meters ("off"), dimmed knobs, and the engine does not
// run it at all. Included by MixerView.h (needs Meter and col::).
#pragma once
#include <deque>
// (Included inside namespace wmui, after Meter and col:: are defined.)
class FxChainPanel : public juce::Component, private juce::Timer {
public:
    static constexpr int kEq = 100, kDyn = 101;   // ids for the two older FX; chain FX use their engine::FxKind value
    std::function<void()> onClose;
    FxChainPanel(wm::mixer::Mixer& m, wm::engine::AudioEngine& e) : mx(m), eng(e) {
        addAndMakeVisible(typeBox); addAndMakeVisible(onBtn); addAndMakeVisible(close); addAndMakeVisible(meterIn); addAndMakeVisible(meterOut);
        int id = 1; for (int k : order()) { typeBox.addItem(nameOf(k), id); ids.push_back(k); ++id; }
        typeBox.setSelectedId(1, juce::dontSendNotification);
        typeBox.onChange = [this] { kind = ids[size_t(typeBox.getSelectedId() - 1)]; rebuildKnobs(); refresh(); };
        onBtn.setClickingTogglesState(false); onBtn.setTooltip("FX on/off for this channel. Off = bypassed: the sound passes untouched and this FX does not run.");
        onBtn.onClick = [this] { setOn(kind, !isOn(kind)); refresh(); };
        close.setButtonText(juce::CharPointer_UTF8("\xC3\x97")); close.setTooltip("Close the FX panel (the chain keeps running)"); close.onClick = [this] { if (onClose) onClose(); };
        for (int i = 0; i < 4; ++i) { knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag); knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16); addAndMakeVisible(knobs[i]); addAndMakeVisible(labels[i]); labels[i].setJustificationType(juce::Justification::centred); labels[i].setColour(juce::Label::textColourId, col::dim); }
        for (size_t i = 0; i < ids.size(); ++i) {   // chain row: lit = on, click selects
            auto* b = chips.add(new juce::TextButton(nameOf(ids[i]))); b->setTooltip("Select " + nameOf(ids[i]) + " (lit = on)");
            b->onClick = [this, i] { typeBox.setSelectedId(int(i) + 1, juce::sendNotification); }; addAndMakeVisible(b);
        }
        rebuildKnobs(); startTimerHz(30);
    }
    void setChannel(int c) { ch = c; rebuildKnobs(); refresh(); }
    // Mockup layout (214 x 366): title, type select + On/Off, display, IN/OUT meters, 2x2 knobs, chain buttons, note.
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff262930)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
        g.setColour(juce::Colour(0xff4a4e57)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6, 1.0f);
        g.setColour(juce::Colour(0xffc0a9fe)); g.setFont(juce::Font(10.0f, juce::Font::bold));
        if (ch >= 0 && ch < int(mx.state().channels.size())) g.drawText(juce::String(juce::CharPointer_UTF8("FX \xC2\xB7 ")) + juce::String(juce::CharPointer_UTF8(mx.state().channels[size_t(ch)].name.c_str())).toUpperCase(), 10, 8, getWidth() - 40, 14, juce::Justification::centredLeft);
        // display: the live output level of this FX over time (flat and dim when it is off)
        auto d = juce::Rectangle<float>(10, 56, 128, 96); g.setColour(juce::Colour(0xff14161a)); g.fillRoundedRectangle(d, 4);
        bool on = isOn(kind); g.setColour(juce::Colour(0xff2c3036)); for (int i = 1; i < 4; ++i) g.drawHorizontalLine(int(d.getY() + d.getHeight() * float(i) / 4), d.getX() + 2, d.getRight() - 2);
        juce::Path p; for (size_t i = 0; i < hist.size(); ++i) { float x = d.getX() + 2 + float(i) * (d.getWidth() - 4) / float(kHist - 1), y = d.getBottom() - 4 - (d.getHeight() - 8) * juce::jlimit(0.0f, 1.0f, (hist[i] + 60.0f) / 60.0f); if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
        g.setColour((on ? juce::Colour(0xff01fcdc) : juce::Colour(0xff4a4e57))); g.strokePath(p, juce::PathStrokeType(1.5f));
        if (!on) { g.setColour(juce::Colour(0xff6a6f7a)); g.setFont(10.0f); g.drawText("off", d, juce::Justification::centred); }
        g.setColour(juce::Colour(0xffb9b6ae)); g.setFont(8.0f);
        g.drawText(inTxt, meterIn.getX() - 6, meterIn.getBottom() + 2, 34, 10, juce::Justification::centred); g.drawText(outTxt, meterOut.getX() - 6, meterOut.getBottom() + 2, 34, 10, juce::Justification::centred);
        g.drawText("IN", meterIn.getX(), meterIn.getY() - 11, 22, 10, juce::Justification::centred); g.drawText("OUT", meterOut.getX(), meterOut.getY() - 11, 22, 10, juce::Justification::centred);
        g.setFont(9.0f); g.drawText(kind == wm::engine::kMidSide ? "M/S works on a stereo strip" : on ? "On: the FX runs" : "Off: bypassed, the sound passes untouched", 10, 338, getWidth() - 20, 12, juce::Justification::centredLeft);
    }
    void resized() override {
        close.setBounds(getWidth() - 26, 5, 18, 18);
        typeBox.setBounds(10, 28, getWidth() - 62, 22); onBtn.setBounds(getWidth() - 46, 28, 37, 22);
        meterIn.setBounds(getWidth() - 70, 66, 22, 72); meterOut.setBounds(getWidth() - 45, 66, 22, 72);
        for (int i = 0; i < 4; ++i) { auto cell = juce::Rectangle<int>(10 + (i % 2) * 96, 158 + (i / 2) * 58, 96, 55); labels[i].setBounds(cell.removeFromBottom(12)); knobs[i].setBounds(cell); }
        for (int i = 0; i < chips.size(); ++i) chips[i]->setBounds(10 + (i % 4) * 50, 278 + (i / 4) * 19, 48, 16);
    }
private:
    static std::vector<int> order() { std::vector<int> v = {kEq, kDyn}; for (int k = 0; k < wm::engine::kFxKinds; ++k) v.push_back(k); return v; }
    static juce::String nameOf(int k) { return k == kEq ? "EQ" : k == kDyn ? "Dynamics" : wm::engine::fxSpec(k).name; }
    wm::mixer::Channel* cur() { return ch >= 0 && ch < int(mx.state().channels.size()) ? &mx.state().channels[size_t(ch)] : nullptr; }
    bool isOn(int k) { auto* c = cur(); if (!c) return false; return k == kEq ? c->fx.eqOn : k == kDyn ? c->fx.compOn : c->chain.s[k].on; }
    void setOn(int k, bool v) { auto* c = cur(); if (!c) return; if (k == kEq) c->fx.eqOn = v; else if (k == kDyn) c->fx.compOn = v; else c->chain.s[k].on = v; }
    // value pointer + range for knob i of the shown FX
    struct KnobRef { float* v; double lo, hi, def; juce::String name, unit; };
    KnobRef knobRef(int i) {
        auto* c = cur(); static float dummy = 0; if (!c) return {&dummy, 0, 1, 0, "", ""};
        if (kind == kEq) { static const char* n[4] = {"Low cut", "Low", "Mid", "High"}; float* v[4] = {&c->fx.lowCutHz, &c->fx.lowGain, &c->fx.midGain, &c->fx.highGain}; double lo[4] = {0, -12, -12, -12}, hi[4] = {400, 12, 12, 12};
            return {v[i], lo[i], hi[i], 0, n[i], i == 0 ? "Hz" : "dB"}; }
        if (kind == kDyn) { static const char* n[4] = {"Thresh", "Ratio", "Attack", "Gain"}; float* v[4] = {&c->fx.threshDb, &c->fx.ratio, &c->fx.attackMs, &c->fx.makeupDb}; double lo[4] = {-48, 1, 1, 0}, hi[4] = {0, 10, 100, 12}, df[4] = {-18, 3, 10, 0}; const char* u[4] = {"dB", ":1", "ms", "dB"};
            return {v[i], lo[i], hi[i], df[i], n[i], u[i]}; }
        const auto& k = wm::engine::fxSpec(kind).k[i]; return {&c->chain.s[kind].p[i], k.lo, k.hi, k.def, k.name, k.unit};
    }
    void rebuildKnobs() {
        for (int i = 0; i < 4; ++i) {
            auto kr = knobRef(i); knobs[i].onValueChange = nullptr;
            knobs[i].setRange(kr.lo, kr.hi, (kr.hi - kr.lo) / 400.0); knobs[i].setDoubleClickReturnValue(true, kr.def);
            knobs[i].setValue(*kr.v, juce::dontSendNotification); knobs[i].setTextValueSuffix(" " + kr.unit); labels[i].setText(kr.name, juce::dontSendNotification);
            knobs[i].onValueChange = [this, i] { auto k = knobRef(i); *k.v = float(knobs[size_t(i)].getValue()); };   // editing never switches the FX on
        }
    }
    void refresh() {
        bool on = isOn(kind); onBtn.setButtonText(on ? "On" : "Off"); onBtn.setColour(juce::TextButton::buttonColourId, on ? col::teal.darker(0.3f) : col::bg);
        for (int i = 0; i < 4; ++i) knobs[i].setAlpha(on ? 1.0f : 0.45f);
        for (int i = 0; i < chips.size(); ++i) { bool o = isOn(ids[size_t(i)]); chips[i]->setColour(juce::TextButton::buttonColourId, o ? col::teal.darker(0.4f) : col::panel); chips[i]->setColour(juce::TextButton::textColourOffId, o ? col::text : col::dim); }
        repaint();
    }
    static float toDb(float lin) { return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -60.0f; }
    void timerCallback() override {
        if (!isShowing() || ch < 0) return;
        bool on = isOn(kind); float dIn = -60, dOut = -60; inTxt = outTxt = "off";
        if (on && kind >= 0 && kind < wm::engine::kFxKinds && kind != wm::engine::kMidSide) { dIn = toDb(eng.fxInPeak(ch, kind)); dOut = toDb(eng.fxOutPeak(ch, kind)); }
        else if (on && kind == kEq) { dIn = dOut = eng.inputPeakDb(ch); }
        else if (on && kind == kDyn) { dIn = eng.inputPeakDb(ch); dOut = dIn + eng.gainReductionDb(ch) + (cur() ? cur()->fx.makeupDb : 0.0f); }
        if (on && !(kind == wm::engine::kMidSide)) { inTxt = juce::String(dIn, 1); outTxt = juce::String(dOut, 1); }
        hist.push_back(on ? dOut : -60.0f); while (int(hist.size()) > kHist) hist.pop_front();
        meterIn.setDb(dIn); meterOut.setDb(dOut); meterIn.setAlpha(on ? 1.0f : 0.3f); meterOut.setAlpha(on ? 1.0f : 0.3f); repaint();
        for (int i = 0; i < chips.size(); ++i) { bool o = isOn(ids[size_t(i)]); chips[i]->setColour(juce::TextButton::buttonColourId, o ? col::teal.darker(0.4f) : col::panel); }
        onBtn.setButtonText(on ? "On" : "Off"); for (int i = 0; i < 4; ++i) knobs[i].setAlpha(on ? 1.0f : 0.45f);
    }
    static constexpr int kHist = 64; std::deque<float> hist;
    wm::mixer::Mixer& mx; wm::engine::AudioEngine& eng; int ch = 0, kind = kEq; std::vector<int> ids;
    juce::ComboBox typeBox; juce::TextButton onBtn, close; Meter meterIn, meterOut; juce::Slider knobs[4]; juce::Label labels[4];
    juce::OwnedArray<juce::TextButton> chips; juce::String inTxt = "off", outTxt = "off";
};
