// EQ and compressor graphs for the selected channel. NOT compiled with the app yet (syntax-checked only).
// Dragging a handle changes the knob value and the audio; the curve is redrawn from the same parameters.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/AudioEngine.h"
#include "MixerView.h"

namespace wmui {

class EqGraph : public juce::Component {
public:
    std::function<wm::engine::FxParams*()> params;
    void paint(juce::Graphics& g) override {
        auto* p = params ? params() : nullptr;
        auto r = getLocalBounds().toFloat();
        g.setColour(col::bg); g.fillRoundedRectangle(r, 6);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        for (float f : {100.f, 1000.f, 10000.f}) g.drawVerticalLine(int(xOf(f)), 0, r.getBottom());
        for (float d : {-12.f, 0.f, 12.f}) g.drawHorizontalLine(int(yOf(d)), 0, r.getRight());
        if (!p) return;
        juce::Path path;
        for (int x = 0; x < getWidth(); x += 2) {
            float y = yOf(totalDb(*p, fOf(float(x))));
            x == 0 ? path.startNewSubPath(0, y) : path.lineTo(float(x), y);
        }
        g.setColour(p->eqOn ? col::teal : col::teal.withAlpha(0.3f)); g.strokePath(path, juce::PathStrokeType(2.0f));
        auto dot = [&](float f, float gain, juce::Colour c) { g.setColour(c); g.fillEllipse(xOf(f) - 6, yOf(gain) - 6, 12, 12); };
        dot(p->lowFreq, p->lowGain, col::violet); dot(p->midFreq, p->midGain, col::orange); dot(p->highFreq, p->highGain, col::yellow);
        if (p->lowCutHz > 0) dot(p->lowCutHz, 0, col::pink);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        auto* p = params ? params() : nullptr; if (!p) return;
        float best = 1e9f; drag = -1;
        auto test = [&](int id, float f, float gain) {
            float d = std::hypot(e.position.x - xOf(f), e.position.y - yOf(gain)); if (d < best && d < 16) { best = d; drag = id; } };
        test(0, p->lowFreq, p->lowGain); test(1, p->midFreq, p->midGain); test(2, p->highFreq, p->highGain);
        if (p->lowCutHz > 0) test(3, p->lowCutHz, 0);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        auto* p = params ? params() : nullptr; if (!p || drag < 0) return;
        float f = juce::jlimit(20.f, 20000.f, fOf(e.position.x)), gdb = juce::jlimit(-18.f, 18.f, dbOf(e.position.y));
        p->eqOn = true;
        if (drag == 0) { p->lowFreq = f; p->lowGain = gdb; }
        if (drag == 1) { p->midFreq = f; p->midGain = gdb; }
        if (drag == 2) { p->highFreq = f; p->highGain = gdb; }
        if (drag == 3) p->lowCutHz = f;
        repaint(); if (onChange) onChange();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override {   // toggle low cut (default 80 Hz)
        if (auto* p = params ? params() : nullptr) { p->lowCutHz = p->lowCutHz > 0 ? 0.f : 80.f; p->eqOn = true; repaint(); if (onChange) onChange(); }
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {   // wheel = mid Q
        if (auto* p = params ? params() : nullptr) { p->midQ = juce::jlimit(0.2f, 10.f, p->midQ * (1.f + w.deltaY * 0.5f)); repaint(); if (onChange) onChange(); }
    }
    std::function<void()> onChange;
private:
    static float totalDb(const wm::engine::FxParams& p, float f) {
        const float sr = 48000.f;
        float d = wm::engine::Biquad::lowShelf(sr, p.lowFreq, p.lowGain).magDb(sr, f) +
                  wm::engine::Biquad::peak(sr, p.midFreq, p.midGain, p.midQ).magDb(sr, f) +
                  wm::engine::Biquad::highShelf(sr, p.highFreq, p.highGain).magDb(sr, f);
        if (p.lowCutHz > 0) d += wm::engine::Biquad::highPass(sr, p.lowCutHz).magDb(sr, f);
        return d;
    }
    float xOf(float f) const { return float(getWidth()) * std::log(f / 20.f) / std::log(1000.f); }
    float fOf(float x) const { return 20.f * std::exp(x / float(getWidth()) * std::log(1000.f)); }
    float yOf(float db) const { return float(getHeight()) * (0.5f - db / 36.f); }
    float dbOf(float y) const { return (0.5f - y / float(getHeight())) * 36.f; }
    int drag = -1;
};

class CompGraph : public juce::Component {
public:
    std::function<wm::engine::FxParams*()> params;
    std::function<float()> grDb;      // live gain reduction from the engine
    std::function<void()> onChange;
    void paint(juce::Graphics& g) override {
        auto* p = params ? params() : nullptr;
        auto r = getLocalBounds().toFloat();
        g.setColour(col::bg); g.fillRoundedRectangle(r, 6);
        g.setColour(juce::Colours::white.withAlpha(0.1f)); g.drawLine(0, r.getBottom(), r.getRight(), 0);   // 1:1 line
        if (!p) return;
        juce::Path path;
        for (int x = 0; x < getWidth(); x += 2) {
            float y = yOf(wm::engine::compOutDb(*p, inOf(float(x))));
            x == 0 ? path.startNewSubPath(0, y) : path.lineTo(float(x), y);
        }
        g.setColour(p->compOn ? col::orange : col::orange.withAlpha(0.3f)); g.strokePath(path, juce::PathStrokeType(2.0f));
        g.setColour(col::yellow); g.fillEllipse(xOf(p->threshDb) - 6, yOf(p->threshDb) - 6, 12, 12);
        g.setColour(col::pink); g.fillEllipse(xOf(-6) - 6, yOf(wm::engine::compOutDb(*p, -6)) - 6, 12, 12);
        if (grDb) { g.setColour(col::teal); g.drawText(juce::String(grDb(), 1) + " dB", getLocalBounds().removeFromTop(18), juce::Justification::right); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        auto* p = params ? params() : nullptr; if (!p) return;
        float d1 = std::hypot(e.position.x - xOf(p->threshDb), e.position.y - yOf(p->threshDb));
        float d2 = std::hypot(e.position.x - xOf(-6), e.position.y - yOf(wm::engine::compOutDb(*p, -6)));
        drag = d1 < 16 && d1 <= d2 ? 0 : d2 < 16 ? 1 : -1;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        auto* p = params ? params() : nullptr; if (!p || drag < 0) return;
        p->compOn = true;
        if (drag == 0) p->threshDb = juce::jlimit(-60.f, -3.f, inOf(e.position.x));
        if (drag == 1) {   // handle sits at in = -6 dB: its height sets the ratio
            float out = juce::jlimit(p->threshDb + 0.5f, -6.f, dbOfY(e.position.y));
            p->ratio = juce::jlimit(1.f, 20.f, (-6.f - p->threshDb) / std::max(0.25f, out - p->threshDb));
        }
        repaint(); if (onChange) onChange();
    }
private:
    float xOf(float db) const { return float(getWidth()) * (db + 60.f) / 60.f; }
    float inOf(float x) const { return x / float(getWidth()) * 60.f - 60.f; }
    float yOf(float db) const { return float(getHeight()) * (1.f - (db + 60.f) / 60.f); }
    float dbOfY(float y) const { return (1.f - y / float(getHeight())) * 60.f - 60.f; }
    int drag = -1;
};

}  // namespace wmui
