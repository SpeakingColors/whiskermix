// Routing page, ported natively from the approved mockup: "Audio device aggregation" panel and the routing matrix
// (inputs -> outputs) with Matrix / Cables views. Geometry measured in the mockup (page area 974 wide).
#pragma once
#include "MixerView.h"
#include "MockKit.h"
#include "../mixer/RoutingMatrix.h"

namespace wmui {

class MockRoutingView : public juce::Component {
public:
    struct Dev { std::string name, detail, status; bool enabled = true; };
    std::function<std::vector<Dev>()> devices;    // real audio devices when the build can list them
    std::function<void()> onPrefs;                // clock / drift changed: save the preferences

    MockRoutingView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r) : mx(m), rt(r) {
        auto seg = [this](MockButton& b, const juce::String& t, const char* tip, std::function<void()> f) { b.setButtonText(t); b.setTooltip(tip); b.fill = mock::dark; b.edge = mock::line; b.textCol = mock::ink; b.fontSize = 11.0f; b.bold = true; b.radius = 4.0f; b.onClick = std::move(f); addAndMakeVisible(b); };
        seg(ckDig, "Digiface", "Master clock: the Digiface USB", [this] { wm::mixer::settings().aggClock = 0; prefs(); });
        seg(ckGpu, "GPU", "Master clock: the GPU HDMI output", [this] { wm::mixer::settings().aggClock = 1; prefs(); });
        seg(ckAuto, "Auto", "Master clock: chosen automatically", [this] { wm::mixer::settings().aggClock = 2; prefs(); });
        seg(drAd, "Adaptive resample", "Resample extra devices to the master clock so they never drift", [this] { wm::mixer::settings().aggDrift = 0; prefs(); });
        seg(drOff, "Off", "No drift correction", [this] { wm::mixer::settings().aggDrift = 1; prefs(); });
        seg(vMatrix, "Matrix", "Routing matrix view", [this] { cables = false; sync(); });
        seg(vCables, "Cables", "Cables view: lines from inputs to outputs", [this] { cables = true; sync(); });
        sync(); refreshDevices();
        setWantsKeyboardFocus(false);
    }
    void refreshDevices() { devs = devices ? devices() : std::vector<Dev>{}; if (devs.empty()) devs.push_back({"Digiface USB", "ADAT, USB", "clock master", true}); resized(); repaint(); }
    void visibilityChanged() override { if (isVisible()) refreshDevices(); }

