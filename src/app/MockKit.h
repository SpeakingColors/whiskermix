// Shared pieces of the ported mockup: design-size constants, palette, and the flat mockup button.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace wmui {
namespace mock {
constexpr int kW = 1082, kH = 931, kCheek = 34, kPadX = 20, kContentY = 106;
const juce::Colour outer(0xff0b0b0c), body(0xff2a2c31), dark(0xff1f2125), line(0xff4a4e57), btn(0xff3d4048), ink(0xffeceae4),
    mute(0xffb9b6ae), aqua(0xff01fcdc), orange(0xffff9000), ink2(0xff1e1f23);
}

inline float textWidth(const juce::Font& f, const juce::String& t) { juce::GlyphArrangement ga; ga.addLineOfText(f, t, 0.0f, 0.0f); return ga.getBoundingBox(0, -1, true).getWidth(); }

// Flat button drawn like the mockup's buttons.
class MockButton : public juce::Button {
public:
    juce::Colour fill = mock::dark, edge = mock::line, textCol = mock::ink, fillOn = mock::aqua, textOn = mock::ink2;
    float radius = 5.0f, fontSize = 11.0f; bool bold = false, circle = false;
    std::function<void(juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;   // drawn instead of the text (vector icons, no emoji font needed)
    MockButton(const juce::String& text = {}) : juce::Button(text) { setClickingTogglesState(false); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f); bool on = getToggleState();
        auto c = on ? fillOn : fill; if (over) c = c.brighter(0.12f); if (down) c = c.darker(0.2f);
        g.setColour(c); if (circle) g.fillEllipse(r); else g.fillRoundedRectangle(r, radius);
        if (!on || edge != mock::line) { g.setColour(on ? c : edge); if (circle) g.drawEllipse(r, 1.0f); else g.drawRoundedRectangle(r, radius, 1.0f); }
        if (icon) { icon(g, getLocalBounds().toFloat().reduced(4.0f), on ? textOn : textCol); return; }
        g.setColour(on ? textOn : textCol); g.setFont(juce::Font(fontSize, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
};

}  // namespace wmui
