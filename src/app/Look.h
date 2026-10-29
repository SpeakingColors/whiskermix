// WhiskerMix look: navy panels, neon soda pop controls (same palette as the cat plugin suite), rounded buttons,
// pill tabs, glowing knobs and faders with a dB scale. Pure JUCE, no extra dependencies (keeps offline builds working).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace wmui {

inline juce::Colour stripColour(int i) {
    static const juce::uint32 c[9] = {0xffff7ab0, 0xffff9f5a, 0xffffd75e, 0xff7edc7e, 0xff2fe3c8, 0xff64b5f6, 0xffb98cf0, 0xff5b7be8, 0xffa8a8b0};
    return juce::Colour(c[((i % 9) + 9) % 9]);
}
inline const char* stripColourName(int i) {
    static const char* n[9] = {"pink", "orange", "yellow", "green", "teal", "blue", "violet", "indigo", "grey"};
    return n[((i % 9) + 9) % 9];
}

struct SodaLook : juce::LookAndFeel_V4 {
    juce::Colour bg{0xff1f2125}, panel{0xff34373e}, raised{0xff3d4048}, text{0xffeceae4}, dim{0xffa8a59d}, teal{0xff01fcdc}, orange{0xffff9000}, line{0xff4a4e57};
    SodaLook() {
        setColour(juce::ResizableWindow::backgroundColourId, bg);
        setColour(juce::TextButton::buttonColourId, raised);
        setColour(juce::TextButton::buttonOnColourId, teal);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, bg);
        setColour(juce::Label::textColourId, text);
        setColour(juce::ToggleButton::textColourId, text);
        setColour(juce::ToggleButton::tickColourId, teal);
        setColour(juce::ComboBox::backgroundColourId, bg);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::outlineColourId, raised);
        setColour(juce::ComboBox::arrowColourId, teal);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, teal);
        setColour(juce::PopupMenu::highlightedTextColourId, bg);
        setColour(juce::TextEditor::backgroundColourId, bg);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::outlineColourId, raised);
        setColour(juce::TextEditor::focusedOutlineColourId, teal);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff10304a));
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, teal);
        setColour(juce::ScrollBar::thumbColourId, raised);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        bool on = b.getToggleState();
        auto c = findColour(on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
        if (!b.isEnabled()) c = c.withAlpha(0.35f);
        if (down) c = c.darker(0.2f); else if (over) c = c.brighter(0.15f);
        g.setColour(c); g.fillRoundedRectangle(r, 6.0f);
        if (!on) { g.setColour(line.withAlpha(0.8f)); g.drawRoundedRectangle(r, 6.0f, 1.0f); }
    }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::Font(11.5f, juce::Font::bold); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(12.0f); }
    juce::Font getLabelFont(juce::Label&) override { return juce::Font(12.0f); }
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override {
        auto r = b.getLocalBounds().toFloat();
        float s = juce::jmin(18.0f, r.getHeight() - 4.0f);
        auto box = juce::Rectangle<float>(r.getX() + 2.0f, r.getCentreY() - s / 2, s, s);
        bool on = b.getToggleState();
        g.setColour(on ? findColour(juce::ToggleButton::tickColourId) : bg); g.fillRoundedRectangle(box, 4.0f);
        g.setColour(on ? findColour(juce::ToggleButton::tickColourId) : line.brighter(over ? 0.3f : 0.0f)); g.drawRoundedRectangle(box, 4.0f, 1.0f);
        if (on) {
            juce::Path t; t.startNewSubPath(box.getX() + s * 0.25f, box.getCentreY()); t.lineTo(box.getX() + s * 0.44f, box.getBottom() - s * 0.27f);
            t.lineTo(box.getRight() - s * 0.22f, box.getY() + s * 0.27f);
            g.setColour(bg); g.strokePath(t, juce::PathStrokeType(2.2f));
        }
        g.setColour(b.isEnabled() ? text : dim); g.setFont(11.5f);
        g.drawText(b.getButtonText(), r.withTrimmedLeft(s + 10.0f), juce::Justification::centredLeft);
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(3.0f);
        float d = juce::jmin(r.getWidth(), r.getHeight()), rad = d / 2;
        auto c = r.getCentre();
        auto accent = s.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(bg); g.fillEllipse(c.x - rad, c.y - rad, d, d);
        juce::Path track; track.addCentredArc(c.x, c.y, rad - 2, rad - 2, 0, a0, a1, true);
        g.setColour(raised); g.strokePath(track, juce::PathStrokeType(3.0f));
        float ang = a0 + pos * (a1 - a0);
        // arcs start from the middle for bipolar sliders (pan), from the start otherwise
        float from = s.getMinimum() < 0 && s.getMaximum() > 0 ? a0 + (a1 - a0) * float(s.valueToProportionOfLength(0.0)) : a0;
        juce::Path val; val.addCentredArc(c.x, c.y, rad - 2, rad - 2, 0, juce::jmin(from, ang), juce::jmax(from, ang), true);
        g.setColour(accent); g.strokePath(val, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(text); g.drawLine(c.x + std::sin(ang) * (rad * 0.25f), c.y - std::cos(ang) * (rad * 0.25f),
                                      c.x + std::sin(ang) * (rad - 5), c.y - std::cos(ang) * (rad - 5), 2.0f);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s) override {
        auto accent = s.findColour(juce::Slider::thumbColourId);
        if (st == juce::Slider::LinearVertical) {
            float cx = float(x) + float(w) / 2;
            g.setColour(bg); g.fillRoundedRectangle(cx - 3, float(y), 6, float(h), 3);
            g.setColour(accent.withAlpha(0.55f)); g.fillRoundedRectangle(cx - 3, pos, 6, float(y + h) - pos, 3);
            auto cap = juce::Rectangle<float>(cx - 14, pos - 8, 28, 16);
            g.setColour(juce::Colour(0xffe9e9ee)); g.fillRoundedRectangle(cap, 3);
            g.setColour(accent); g.fillRect(cap.getX() + 2, cap.getCentreY() - 1, cap.getWidth() - 4, 2.0f);
        } else if (st == juce::Slider::LinearHorizontal) {
            float cy = float(y) + float(h) / 2;
            g.setColour(bg); g.fillRoundedRectangle(float(x), cy - 3, float(w), 6, 3);
            g.setColour(accent.withAlpha(0.8f)); g.fillRoundedRectangle(float(x), cy - 3, pos - float(x), 6, 3);
            g.setColour(juce::Colour(0xffe9e9ee)); g.fillEllipse(pos - 7, cy - 7, 14, 14);
        } else LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, st, s);
    }
    // Pill tab bar
    int getTabButtonBestWidth(juce::TabBarButton& b, int) override { return juce::jmax(80, 30 + 9 * b.getButtonText().length()); }
    void drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool) override {
        auto r = b.getLocalBounds().toFloat().reduced(3.0f, 4.0f);
        bool on = b.isFrontTab();
        if (on) { g.setColour(teal); g.fillRoundedRectangle(r, r.getHeight() / 2); }
        else if (over) { g.setColour(raised); g.fillRoundedRectangle(r, r.getHeight() / 2); }
        g.setColour(on ? bg : text); g.setFont(15.0f);
        g.drawText(b.getButtonText(), r, juce::Justification::centred);
    }
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics& g) override { g.fillAll(bg); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb) override {
        auto r = juce::Rectangle<float>(0, 0, float(w), float(h));
        g.setColour(bg); g.fillRoundedRectangle(r, 5); g.setColour(cb.hasKeyboardFocus(true) ? teal : line); g.drawRoundedRectangle(r.reduced(0.5f), 5, 1.0f);
        juce::Path p; float cx = float(w) - 14, cy = float(h) / 2;
        p.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3); g.setColour(teal); g.fillPath(p);
    }
};

// Panel card: rounded navy box with a small caps title
inline void drawCard(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour titleCol = juce::Colour(0xffc7c9ff)) {
    (void)titleCol;
    g.setColour(juce::Colour(0xff34373e)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff4a4e57)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
    if (title.isNotEmpty()) { juce::Font f(10.0f); f.setExtraKerningFactor(0.14f); g.setFont(f); g.setColour(juce::Colour(0xffa8a59d)); g.drawText(title.toUpperCase(), r.getX() + 11, r.getY() + 8, r.getWidth() - 24, 14, juce::Justification::centredLeft); }
    g.setFont(juce::Font(12.0f));   // drop the letter-spaced title font so later text is normal
}

}  // namespace wmui