    void paint(juce::Graphics& g) override {
        auto lab = [&](const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft) { juce::Font f(10.0f); f.setExtraKerningFactor(0.14f); g.setFont(f); g.setColour(mock::mute); g.drawText(t.toUpperCase(), r, j, false); };
        auto panel = [&](juce::Rectangle<int> r) { g.setColour(juce::Colour(0xff34373e)); g.fillRoundedRectangle(r.toFloat(), 8.0f); g.setColour(mock::line); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 8.0f, 1.0f); };
        panel(p1()); panel(p2());
        lab("Audio device aggregation", {11, 10, 300, 14});
        int nin = 0, nout = 0; for (auto& c : mx.state().channels) nin += c.stereoLinked ? 2 : 1; for (auto& b : mx.state().buses) nout += b.stereo ? 2 : 1;
        {   // "Name: Studio Aggregate · 42 in · 44 out", right aligned
            auto f = mx_::f(11.0f); g.setFont(f);
            const juce::String a = "Name: ", b = "Studio Aggregate", c = juce::String(juce::CharPointer_UTF8(" \xC2\xB7 ")) + juce::String(nin) + juce::String(juce::CharPointer_UTF8(" in \xC2\xB7 ")) + juce::String(nout) + " out";
            int wa = int(textWidth(f, a)) + 1, wb = int(textWidth(f, b)) + 1, wc = int(textWidth(f, c)) + 2, x = getWidth() - 11 - wa - wb - wc;
            g.setColour(mock::ink); g.drawText(a, x, 8, wa, 16, juce::Justification::centredLeft, false); g.setColour(mock::aqua); g.drawText(b, x + wa, 8, wb, 16, juce::Justification::centredLeft, false);
            g.setColour(mock::ink); g.drawText(c, x + wa + wb, 8, wc, 16, juce::Justification::centredLeft, false);
        }
        for (size_t i = 0; i < devs.size(); ++i) {
            auto row = juce::Rectangle<int>(11, 36 + int(i) * 40, 952, 30);
            if (row.getBottom() > 280) break;
            g.setColour(juce::Colour(0xff3a3d45)); g.fillRoundedRectangle(row.toFloat(), 4.0f);
            auto box = juce::Rectangle<float>(float(row.getX() + 8), float(row.getY() + 5), 20.0f, 20.0f);
            static const juce::uint32 cs[4] = {0xff02ebba, 0xffc9a8ff, 0xffff9000, 0xff02ebba};
            if (devs[i].enabled) { g.setColour(juce::Colour(cs[i % 4])); g.fillRoundedRectangle(box, 3.0f); } else { g.setColour(mock::mute); g.drawRoundedRectangle(box, 3.0f, 1.5f); }
            g.setColour(devs[i].enabled ? mock::ink : mock::mute); g.setFont(mx_::f(14.0f)); g.drawText(juce::CharPointer_UTF8(devs[i].name.c_str()), row.getX() + 40, row.getY(), 500, 30, juce::Justification::centredLeft, true);
            g.setColour(mock::mute); g.setFont(mx_::f(11.0f)); g.drawText(juce::CharPointer_UTF8(devs[i].detail.c_str()), 560, row.getY(), 280, 30, juce::Justification::centredLeft, true);
            std::string stat = i == 0 ? (devs[i].enabled ? (devs[i].status.empty() ? std::string("clock master") : devs[i].status) : std::string("off")) : (devs[i].enabled ? std::string("resampled") : std::string("off"));
            g.setColour(devs[i].enabled && stat != "off" ? mock::aqua : mock::mute); g.drawText(juce::CharPointer_UTF8(stat.c_str()), 844, row.getY(), 106, 30, juce::Justification::centredLeft, true);
        }
        lab("Clock", {11, 288, 50, 28}); lab("Drift fix", {196 + 14, 288, 70, 28});
        g.setColour(mock::mute); g.setFont(mx_::f(10.0f)); g.drawText("Your DAW sees one big device; extra devices are resampled to the master clock so they never drift.", 11, 316, 800, 14, juce::Justification::centredLeft, false);
        // matrix panel
        auto q = p2(); lab(juce::String(juce::CharPointer_UTF8("Routing matrix \xC2\xB7 inputs \xE2\x86\x92 outputs")), {q.getX() + 11, q.getY() + 12, 400, 14});
        auto& st = mx.state(); int nb = int(st.buses.size()), nc = int(st.channels.size());
        if (cables) { paintCables(g, q); return; }
        const int x0 = q.getX() + 135, pitch = juce::jmin(104, (q.getWidth() - 135 - 20) / juce::jmax(1, nb));
        const int cell = pitch - 4;
        for (int b = 0; b < nb; ++b) {
            g.setColour(mock::mute); g.setFont(mx_::f(10.0f));
            g.drawText(juce::CharPointer_UTF8(st.buses[size_t(b)].name.c_str()), x0 + b * pitch, q.getY() + 54, cell, 14, juce::Justification::centred, true);
        }
        auto vp = matrixView(); g.saveState(); g.reduceClipRegion(vp);
        for (int c = 0; c < nc; ++c) {
            int y = vp.getY() + c * 27 - scrollY; if (y + 24 < vp.getY() || y > vp.getBottom()) continue;
            auto& ch = st.channels[size_t(c)];
            g.setColour(mock::ink); g.setFont(mx_::f(11.0f));
            g.drawText((ch.emoji.empty() ? juce::String() : juce::String(juce::CharPointer_UTF8(ch.emoji.c_str())) + " ") + juce::String(juce::CharPointer_UTF8(ch.name.c_str())), q.getX() + 11, y, 118, 24, juce::Justification::centredLeft, true);
            for (int b = 0; b < nb; ++b) {
                auto r = juce::Rectangle<int>(x0 + b * pitch, y, cell, 24); bool on = rt.routed(c, b);
                juce::Colour cc = st.buses[size_t(b)].controlRoom ? juce::Colour(0xffff9000) : (b == nb - 1 ? juce::Colour(0xffffd100) : juce::Colour(0xff01fcdc));
                g.setColour(on ? cc : juce::Colour(0xff1f2125)); g.fillRoundedRectangle(r.toFloat(), 3.0f);
                if (hoverC == c && hoverB == b && !on) { g.setColour(mock::line); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 3.0f, 1.0f); }
                if (on) { g.setColour(juce::Colour(0xff1e1f23)); g.setFont(mx_::f(10.0f)); g.drawText(juce::String(rt.get(c, b), rt.get(c, b) == std::floor(rt.get(c, b)) ? 0 : 1) + " dB", r, juce::Justification::centred, false); }
            }
        }
        g.restoreState();
        if (maxScroll() > 0) { auto tr = juce::Rectangle<float>(float(q.getRight() - 8), float(vp.getY()), 4.0f, float(vp.getHeight())); g.setColour(juce::Colour(0xff1f2125)); g.fillRoundedRectangle(tr, 2.0f);
            float f = float(vp.getHeight()) / float(nc * 27), th = juce::jmax(20.0f, tr.getHeight() * f); g.setColour(juce::Colour(0xff6a6f7a)); g.fillRoundedRectangle(tr.getX(), tr.getY() + (tr.getHeight() - th) * float(scrollY) / float(maxScroll()), 4.0f, th, 2.0f); }
        g.setColour(mock::mute); g.setFont(mx_::f(10.0f)); g.drawText("Tap to route, drag up/down for level. The last output is the FX return.", q.getX() + 11, q.getBottom() - 18, 700, 14, juce::Justification::centredLeft, false);
    }
    void resized() override {
        int ys = 284; ckDig.setBounds(58, ys, 60, 28); ckGpu.setBounds(118, ys, 41, 28); ckAuto.setBounds(159, ys, 42, 28);
        drAd.setBounds(275, ys, 118, 28); drOff.setBounds(393, ys, 32, 28);
        auto q = p2(); vMatrix.setBounds(q.getRight() - 117, q.getY() + 10, 53, 30); vCables.setBounds(q.getRight() - 64, q.getY() + 10, 53, 30);
        hoverC = hoverB = -1;
    }
    void mouseMove(const juce::MouseEvent& e) override { int c, b; if (cellAt(e.position, c, b) && !cables) { if (c != hoverC || b != hoverB) { hoverC = c; hoverB = b; repaint(); } } else if (hoverC >= 0) { hoverC = hoverB = -1; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hoverC = hoverB = -1; repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        // device rows: toggle the checkbox
        for (size_t i = 0; i < devs.size(); ++i) { auto row = juce::Rectangle<int>(11, 36 + int(i) * 40, 952, 30); if (row.getBottom() <= 280 && row.contains(e.getPosition()) && e.x < 120) { devs[i].enabled = !devs[i].enabled; repaint(); return; } }
        if (cables) return;
        int c, b; ci = -1; if (!cellAt(e.position, c, b)) return; ci = c; bi = b; startDb = rt.routed(c, b) ? rt.get(c, b) : 0.0f; moved = false;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (ci < 0 || !rt.routed(ci, bi)) return;
        if (std::abs(e.getDistanceFromDragStartY()) > 3) moved = true;
        if (moved) { rt.set(ci, bi, juce::jlimit(-60.0f, 6.0f, std::round((startDb - float(e.getDistanceFromDragStartY()) * 0.25f) * 2.0f) / 2.0f)); repaint(); }
    }
    void mouseUp(const juce::MouseEvent&) override { if (ci >= 0 && !moved) { rt.toggle(ci, bi); repaint(); } ci = -1; }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scrollY = juce::jlimit(0, maxScroll(), scrollY - int(w.deltaY * 160.0f)); repaint(); }
    void refresh() { sync(); repaint(); }

