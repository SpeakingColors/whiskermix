// Routing matrix: rows = input channels, columns = buses. Click a cell to route, drag vertically to set send gain.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../mixer/MixerModel.h"
#include "../mixer/RoutingMatrix.h"
#include "MixerView.h"

namespace wmui {
class RoutingView : public juce::Component {
public:
    RoutingView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r) : mx(m), rt(r) {}
    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        const int lw = 90, hh = 24, cw = 64, rh = 28;
        auto& st = mx.state();
        g.setFont(13.0f);
        for (size_t b = 0; b < st.buses.size(); ++b) {
            g.setColour(col::text); g.drawText(st.buses[b].name, lw + int(b) * cw, 0, cw, hh, juce::Justification::centred);
        }
        for (size_t c = 0; c < st.channels.size(); ++c) {
            int y = hh + int(c) * rh;
            g.setColour(col::text); g.drawText(st.channels[c].name, 4, y, lw - 8, rh, juce::Justification::centredLeft);
            for (size_t b = 0; b < st.buses.size(); ++b) {
                auto cell = juce::Rectangle<int>(lw + int(b) * cw, y, cw, rh).reduced(3);
                bool on = rt.routed(int(c), int(b));
                g.setColour(on ? col::teal.withAlpha(0.85f) : col::panel); g.fillRoundedRectangle(cell.toFloat(), 4);
                if (on) { g.setColour(col::bg); g.drawText(juce::String(rt.get(int(c), int(b)), 1), cell, juce::Justification::centred); }
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        hit(e.position); startDb = ci >= 0 && rt.routed(ci, bi) ? rt.get(ci, bi) : 0.0f; moved = false;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (ci < 0 || !rt.routed(ci, bi)) return;
        if (std::abs(e.getDistanceFromDragStartY()) > 3) moved = true;
        if (moved) { rt.set(ci, bi, juce::jlimit(-60.0f, 6.0f, startDb - float(e.getDistanceFromDragStartY()) * 0.25f)); repaint(); }
    }
    void mouseUp(const juce::MouseEvent&) override { if (ci >= 0 && !moved) { rt.toggle(ci, bi); repaint(); } }
private:
    void hit(juce::Point<float> p) {
        const int lw = 90, hh = 24, cw = 64, rh = 28;
        ci = int((p.y - hh) / rh); bi = int((p.x - lw) / cw);
        if (p.y < hh || p.x < lw || ci >= int(mx.state().channels.size()) || bi >= int(mx.state().buses.size())) ci = -1;
    }
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; int ci = -1, bi = 0; float startDb = 0; bool moved = false;
};
}
