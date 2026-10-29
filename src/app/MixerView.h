// Mixer window contents (JUCE). Controls write straight into the mixer state the engine reads, and meters read the
// engine back, so every display reacts to its controls. Left: channel strips. Right: settings of the selected
// channel (width, M/S, phase, link, FX send, colour, emoji) and the main fader.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "../mixer/Settings.h"
#include "Look.h"

namespace wmui {
namespace col {  // Soda Pop palette
const juce::Colour bg(0xff2a2c31), field(0xff1f2125), panel(0xff34373e), line(0xff4a4e57), text(0xffeceae4), teal(0xff01fcdc), mint(0xff02ebba),
    orange(0xffff9000), yellow(0xffffd100), violet(0xffa43dff), pink(0xffff1383), title(0xffa8a59d), dim(0xffa8a59d);
}

// Emoji choices for channel names (UTF-8 escapes, so every compiler reads them the same way).
inline const std::vector<std::string>& emojiList() {
    static const std::vector<std::string> v = {"", "\xF0\x9F\x8E\xB9", "\xF0\x9F\xA5\x81", "\xF0\x9F\x8E\x9B", "\xF0\x9F\x94\xB7", "\xE2\x9C\xA8", "\xF0\x9F\x8E\xA4",
        "\xF0\x9F\x8C\x8A", "\xF0\x9F\x92\xBF", "\xF0\x9F\x94\x8A", "\xF0\x9F\x8E\xB8", "\xF0\x9F\x8E\xBA", "\xF0\x9F\x90\xB1"};
    return v;
}
inline int emojiIndex(const std::string& e) {
    auto& l = emojiList(); for (size_t i = 0; i < l.size(); ++i) if (l[i] == e) return int(i); return 0;
}

// dB mapping shared by meters and fader tick labels: -60 .. +6 dB
inline float dbFrac(float db) { return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f); }

class Meter : public juce::Component {
public:
    void setDb(float db) {
        level = juce::jlimit(-60.0f, 6.0f, db);
        double now = juce::Time::getMillisecondCounterHiRes();
        float hold = wm::mixer::settings().peakHoldSec;
        if (level >= peak || now - peakAt > double(hold) * 1000.0 || hold <= 0.0f) { peak = level; peakAt = now; }
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(col::bg); g.fillRoundedRectangle(r, 3);
        float h = r.getHeight() * dbFrac(level);
        auto bar = r.withTop(r.getBottom() - h);
        g.setColour(level > 0 ? col::pink : level > -12 ? col::yellow : col::teal);
        g.fillRoundedRectangle(bar, 3);
        if (wm::mixer::settings().peakHoldSec > 0.0f && peak > -59.0f) {
            float y = r.getBottom() - r.getHeight() * dbFrac(peak);
            g.setColour(peak > 0 ? col::pink : col::text); g.fillRect(r.getX(), y, r.getWidth(), 2.0f);
        }
    }
private:
    float level = -60.0f, peak = -60.0f; double peakAt = 0;
};

class Swatches : public juce::Component, public juce::SettableTooltipClient {
public:
    int value = 0; std::function<void(int)> onPick;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); float d = r.getHeight();
        for (int i = 0; i < 9; ++i) {
            auto c = juce::Rectangle<float>(r.getX() + float(i) * (d + 3), r.getY(), d, d).reduced(2);
            g.setColour(stripColour(i)); g.fillEllipse(c);
            if (i == value) { g.setColour(col::text); g.drawEllipse(c.expanded(2), 2.0f); }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        int i = int(e.position.x / float(getHeight() + 3));
        if (i >= 0 && i < 9) { value = i; repaint(); if (onPick) onPick(i); }
    }
};

