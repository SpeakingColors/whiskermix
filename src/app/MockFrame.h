// The approved mockup's frame, ported natively (no web view): wood cheeks, cat logo, title, page tabs, workspace row,
// "100%" size button. Everything is laid out in the mockup's own pixel coordinates (design size 1082 x 931) and the whole
// window scales with the aspect kept; a click on a wood cheek or the 100% button returns to 100%.
// Numbers come from the mockup page measured in a browser (tools/mockup_measure.py).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AppView.h"

namespace wmui {
namespace mock {
constexpr int kW = 1082, kH = 931, kCheek = 34, kPadX = 20, kContentY = 106;
const juce::Colour outer(0xff0b0b0c), body(0xff2a2c31), dark(0xff1f2125), line(0xff4a4e57), btn(0xff3d4048), ink(0xffeceae4),
    mute(0xffb9b6ae), aqua(0xff01fcdc), orange(0xffff9000), ink2(0xff1e1f23);
}

// Flat button drawn like the mockup's buttons.
class MockButton : public juce::Button {
public:
    juce::Colour fill = mock::dark, edge = mock::line, textCol = mock::ink, fillOn = mock::aqua, textOn = mock::ink2;
    float radius = 5.0f, fontSize = 11.0f; bool bold = false, circle = false;
    MockButton(const juce::String& text = {}) : juce::Button(text) { setClickingTogglesState(false); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f); bool on = getToggleState();
        auto c = on ? fillOn : fill; if (over) c = c.brighter(0.12f); if (down) c = c.darker(0.2f);
        g.setColour(c); if (circle) g.fillEllipse(r); else g.fillRoundedRectangle(r, radius);
        if (!on || edge != mock::line) { g.setColour(on ? c : edge); if (circle) g.drawEllipse(r, 1.0f); else g.drawRoundedRectangle(r, radius, 1.0f); }
        g.setColour(on ? textOn : textCol); g.setFont(juce::Font(fontSize, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
};

class MockFrame : public juce::Component, private juce::Timer {
public:
    std::function<void()> resetSize;   // asks the window for 100%
    explicit MockFrame(AppView& a) : app(a) {
        app.setChromeless(true); addAndMakeVisible(app);
        static const char* names[5] = {"Mixer", "Routing", "MIDI hub", "Pulse 16", "Settings"};
        static const int widths[5] = {61, 73, 80, 76, 76};
        for (int i = 0; i < 5; ++i) {
            auto* b = tabsBtn.add(new MockButton(names[i])); b->fill = juce::Colours::transparentBlack; b->edge = juce::Colours::transparentBlack; b->textCol = mock::mute;
            b->radius = 6.0f; b->fontSize = 12.0f; b->bold = true; tabW[i] = widths[i];
            b->onClick = [this, i] { showPage(i); }; b->setTooltip(juce::String("Open ") + names[i]); addAndMakeVisible(b);
        }
        auto mk = [this](MockButton& b, const juce::String& t, const char* tip, std::function<void()> f) { b.setButtonText(t); b.setTooltip(tip); b.onClick = std::move(f); addAndMakeVisible(b); };
        mk(sizeB, juce::CharPointer_UTF8("\xE2\xA4\xA1 100%"), "UI size: drag any corner or edge to resize (aspect ratio kept); click to reset to 100%", [this] { if (resetSize) resetSize(); });
        sizeB.fill = juce::Colours::transparentBlack; sizeB.edge = juce::Colour(0xff6a6f7a); sizeB.textCol = juce::Colour(0xffcfccc4); sizeB.radius = 15.0f;
        mk(infoB, "i", "What this plugin does", [this] { app.showAbout(); }); mk(gearB, juce::CharPointer_UTF8("\xE2\x9A\x99"), "Settings: MIDI, I/O, controller maps, help", [this] { showPage(4); });
        for (auto* b : {&infoB, &gearB}) { b->circle = true; b->fill = mock::btn; b->fontSize = 14.0f; b->bold = true; }
        mk(wsB, juce::CharPointer_UTF8("\xF0\x9F\x92\xBE Workspace: Studio main \xE2\x96\xBE"), "Pick a saved workspace (snapshot)", [this] { app.showSnapshotMenu(&wsB); });
        wsB.edge = mock::aqua; wsB.radius = 6.0f; wsB.fontSize = 12.0f;
        for (int i = 0; i < 5; ++i) {
            auto* q = quick.add(new MockButton(juce::String(i + 1))); q->setTooltip("Quick workspace " + juce::String(i + 1) + " (click to recall, shift-click to store)");
            q->onClick = [this, i] { app.quickSlot(i, juce::ModifierKeys::currentModifiers.isShiftDown()); refreshQuick(); }; addAndMakeVisible(q);
        }
        mk(saveB, "Save", "Save the workspace to a .wmix file", [this] { app.doSave(); }); mk(loadB, juce::CharPointer_UTF8("Load\xE2\x80\xA6"), "Open a .wmix workspace file", [this] { app.doLoad(); });
        for (auto* b : {&saveB, &loadB}) { b->fill = mock::btn; b->radius = 7.0f; b->bold = true; }
        mk(undoB, juce::CharPointer_UTF8("\xE2\x86\xB6"), "Undo", [this] { app.doUndo(); }); mk(redoB, juce::CharPointer_UTF8("\xE2\x86\xB7"), "Redo", [this] { app.doRedo(); });
        for (auto* b : {&undoB, &redoB}) b->fontSize = 14.0f;
        showPage(0); refreshQuick(); startTimerHz(8);
    }
    void showPage(int i) { page = i; app.showPage(i); for (int k = 0; k < tabsBtn.size(); ++k) tabsBtn[k]->setToggleState(k == i, juce::dontSendNotification); repaint(); }
    void timerCallback() override { refreshQuick(); auto t = juce::String(juce::CharPointer_UTF8("\xF0\x9F\x92\xBE Workspace: ")) + app.workspaceName() + juce::String(juce::CharPointer_UTF8(" \xE2\x96\xBE")); if (t != wsB.getButtonText()) wsB.setButtonText(t); }
    void refreshQuick() { for (int i = 0; i < quick.size(); ++i) quick[i]->setToggleState(i == app.currentSlot(), juce::dontSendNotification); undoB.setEnabled(app.canUndo()); redoB.setEnabled(app.canRedo()); }
    void setPercent(int p) { if (p != pct) { pct = p; sizeB.setButtonText(juce::String(juce::CharPointer_UTF8("\xE2\xA4\xA1 ")) + juce::String(p) + "%"); } }
    void paint(juce::Graphics& g) override {
        using namespace mock;
        g.fillAll(outer);
        // everything below is drawn in design pixels (the frame is scaled by the window)
        g.setColour(body); g.fillRect(kCheek, 0, kW - 2 * kCheek, kH);
        wood(g, 0, true); wood(g, kW - kCheek, false);
        // logo
        juce::Path head; auto P = [](float x, float y) { return juce::Point<float>(54.0f + x * 38.0f / 40.0f, 24.0f + y * 28.0f / 30.0f); };
        head.startNewSubPath(P(5, 26)); for (auto pt : {P(5, 10), P(10, 4), P(14, 10), P(26, 10), P(30, 4), P(35, 10), P(35, 26)}) head.lineTo(pt);
        g.setColour(aqua); g.strokePath(head, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path wh; for (auto seg : {std::array<juce::Point<float>, 2>{P(2, 16), P(14, 18)}, {P(2, 21), P(14, 20)}, {P(38, 16), P(26, 18)}, {P(38, 21), P(26, 20)}}) { wh.startNewSubPath(seg[0]); wh.lineTo(seg[1]); }
        g.setColour(orange); g.strokePath(wh, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // title
        g.setColour(ink); { juce::Font f(19.0f, juce::Font::bold); f.setExtraKerningFactor(0.08f); g.setFont(f); g.drawText("WHISKERMIX", 102, 22, 160, 21, juce::Justification::centredLeft, false); }
        g.setColour(mute); g.setFont(10.0f); g.drawText(juce::CharPointer_UTF8("studio hub \xC2\xB7 mixer + MIDI hub"), 102, 43, 160, 11, juce::Justification::centredLeft, false);
        // nav pill
        g.setColour(dark); g.fillRoundedRectangle(270.0f, 18.0f, 389.0f, 40.0f, 8.0f); g.setColour(line); g.drawRoundedRectangle(270.5f, 18.5f, 388.0f, 39.0f, 8.0f, 1.0f);
    }
    void resized() override {
        using namespace mock;
        int x = 275; for (int i = 0; i < tabsBtn.size(); ++i) { tabsBtn[i]->setBounds(x, 23, tabW[i], 30); x += tabW[i] + 3; }
        sizeB.setBounds(881, 23, 59, 30); infoB.setBounds(958, 22, 32, 32); gearB.setBounds(996, 22, 32, 32);
        wsB.setBounds(54, 64, 187, 28); for (int i = 0; i < 5; ++i) quick[i]->setBounds(244 + 29 * i, 64, 26, 28);
        saveB.setBounds(389, 64, 48, 28); loadB.setBounds(441, 64, 59, 28); undoB.setBounds(502, 64, 30, 28); redoB.setBounds(535, 64, 30, 28);
        app.setBounds(kCheek + kPadX, kContentY, kW - 2 * (kCheek + kPadX), kH - kContentY - 18);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasDraggedSinceMouseDown() && (e.x < mock::kCheek || e.x >= mock::kW - mock::kCheek) && resetSize) resetSize();   // wood cheek click = 100%
    }
    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::NormalCursor; }
private:
    static void wood(juce::Graphics& g, int x, bool left) {
        juce::Path clip; clip.addRoundedRectangle(float(x), 0.0f, float(mock::kCheek), float(mock::kH), 6.0f, 6.0f, left, !left, left, !left);
        juce::Graphics::ScopedSaveState s(g); g.reduceClipRegion(clip);
        static const juce::uint32 col[4] = {0xff5b3a22, 0xff6e4729, 0xff4e3120, 0xff77502f}; static const int len[4] = {3, 4, 2, 5};
        for (int px = 0, k = 0; px < mock::kCheek + 14; ) { g.setColour(juce::Colour(col[k])); g.fillRect(x + px, 0, len[k], mock::kH); px += len[k]; k = (k + 1) % 4; }
        juce::ColourGradient sh(juce::Colours::black.withAlpha(0.6f), left ? float(x + mock::kCheek) : float(x), 0, juce::Colours::transparentBlack, left ? float(x + mock::kCheek - 8) : float(x + 8), 0, false);
        g.setGradientFill(sh); g.fillRect(x, 0, mock::kCheek, mock::kH);
    }
    AppView& app; int page = 0, pct = 100; int tabW[5] = {};
    juce::OwnedArray<MockButton> tabsBtn, quick; MockButton sizeB, infoB, gearB, wsB, saveB, loadB, undoB, redoB;
};

// Window content: holds the frame at its design size and scales it to the window, aspect kept.
class ScaledHost : public juce::Component {
public:
    explicit ScaledHost(AppView& a) : frame(a) { addAndMakeVisible(frame); frame.setSize(mock::kW, mock::kH); }
    void resized() override {
        float s = juce::jmin(float(getWidth()) / mock::kW, float(getHeight()) / mock::kH);
        frame.setTransform(juce::AffineTransform::scale(s).translated((getWidth() - mock::kW * s) / 2, (getHeight() - mock::kH * s) / 2));
        frame.setPercent(int(std::lround(s * 100.0f)));
    }
    void paint(juce::Graphics& g) override { g.fillAll(mock::outer); }
    MockFrame frame;
};
}  // namespace wmui
