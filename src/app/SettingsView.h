// Settings page: channel layout (names, icons, colours, stereo links, hide) and preferences.
// Names, icons and colours show everywhere (mixer strips, routing, MIDI hub). Only options the app honours are listed.
#pragma once
#include "../mixer/MixerModel.h"
#include "../mixer/RoutingMatrix.h"
#include "../mixer/Settings.h"
#include "MixerView.h"

namespace wmui {

class LayoutTable : public juce::Component {
public:
    std::function<void()> onChanged;
    LayoutTable(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r) : mx(m), rt(r) {}
    void rebuild() {
        rows.clear(); removeAllChildren();
        for (size_t i = 0; i < mx.state().channels.size(); ++i) {
            auto row = std::make_unique<Row>(); int c = int(i);
            row->name.setTooltip("Channel name");
            row->name.onTextChange = [this, c, r = row.get()] { mx.rename(c, r->name.getText().toStdString()); if (onChanged) onChanged(); };
            for (auto& e : emojiList()) row->emoji.addItem(e.empty() ? juce::String("(none)") : juce::String(juce::CharPointer_UTF8(e.c_str())), row->emoji.getNumItems() + 1);
            row->emoji.onChange = [this, c, r = row.get()] { mx.state().channels[size_t(c)].emoji = emojiList()[size_t(r->emoji.getSelectedId() - 1)]; if (onChanged) onChanged(); };
            for (int k = 0; k < 9; ++k) row->colour.addItem(stripColourName(k), k + 1);
            row->colour.onChange = [this, c, r = row.get()] { mx.state().channels[size_t(c)].colour = r->colour.getSelectedId() - 1; if (onChanged) onChanged(); repaint(); };
            row->stereo.setTooltip("Pair this channel with the next one");
            row->stereo.onClick = [this, c, r = row.get()] { mx.state().channels[size_t(c)].stereoLinked = r->stereo.getToggleState(); if (onChanged) onChanged(); };
            row->hide.setTooltip("Hide this channel strip in the mixer");
            row->hide.onClick = [this, c, r = row.get()] { mx.state().channels[size_t(c)].hidden = r->hide.getToggleState(); if (onChanged) onChanged(); };
            for (auto* x : std::initializer_list<juce::Component*>{&row->name, &row->emoji, &row->colour, &row->stereo, &row->hide}) addAndMakeVisible(x);
            rows.push_back(std::move(row));
        }
        refresh(); setSize(getWidth(), headH + int(rows.size()) * rowH + 8); resized();
    }
    void refresh() {
        for (size_t i = 0; i < rows.size() && i < mx.state().channels.size(); ++i) {
            auto& c = mx.state().channels[i]; auto& r = *rows[i];
            r.name.setText(c.name, false); r.emoji.setSelectedId(emojiIndex(c.emoji) + 1, juce::dontSendNotification);
            r.colour.setSelectedId(c.colour + 1, juce::dontSendNotification);
            r.stereo.setToggleState(c.stereoLinked, juce::dontSendNotification); r.stereo.setEnabled(i + 1 < rows.size());
            r.hide.setToggleState(c.hidden, juce::dontSendNotification);
        }
        repaint();
    }
    // Hide channels that are not routed anywhere.
    void hideUnused() {
        for (int c = 0; c < int(mx.state().channels.size()); ++c) {
            bool used = false; for (int b = 0; b < rt.outputs(); ++b) used = used || rt.routed(c, b);
            mx.state().channels[size_t(c)].hidden = !used;
        }
        refresh(); if (onChanged) onChanged();
    }
    void resetNames() {
        for (size_t i = 0; i < mx.state().channels.size(); ++i) { mx.state().channels[i].name = "In " + std::to_string(i + 1); mx.state().channels[i].emoji.clear(); mx.state().channels[i].colour = 0; }
        refresh(); if (onChanged) onChanged();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(col::dim); g.setFont(12.0f);
        g.drawText("CHANNEL", cX[0], 0, 60, headH, juce::Justification::centredLeft); g.drawText("NAME", cX[1], 0, 100, headH, juce::Justification::centredLeft);
        g.drawText("ICON", cX[2], 0, 80, headH, juce::Justification::centredLeft); g.drawText("COLOUR", cX[3], 0, 80, headH, juce::Justification::centredLeft);
        g.drawText("STEREO", cX[4], 0, 80, headH, juce::Justification::centredLeft); g.drawText("HIDE", cX[5], 0, 80, headH, juce::Justification::centredLeft);
        g.drawText("IN USE", cX[6], 0, 60, headH, juce::Justification::centredLeft);
        for (size_t i = 0; i < rows.size() && i < mx.state().channels.size(); ++i) {
            int y = headH + int(i) * rowH;
            if (i % 2 == 0) { g.setColour(juce::Colour(0xff2b2b32)); g.fillRect(0, y, getWidth(), rowH); }
            g.setColour(col::text); g.setFont(13.0f); g.drawText("In " + juce::String(int(i) + 1), cX[0], y, 60, rowH, juce::Justification::centredLeft);
            bool used = false; for (int b = 0; b < rt.outputs(); ++b) used = used || (int(i) < rt.inputs() && rt.routed(int(i), b));
            g.setColour(used ? col::mint : col::dim); g.fillEllipse(float(cX[6]) + 4, float(y + rowH / 2 - 4), 8, 8);
            g.setColour(stripColour(mx.state().channels[i].colour)); g.fillRoundedRectangle(float(cX[3]) - 8, float(y + 6), 4, float(rowH - 12), 2);
        }
    }
    void resized() override {
        int w = getWidth(); cX[0] = 12; cX[1] = 80; cX[2] = 80 + juce::jmax(180, w / 4) + 12; cX[3] = cX[2] + 100; cX[4] = cX[3] + 130; cX[5] = cX[4] + 90; cX[6] = cX[5] + 90;
        for (size_t i = 0; i < rows.size(); ++i) {
            int y = headH + int(i) * rowH; auto& r = *rows[i];
            r.name.setBounds(cX[1], y + 4, cX[2] - cX[1] - 12, rowH - 8); r.emoji.setBounds(cX[2], y + 4, 84, rowH - 8); r.colour.setBounds(cX[3], y + 4, 110, rowH - 8);
            r.stereo.setBounds(cX[4], y + 4, 40, rowH - 8); r.hide.setBounds(cX[5], y + 4, 40, rowH - 8);
        }
    }
    int neededHeight() const { return headH + int(rows.size()) * rowH + 8; }
private:
    struct Row { juce::TextEditor name; juce::ComboBox emoji, colour; juce::ToggleButton stereo, hide; };
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; std::vector<std::unique_ptr<Row>> rows;
    static constexpr int headH = 26, rowH = 34; int cX[7] = {12, 80, 280, 380, 510, 600, 690};
};

class SettingsView : public juce::Component {
public:
    std::function<void()> onChanged;           // channel layout changed
    std::function<void()> onSettingsChanged;   // preferences changed
    SettingsView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r) : table(m, r) {
        table.onChanged = [this] { if (onChanged) onChanged(); };
        vp.setViewedComponent(&inner, false); vp.setScrollBarsShown(true, false); addAndMakeVisible(vp);
        inner.addAndMakeVisible(table); table.rebuild();
        auto btn = [this](juce::TextButton& b, const char* t, const char* tip) { b.setButtonText(t); b.setTooltip(tip); inner.addAndMakeVisible(b); };
        btn(hideB, "Hide unused", "Hide channels that are not routed to any bus"); hideB.onClick = [this] { table.hideUnused(); };
        btn(resetB, "Reset names", "Back to In 1, In 2, ... and clear icons and colours"); resetB.onClick = [this] { table.resetNames(); };
        auto tog = [this](juce::ToggleButton& t, const char* n, const char* tip, bool wm::mixer::Settings::*f) {
            t.setButtonText(n); t.setTooltip(tip); t.setToggleState(wm::mixer::settings().*f, juce::dontSendNotification);
            t.onClick = [this, &t, f] { wm::mixer::settings().*f = t.getToggleState(); if (onSettingsChanged) onSettingsChanged(); }; inner.addAndMakeVisible(t); };
        tog(tipT, "Info bubbles on hover (after 1 s)", "Show a short explanation when the mouse rests on a control", &wm::mixer::Settings::tooltips);
        tog(dblT, "Double-click resets faders and knobs", "Double-click a fader or knob to put it back to its default", &wm::mixer::Settings::doubleClickReset);
        tog(autoT, "Save the workspace every minute", "Writes autosave.wmix in the app data folder", &wm::mixer::Settings::autosave);
        tog(startT, "Open the autosave at start", "Come back to where you left off", &wm::mixer::Settings::loadAutosaveOnStart);
        tog(grT, "Show gain reduction on strips", "Shows the compressor GR under a strip when its compressor is on", &wm::mixer::Settings::showGrOnStrips);
        hold.setSliderStyle(juce::Slider::LinearHorizontal); hold.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20); hold.setRange(0.0, 10.0, 0.5);
        hold.setTextValueSuffix(" s"); hold.setValue(double(wm::mixer::settings().peakHoldSec), juce::dontSendNotification);
        hold.setColour(juce::Slider::thumbColourId, col::teal); hold.setDoubleClickReturnValue(true, 2.0); hold.setTooltip("How long meter peaks stay. 0 = off");
        hold.onValueChange = [this] { wm::mixer::settings().peakHoldSec = float(hold.getValue()); if (onSettingsChanged) onSettingsChanged(); };
        inner.addAndMakeVisible(hold);
    }
    void rebuild() { table.rebuild(); resized(); }
    void refresh() { table.refresh(); resized(); }
    void syncSettings() {
        auto& s = wm::mixer::settings();
        tipT.setToggleState(s.tooltips, juce::dontSendNotification); dblT.setToggleState(s.doubleClickReset, juce::dontSendNotification);
        autoT.setToggleState(s.autosave, juce::dontSendNotification); startT.setToggleState(s.loadAutosaveOnStart, juce::dontSendNotification);
        grT.setToggleState(s.showGrOnStrips, juce::dontSendNotification); hold.setValue(double(s.peakHoldSec), juce::dontSendNotification);
    }
    void paint(juce::Graphics& g) override { g.fillAll(col::bg); }
    void resized() override {
        vp.setBounds(getLocalBounds());
        int w = juce::jmax(640, vp.getWidth() - vp.getScrollBarThickness());
        table.setSize(w - 24, table.neededHeight()); table.resized();
        inner.setSize(w, 8 + 34 + table.neededHeight() + 8 + 190 + 8);
        inner.layout(w, table, hideB, resetB, tipT, dblT, autoT, startT, grT, hold);
    }