class Strip : public juce::Component {
public:
    Strip(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r, int ch, int bus0 = 0)
        : mx(m), rt(r), c(ch), bus(bus0) {
        auto& s = mx.state().channels[size_t(c)];
        name.setText(s.name, juce::dontSendNotification);
        name.setColour(juce::Label::textColourId, col::text); name.setJustificationType(juce::Justification::centredLeft);
        name.setEditable(false, true); name.setTooltip("Double-click to rename");
        name.onTextChange = [this] { mx.rename(c, name.getText().toStdString()); };
        fader.setSliderStyle(juce::Slider::LinearVertical); fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        fader.setRange(-60.0, 6.0, 0.1); fader.setSkewFactorFromMidPoint(-12.0);
        fader.setValue(s.gainDb, juce::dontSendNotification);
        fader.setTextValueSuffix(" dB"); fader.setColour(juce::Slider::thumbColourId, col::teal);
        fader.setTooltip("Channel level in dB (double-click resets to 0)");
        fader.onValueChange = [this] { mx.state().channels[size_t(c)].gainDb = float(fader.getValue()); };
        pan.setSliderStyle(juce::Slider::RotaryVerticalDrag); pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange(-1.0, 1.0, 0.01); pan.setValue(s.pan, juce::dontSendNotification);
        pan.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        pan.setColour(juce::Slider::rotarySliderFillColourId, col::violet); pan.setTooltip("Pan (pair: balance)");
        pan.onValueChange = [this] { mx.state().channels[size_t(c)].pan = float(pan.getValue()); };
        auto toggle = [this](juce::TextButton& b, const char* t, const char* tip, juce::Colour on, bool wm::mixer::Channel::*f) {
            b.setButtonText(juce::CharPointer_UTF8(t)); b.setClickingTogglesState(true); b.setColour(juce::TextButton::buttonOnColourId, on);
            b.setToggleState(mx.state().channels[size_t(c)].*f, juce::dontSendNotification); b.setTooltip(tip);
            b.onClick = [this, &b, f] { mx.state().channels[size_t(c)].*f = b.getToggleState(); };
            addAndMakeVisible(b);
        };
        toggle(mute, "M", "Mute", col::pink, &wm::mixer::Channel::mute);
        toggle(solo, "S", "Solo", col::yellow, &wm::mixer::Channel::solo);
        toggle(phase, "\xC3\x98", "Phase invert (left)", col::orange, &wm::mixer::Channel::phaseL);
        setBtn.setButtonText(juce::CharPointer_UTF8("\xF0\x9F\x94\xA7")); setBtn.setTooltip("Channel settings (width, M/S, phase, link, send, colour, icon)");
        fxBtn.setButtonText("FX"); fxBtn.setTooltip("Open this channel's FX panel beside the strip");
        for (auto* b : {&setBtn, &fxBtn}) { b->setClickingTogglesState(false); b->setColour(juce::TextButton::buttonOnColourId, col::pink); addAndMakeVisible(*b); }
        setBtn.onClick = [this] { if (onWrench) onWrench(c); }; fxBtn.onClick = [this] { if (onFx) onFx(c); };
        for (auto* x : std::initializer_list<juce::Component*>{&name, &fader, &pan, &meter}) addAndMakeVisible(x);
        applySettings();
    }
    void setMeter(float db) { meter.setDb(db); }
    void setGr(float db) { if (std::fabs(db - gr) > 0.05f) { gr = db; repaint(); } }
    std::function<void(int)> onSelect, onWrench, onFx;
    bool selected = false;
    void setLit(bool fxOpen, bool setOpen) { fxBtn.setToggleState(fxOpen, juce::dontSendNotification); setBtn.setToggleState(setOpen, juce::dontSendNotification); }
    void mouseDown(const juce::MouseEvent&) override { if (onSelect) onSelect(c); }
    void applySettings() {
        bool d = wm::mixer::settings().doubleClickReset;
        fader.setDoubleClickReturnValue(d, 0.0); pan.setDoubleClickReturnValue(d, 0.0);
    }
    // Re-read all controls from the mixer state (after undo, recall, load or a panel edit).
    void refresh() {
        auto& s = mx.state().channels[size_t(c)];
        name.setText(s.name, juce::dontSendNotification);
        fader.setValue(s.gainDb, juce::dontSendNotification); pan.setValue(s.pan, juce::dontSendNotification);
        mute.setToggleState(s.mute, juce::dontSendNotification); solo.setToggleState(s.solo, juce::dontSendNotification);
        phase.setToggleState(s.phaseL, juce::dontSendNotification);
        applySettings(); repaint();
    }
    void paint(juce::Graphics& g) override {
        auto& s = mx.state().channels[size_t(c)];
        auto b = getLocalBounds().toFloat().reduced(2);
        auto colr = stripColour(s.colour);
        g.setColour(col::panel); g.fillRoundedRectangle(b, 6);
        g.setColour(colr); g.fillRoundedRectangle(b.withHeight(4.0f), 2.0f);
        if (selected) { g.setColour(col::teal); g.drawRoundedRectangle(b, 6, 2); }
        if (!s.emoji.empty()) { g.setColour(col::text); g.setFont(14.0f); g.drawText(juce::CharPointer_UTF8(s.emoji.c_str()), emojiBox, juce::Justification::centred); }
        // dB scale next to the fader and meter
        g.setFont(9.5f);
        for (float db : {6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f}) {
            float y = float(meter.getY()) + float(meter.getHeight()) * (1.0f - dbFrac(db));
            g.setColour(db == 0.0f ? colr : col::dim);
            g.drawText(juce::String(int(db)), scaleBox.getX(), int(y) - 6, scaleBox.getWidth(), 12, juce::Justification::centredRight);
            g.setColour(col::dim.withAlpha(0.4f)); g.fillRect(float(meter.getX()) - 2.0f, y, 2.0f, 1.0f);
        }
        if (wm::mixer::settings().showGrOnStrips && s.fx.compOn) {
            g.setColour(col::orange); g.setFont(10.0f);
            g.drawText("GR " + juce::String(gr, 1), getLocalBounds().withY(getHeight() - 16).withHeight(14), juce::Justification::centred);
        }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(6); r.removeFromTop(4);
        auto top = r.removeFromTop(22); emojiBox = top.removeFromLeft(20); name.setBounds(top);
        pan.setBounds(r.removeFromTop(44).withSizeKeepingCentre(40, 40));
        auto b = r.removeFromTop(26); int w = b.getWidth() / 3;
        mute.setBounds(b.removeFromLeft(w).reduced(1)); solo.setBounds(b.removeFromLeft(w).reduced(1)); phase.setBounds(b.reduced(1));
        { auto rb = r.removeFromBottom(24); setBtn.setBounds(rb.removeFromLeft(rb.getWidth() / 2).reduced(1)); fxBtn.setBounds(rb.reduced(1)); }
        r.removeFromBottom(14);   // room for the GR readout
        scaleBox = r.removeFromLeft(20).withTrimmedBottom(18);
        meter.setBounds(r.removeFromRight(10).withTrimmedBottom(18).reduced(0, 4));
        fader.setBounds(r);
    }
private:
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; int c, bus; float gr = 0;
    juce::Rectangle<int> emojiBox, scaleBox;
    juce::Label name; juce::Slider fader, pan; juce::TextButton mute, solo, phase, setBtn, fxBtn; Meter meter;
};