private:
    juce::Rectangle<int> p1() const { return {0, 0, getWidth(), 337}; }
    juce::Rectangle<int> p2() const { return {0, 353, getWidth(), juce::jmin(419, getHeight() - 353)}; }
    juce::Rectangle<int> matrixView() const { auto q = p2(); return {q.getX() + 11, q.getY() + 71, q.getWidth() - 22, q.getHeight() - 71 - 24}; }
    int maxScroll() const { return juce::jmax(0, int(mx.state().channels.size()) * 27 - matrixView().getHeight()); }
    bool cellAt(juce::Point<float> p, int& c, int& b) const {
        auto q = p2(); auto vp = matrixView(); int nb = int(mx.state().buses.size());
        const int x0 = q.getX() + 135, pitch = juce::jmin(104, (q.getWidth() - 135 - 20) / juce::jmax(1, nb));
        if (!vp.toFloat().contains(p) || p.x < x0) return false;
        c = int((p.y - float(vp.getY()) + float(scrollY)) / 27.0f); b = int((p.x - float(x0)) / float(pitch));
        return c >= 0 && c < int(mx.state().channels.size()) && b >= 0 && b < nb && std::fmod(p.y - float(vp.getY()) + float(scrollY), 27.0f) < 24.0f && std::fmod(p.x - float(x0), float(pitch)) < float(pitch - 4);
    }
    void paintCables(juce::Graphics& g, juce::Rectangle<int> q) {
        auto& st = mx.state(); int nb = int(st.buses.size()), nc = int(st.channels.size());
        auto vp = matrixView(); float rh = juce::jmin(16.0f, float(vp.getHeight()) / float(juce::jmax(1, juce::jmax(nc, nb)))); const float xl = float(q.getX() + 150), xr = float(q.getRight() - 150);
        g.setFont(mx_::f(9.0f));
        for (int c = 0; c < nc; ++c) { float y = float(vp.getY()) + rh * (float(c) + 0.5f); g.setColour(mock::ink); g.drawText(juce::CharPointer_UTF8(st.channels[size_t(c)].name.c_str()), q.getX() + 11, int(y - 7), 135, 14, juce::Justification::centredRight, true); }
        for (int b = 0; b < nb; ++b) { float y = float(vp.getY()) + rh * (float(b) + 0.5f); g.setColour(mock::ink); g.drawText(juce::CharPointer_UTF8(st.buses[size_t(b)].name.c_str()), int(xr) + 8, int(y - 7), 135, 14, juce::Justification::centredLeft, true); }
        for (int c = 0; c < nc; ++c) for (int b = 0; b < nb; ++b) if (rt.routed(c, b)) {
            float y1 = float(vp.getY()) + rh * (float(c) + 0.5f), y2 = float(vp.getY()) + rh * (float(b) + 0.5f); juce::Path p; p.startNewSubPath(xl, y1); p.cubicTo(xl + 120, y1, xr - 120, y2, xr, y2);
            g.setColour(st.buses[size_t(b)].controlRoom ? juce::Colour(0xffff9000) : juce::Colour(0xff01fcdc)); g.strokePath(p, juce::PathStrokeType(1.5f));
        }
    }
    void prefs() { sync(); if (onPrefs) onPrefs(); }
    void sync() {
        auto& s = wm::mixer::settings(); MockButton* ck[3] = {&ckDig, &ckGpu, &ckAuto};
        for (int i = 0; i < 3; ++i) ck[i]->setToggleState(s.aggClock == i, juce::dontSendNotification);
        drAd.setToggleState(s.aggDrift == 0, juce::dontSendNotification); drOff.setToggleState(s.aggDrift == 1, juce::dontSendNotification);
        vMatrix.setToggleState(!cables, juce::dontSendNotification); vCables.setToggleState(cables, juce::dontSendNotification); repaint();
    }
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; std::vector<Dev> devs; bool cables = false, moved = false; int scrollY = 0, ci = -1, bi = 0, hoverC = -1, hoverB = -1; float startDb = 0;
    MockButton ckDig, ckGpu, ckAuto, drAd, drOff, vMatrix, vCables;
};
}  // namespace wmui