private:
    struct Inner : juce::Component {
        juce::Rectangle<int> c1, c2;
        void paint(juce::Graphics& g) override {
            g.fillAll(col::bg);
            drawCard(g, c1, "Channel layout . names, icons and colours show everywhere", col::title);
            drawCard(g, c2, "Preferences", col::title);
            g.setColour(col::dim); g.setFont(13.0f); g.drawText("Meter peak hold", holdLab, juce::Justification::centredLeft);
        }
        juce::Rectangle<int> holdLab;
        void layout(int w, LayoutTable& t, juce::TextButton& hb, juce::TextButton& rb, juce::ToggleButton& a, juce::ToggleButton& b, juce::ToggleButton& c,
                    juce::ToggleButton& d, juce::ToggleButton& e, juce::Slider& hold) {
            int th = t.getHeight();
            c1 = {8, 8, w - 16, 34 + th + 8}; c2 = {8, c1.getBottom() + 8, w - 16, 190};
            rb.setBounds(c1.getRight() - 12 - 110, c1.getY() + 6, 110, 24); hb.setBounds(rb.getX() - 8 - 110, c1.getY() + 6, 110, 24);
            t.setTopLeftPosition(12, c1.getY() + 34);
            auto r = c2.reduced(14); r.removeFromTop(24);
            int half = r.getWidth() / 2;
            auto L = r.removeFromLeft(half); auto R = r;
            a.setBounds(L.removeFromTop(28)); b.setBounds(L.removeFromTop(28)); c.setBounds(L.removeFromTop(28)); d.setBounds(L.removeFromTop(28));
            e.setBounds(R.removeFromTop(28));
            auto row = R.removeFromTop(28); holdLab = row.removeFromLeft(130); hold.setBounds(row.removeFromLeft(280));
        }
    } inner;
    LayoutTable table; juce::Viewport vp; juce::TextButton hideB, resetB; juce::ToggleButton tipT, dblT, autoT, startT, grT; juce::Slider hold;
};

}  // namespace wmui