#include "FxChainView.h"

// Settings of the selected channel.
class StripPanel : public juce::Component {
public:
    std::function<void()> onLook, onLayout, onOpenFx, onClose;   // name, colour or emoji changed / link changed / EQ or dynamics clicked
    StripPanel(wm::mixer::Mixer& m) : mx(m) {
        auto knob = [this](juce::Slider& s, double lo, double hi, double step, double def, juce::Colour c, const char* tip) {
            s.setSliderStyle(juce::Slider::RotaryVerticalDrag); s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
            s.setRange(lo, hi, step); s.setDoubleClickReturnValue(true, def); s.setColour(juce::Slider::rotarySliderFillColourId, c);
            s.setTooltip(tip); addAndMakeVisible(s); };
        knob(width, 0.0, 2.0, 0.01, 1.0, col::teal, "Stereo width of a linked pair: 0 = mono, 1 = unchanged, 2 = double");
        knob(send, -60.0, 0.0, 0.5, -60.0, col::violet, "Send to the FX bus (last bus). -60 = off");
        width.setTextValueSuffix(" x"); send.setTextValueSuffix(" dB");
        width.onValueChange = [this] { cur().width = float(width.getValue()); };
        send.onValueChange = [this] { cur().fxSendDb = float(send.getValue()); };
        auto tog = [this](juce::TextButton& b, const char* t, const char* tip, juce::Colour on, std::function<void(bool)> f) {
            b.setButtonText(juce::CharPointer_UTF8(t)); b.setClickingTogglesState(true); b.setColour(juce::TextButton::buttonOnColourId, on);
            b.setTooltip(tip); b.onClick = [&b, f] { f(b.getToggleState()); }; addAndMakeVisible(b); };
        tog(ms, "M/S", "Input pair is Mid/Side: decode to left/right (linked pair only)", col::orange, [this](bool v) { cur().midSide = v; });
        tog(phL, "\xC3\x98L", "Invert phase of the left channel", col::orange, [this](bool v) { cur().phaseL = v; if (onLook) onLook(); });
        tog(phR, "\xC3\x98R", "Invert phase of the right channel (linked pair)", col::orange, [this](bool v) { cur().phaseR = v; });
        tog(link, "Link with next", "Make this channel and the next one a stereo pair", col::mint, [this](bool v) { cur().stereoLinked = v; if (onLayout) onLayout(); });
        tog(eq, "EQ", "EQ on/off for this channel (Off = bypassed, the sound passes untouched); also opens the EQ page", col::teal, [this](bool v) { cur().fx.eqOn = v; if (onOpenFx) onOpenFx(); });
        tog(dyn, "Dynamics", "Dynamics on/off for this channel (Off = bypassed, the sound passes untouched); also opens the compressor page", col::violet, [this](bool v) { cur().fx.compOn = v; if (onOpenFx) onOpenFx(); });
        closeBtn.setButtonText(juce::CharPointer_UTF8("\xC3\x97")); closeBtn.setTooltip("Close this channel's settings (the channel stays as it is)");
        closeBtn.onClick = [this] { if (onClose) onClose(); }; addAndMakeVisible(closeBtn);
        sw.onPick = [this](int i) { cur().colour = i; if (onLook) onLook(); };
        sw.setTooltip("Strip colour (also used in the MIDI hub)"); addAndMakeVisible(sw);
        for (auto& e : emojiList()) emoji.addItem(e.empty() ? juce::String("(no icon)") : juce::String(juce::CharPointer_UTF8(e.c_str())), emoji.getNumItems() + 1);
        emoji.setTooltip("Icon shown next to the channel name"); addAndMakeVisible(emoji);
        emoji.onChange = [this] { cur().emoji = emojiList()[size_t(emoji.getSelectedId() - 1)]; if (onLook) onLook(); };
    }
    void setChannel(int c) { ch = c; sync(); repaint(); }
    void sync() {
        if (ch < 0 || ch >= int(mx.state().channels.size())) return;
        auto& s = cur();
        width.setValue(s.width, juce::dontSendNotification); send.setValue(s.fxSendDb, juce::dontSendNotification);
        ms.setToggleState(s.midSide, juce::dontSendNotification); phL.setToggleState(s.phaseL, juce::dontSendNotification);
        phR.setToggleState(s.phaseR, juce::dontSendNotification); link.setToggleState(s.stereoLinked, juce::dontSendNotification);
        link.setEnabled(ch + 1 < int(mx.state().channels.size()));
        eq.setToggleState(s.fx.eqOn, juce::dontSendNotification); dyn.setToggleState(s.fx.compOn, juce::dontSendNotification);
        sw.value = s.colour; sw.repaint();
        emoji.setSelectedId(emojiIndex(s.emoji) + 1, juce::dontSendNotification);
        bool pair = s.stereoLinked;
        width.setEnabled(pair); ms.setEnabled(pair); phR.setEnabled(pair);
    }
    void paint(juce::Graphics& g) override {
        drawCard(g, getLocalBounds(), "Channel settings", col::title);
        if (ch >= 0 && ch < int(mx.state().channels.size())) {
            auto& s = mx.state().channels[size_t(ch)];
            g.setColour(stripColour(s.colour)); g.setFont(15.0f);
            g.drawText(juce::String(s.name), 12, 24, getWidth() - 24, 20, juce::Justification::centredLeft);
        }
        g.setColour(col::dim); g.setFont(11.0f);
        g.drawText("WIDTH", knobL, juce::Justification::centredBottom); g.drawText("FX SEND", knobR, juce::Justification::centredBottom);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(10); closeBtn.setBounds(getWidth() - 34, 6, 24, 22); r.removeFromTop(38);
        auto k = r.removeFromTop(84);
        knobL = k.removeFromLeft(k.getWidth() / 2).withTrimmedBottom(0); knobR = k;
        width.setBounds(knobL.reduced(4).withTrimmedBottom(14)); send.setBounds(knobR.reduced(4).withTrimmedBottom(14));
        auto row = [&r](int h) { auto x = r.removeFromTop(h); r.removeFromTop(4); return x; };
        auto a = row(26); int w = a.getWidth() / 3; ms.setBounds(a.removeFromLeft(w).reduced(1)); phL.setBounds(a.removeFromLeft(w).reduced(1)); phR.setBounds(a.reduced(1));
        link.setBounds(row(26).reduced(1));
        auto b = row(26); w = b.getWidth() / 2; eq.setBounds(b.removeFromLeft(w).reduced(1)); dyn.setBounds(b.reduced(1));
        sw.setBounds(row(24)); emoji.setBounds(row(26));
    }
private:
    wm::mixer::Channel& cur() { return mx.state().channels[size_t(ch)]; }
    wm::mixer::Mixer& mx; int ch = 0; juce::Rectangle<int> knobL, knobR;
    juce::Slider width, send; juce::TextButton ms, phL, phR, link, eq, dyn, closeBtn; Swatches sw; juce::ComboBox emoji;
};

class MixerView : public juce::Component, private juce::Timer {
public:
    MixerView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r, wm::engine::AudioEngine& e) : mx(m), rt(r), eng(e), panel(m), fxPanel(m, e) {
        rebuild();
        main.setSliderStyle(juce::Slider::LinearVertical); main.setRange(-60.0, 6.0, 0.1);
        main.setSkewFactorFromMidPoint(-12.0); main.setValue(0.0, juce::dontSendNotification);
        main.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18); main.setTextValueSuffix(" dB");
        main.setColour(juce::Slider::thumbColourId, col::orange); main.setTooltip("Main output level in dB (double-click resets to 0)");
        main.onValueChange = [this] { mx.state().buses[0].volumeDb = float(main.getValue()); };
        dim.setButtonText("DIM"); dim.setClickingTogglesState(true); dim.setColour(juce::TextButton::buttonOnColourId, col::orange);
        dim.setTooltip("Lower the main output by 20 dB"); dim.onClick = [this] { mx.state().buses[0].dim = dim.getToggleState(); };
        addAndMakeVisible(main); addAndMakeVisible(dim); addAndMakeVisible(mainL); addAndMakeVisible(mainR); addAndMakeVisible(panel);
        panel.onLook = [this] { for (auto& s : strips) s->refresh(); panel.repaint(); };
        panel.onLayout = [this] { };
        panel.onOpenFx = [this] { if (onOpenFx) onOpenFx(); };
        panel.onClose = [this] { setPanelOpen(false); };
        addChildComponent(fxPanel); fxPanel.onClose = [this] { setFxOpen(false); };   // FX panel starts closed
        panel.setVisible(false);          // every channel starts closed, like TotalMix
        pick(0);
        refreshAll();
        startTimerHz(30);
    }
    // Create or remove strips so they match the mixer's channel count (after add, undo, recall or load).
    void rebuild() {
        while (strips.size() > mx.state().channels.size()) strips.pop_back();
        while (strips.size() < mx.state().channels.size()) {
            int i = int(strips.size());
            if (i < rt.inputs() && rt.outputs() > 0 && !rt.routed(i, 0)) rt.set(i, 0, 0.0f);   // new channels go to bus 1 by default
            strips.push_back(std::make_unique<Strip>(mx, rt, i));
            strips.back()->onSelect = [this](int c) { select(c); };
            strips.back()->onWrench = [this](int c) { bool same = c == selectedChannel; select(c); setPanelOpen(!(same && panelOpen_)); };   // wrench: open or close this channel's settings
            strips.back()->onFx = [this](int c) { bool same = c == selectedChannel; select(c); setFxOpen(!(same && fxOpen_)); };            // FX: open or close this channel's FX panel
            addAndMakeVisible(*strips.back());
        }
        if (selectedChannel >= int(strips.size())) selectedChannel = int(strips.size()) - 1;
        resized();
    }
    int selectedChannel = 0;
    std::function<void(int)> onSelect;
    std::function<void()> onOpenFx;
    bool fxOpen() const { return fxOpen_; }
    void setFxOpen(bool o) { fxOpen_ = o; fxPanel.setVisible(o); if (o) fxPanel.setChannel(selectedChannel); updateLit(); resized(); repaint(); }
    void updateLit() { for (size_t i = 0; i < strips.size(); ++i) strips[i]->setLit(fxOpen_ && int(i) == selectedChannel, panelOpen_ && int(i) == selectedChannel); }
    bool panelOpen() const { return panelOpen_; }
    void setPanelOpen(bool o) { panelOpen_ = o; panel.setVisible(o); updateLit(); resized(); repaint(); }
    void select(int c) { pick(c); }
    void pick(int c) {
        selectedChannel = c;
        for (size_t i = 0; i < strips.size(); ++i) { strips[i]->selected = int(i) == c; strips[i]->repaint(); }
        panel.setChannel(c); if (fxOpen_) fxPanel.setChannel(c);
        updateLit();
        if (onSelect) onSelect(c);
    }
    void refreshAll() {
        rebuild();
        for (auto& s : strips) s->refresh();
        main.setValue(mx.state().buses[0].volumeDb, juce::dontSendNotification);
        main.setDoubleClickReturnValue(wm::mixer::settings().doubleClickReset, 0.0);
        dim.setToggleState(mx.state().buses[0].dim, juce::dontSendNotification);
        panel.setChannel(selectedChannel);
        resized();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        g.setColour(col::panel); g.fillRoundedRectangle(mainCard.toFloat(), 8.0f);
        g.setFont(9.5f);
        for (float db : {6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f}) {
            float y = float(mainL.getY()) + float(mainL.getHeight()) * (1.0f - dbFrac(db));
            g.setColour(db == 0.0f ? col::orange : col::dim);
            g.drawText(juce::String(int(db)), mainL.getX() - 26, int(y) - 6, 22, 12, juce::Justification::centredRight);
        }
        g.setColour(col::title); g.setFont(12.5f); g.drawText("MAIN", mainCard.getX() + 12, mainCard.getY() + 6, 80, 18, juce::Justification::centredLeft);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(6);
        auto side = r.removeFromRight(230); r.removeFromRight(6);
        if (panelOpen_) { panel.setBounds(side.removeFromTop(268)); side.removeFromTop(6); }
        mainCard = side;
        auto m = side.reduced(8); m.removeFromTop(22);
        dim.setBounds(m.removeFromTop(28).removeFromLeft(80)); m.removeFromTop(4);
        auto meters = m.removeFromRight(30).withTrimmedBottom(22).reduced(0, 4);
        mainL.setBounds(meters.removeFromLeft(12)); mainR.setBounds(meters.removeFromRight(12));
        main.setBounds(m);
        std::vector<Strip*> vis;
        for (size_t i = 0; i < strips.size(); ++i) {
            bool h = mx.state().channels[i].hidden;
            strips[i]->setVisible(!h);
            if (!h) vis.push_back(strips[i].get());
        }
        const int fxW = fxOpen_ ? 300 : 0;   // the FX panel opens right after the selected strip
        int w = vis.empty() ? 0 : juce::jmin(112, (r.getWidth() - fxW) / int(vis.size()));
        for (size_t i = 0; i < strips.size(); ++i) {
            if (mx.state().channels[i].hidden) continue;
            strips[i]->setBounds(r.removeFromLeft(w));
            if (fxOpen_ && int(i) == selectedChannel) fxPanel.setBounds(r.removeFromLeft(fxW).reduced(2));
        }
    }
private:
    void timerCallback() override {   // meters read straight from the engine
        for (size_t i = 0; i < strips.size(); ++i) { strips[i]->setMeter(eng.inputPeakDb(int(i))); strips[i]->setGr(eng.gainReductionDb(int(i))); }
        mainL.setDb(eng.outputPeakDb(0)); mainR.setDb(eng.outputPeakDb(1));
    }
    bool panelOpen_ = false, fxOpen_ = false;
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; wm::engine::AudioEngine& eng;
    std::vector<std::unique_ptr<Strip>> strips; StripPanel panel; FxChainPanel fxPanel; juce::Slider main; juce::TextButton dim; Meter mainL, mainR;
    juce::Rectangle<int> mainCard;
};
}  // namespace wmui
