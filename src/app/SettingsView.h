// Settings page, as in the approved mockup: channel layout table, then option cards (Mixer, Level meters, Mixer views,
// Operation, Snapshots and workspaces, Devices, Remote control). Names, icons and colours show everywhere.
// Options the app honours are wired (peak hold, gain reduction, double-click reset, autosave, tooltips, dim amount);
// the rest are stored in settings.json under "x_<name>" so they can be backed later.
#pragma once
#include "../mixer/MixerModel.h"
#include "../mixer/RoutingMatrix.h"
#include "../mixer/Settings.h"
#include "MixerView.h"
#include "MockKit.h"

namespace wmui {

class LayoutTable : public juce::Component {
public:
    std::function<void()> onChanged;
    int mode = 0;   // 0 inputs, 1 playbacks, 2 outputs
    LayoutTable(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r) : mx(m), rt(r) {}
    std::vector<int> indices() const {
        std::vector<int> v;
        if (mode == 2) { for (size_t i = 0; i < mx.state().buses.size(); ++i) v.push_back(int(i)); }
        else for (size_t i = 0; i < mx.state().channels.size(); ++i) if (mx.state().channels[i].playback == (mode == 1)) v.push_back(int(i));
        return v;
    }
    void rebuild() {
        rows.clear(); removeAllChildren(); idx = indices();
        for (int c : idx) {
            auto row = std::make_unique<Row>();
            row->name.setTooltip("Name");
            row->name.onTextChange = [this, c, r = row.get()] { if (mode == 2) mx.state().buses[size_t(c)].name = r->name.getText().toStdString(); else mx.rename(c, r->name.getText().toStdString()); if (onChanged) onChanged(); };
            for (auto& e : emojiList()) row->emoji.addItem(e.empty() ? juce::String("(none)") : juce::String(juce::CharPointer_UTF8(e.c_str())), row->emoji.getNumItems() + 1);
            row->emoji.onChange = [this, c, r = row.get()] { mx.state().channels[size_t(c)].emoji = emojiList()[size_t(r->emoji.getSelectedId() - 1)]; if (onChanged) onChanged(); };
            for (int k = 0; k < 9; ++k) row->colour.addItem(stripColourName(k), k + 1);
            row->colour.onChange = [this, c, r = row.get()] { mx.state().channels[size_t(c)].colour = r->colour.getSelectedId() - 1; if (onChanged) onChanged(); repaint(); };
            row->stereo.setTooltip("Stereo pair (two hardware channels, one strip)");
            row->stereo.onClick = [this, c, r = row.get()] { mx.state().channels[size_t(c)].stereoLinked = r->stereo.getToggleState(); if (onChanged) onChanged(); };
            row->hide.setTooltip("Hide this channel strip in the mixer");
            row->hide.onClick = [this, c, r = row.get()] { mx.state().channels[size_t(c)].hidden = r->hide.getToggleState(); if (onChanged) onChanged(); };
            addAndMakeVisible(row->name);
            if (mode != 2) for (auto* x : std::initializer_list<juce::Component*>{&row->emoji, &row->colour, &row->stereo, &row->hide}) addAndMakeVisible(x);
            rows.push_back(std::move(row));
        }
        refresh(); setSize(getWidth(), neededHeight()); resized();
    }
    void refresh() {
        for (size_t i = 0; i < rows.size() && i < idx.size(); ++i) {
            auto& r = *rows[i]; const int c = idx[i];
            if (mode == 2) { r.name.setText(mx.state().buses[size_t(c)].name, false); continue; }
            auto& ch = mx.state().channels[size_t(c)];
            r.name.setText(ch.name, false); r.emoji.setSelectedId(emojiIndex(ch.emoji) + 1, juce::dontSendNotification);
            r.colour.setSelectedId(ch.colour + 1, juce::dontSendNotification);
            r.stereo.setToggleState(ch.stereoLinked, juce::dontSendNotification);
            r.hide.setToggleState(ch.hidden, juce::dontSendNotification);
        }
        repaint();
    }
    void hideUnused() {
        for (int c : idx) {
            if (mode == 2) continue;
            bool used = false; for (int b = 0; b < rt.outputs(); ++b) used = used || rt.routed(c, b);
            mx.state().channels[size_t(c)].hidden = !used;
        }
        refresh(); if (onChanged) onChanged();
    }
    void resetNames() {
        for (int c : idx) { if (mode == 2) continue; auto& ch = mx.state().channels[size_t(c)]; ch.name = "In " + std::to_string(c + 1); ch.emoji.clear(); ch.colour = 0; }
        refresh(); if (onChanged) onChanged();
    }
    juce::String label(int c) const {
        if (mode == 2) { auto& b = mx.state().buses[size_t(c)]; return (b.controlRoom ? "CR " : "OUT ") + juce::String(c + 1); }
        auto& ch = mx.state().channels[size_t(c)];
        if (ch.hwFirst < 0) return "In " + juce::String(c + 1);
        if (ch.playback && ch.hwFirst >= 32) return "PH";
        juce::String s = juce::String(ch.playback ? "PB " : "ADAT ") + juce::String(ch.hwFirst + 1);
        return ch.stereoLinked ? s + "/" + juce::String(ch.hwFirst + 2) : s;
    }
    void paint(juce::Graphics& g) override {
        using namespace mock;
        g.setColour(mute); g.setFont(juce::Font(9.5f));
        auto head = [&](const char* t, int x) { g.drawText(t, x, 0, 100, headH, juce::Justification::centredLeft, false); };
        head("Channel", cX[0]); head("Name", cX[1]);
        if (mode != 2) { head("Icon", cX[2]); head("Colour", cX[3]); head("Stereo", cX[4]); head("Hide in mixer", cX[5]); head("In use", cX[6]); }
        for (size_t i = 0; i < rows.size() && i < idx.size(); ++i) {
            int y = headH + int(i) * rowH, c = idx[i];
            if (i % 2 == 0) { g.setColour(juce::Colour(0xff3a3d45)); g.fillRect(0, y, getWidth(), rowH); }
            g.setColour(ink); g.setFont(juce::Font(10.5f)); g.drawText(label(c), cX[0], y, 70, rowH, juce::Justification::centredLeft, false);
            if (mode == 2) continue;
            bool used = false; for (int b = 0; b < rt.outputs(); ++b) used = used || (c < rt.inputs() && rt.routed(c, b));
            g.setColour(used ? aqua : juce::Colour(0xff6a6f7a)); g.fillEllipse(float(cX[6]) + 2, float(y + rowH / 2 - 3), 6, 6);
            g.setColour(stripColour(mx.state().channels[size_t(c)].colour)); g.fillEllipse(float(cX[3]) - 14, float(y + rowH / 2 - 5), 10, 10);
        }
    }
    void resized() override {
        int w = getWidth(); cX[0] = 8; cX[1] = 90; const int nameW = juce::jmax(130, w * 24 / 100);
        cX[2] = cX[1] + nameW + 12; cX[3] = cX[2] + 100; cX[4] = cX[3] + 116; cX[5] = cX[4] + 110; cX[6] = cX[5] + 120;
        for (size_t i = 0; i < rows.size(); ++i) {
            int y = headH + int(i) * rowH; auto& r = *rows[i];
            r.name.setBounds(cX[1], y + 2, nameW, rowH - 4); r.emoji.setBounds(cX[2], y + 2, 62, rowH - 4); r.colour.setBounds(cX[3], y + 2, 90, rowH - 4);
            r.stereo.setBounds(cX[4] + 4, y + 2, 22, rowH - 4); r.hide.setBounds(cX[5] + 4, y + 2, 22, rowH - 4);
        }
    }
    int neededHeight() const { return headH + int(rows.size()) * rowH + 6; }
private:
    struct Row { juce::TextEditor name; juce::ComboBox emoji, colour; juce::ToggleButton stereo, hide; };
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; std::vector<std::unique_ptr<Row>> rows; std::vector<int> idx;
    static constexpr int headH = 20, rowH = 24; int cX[7] = {8, 90, 280, 380, 496, 606, 726};
};

// One option row of an option card: label on the left, control on the right.
struct OptSpec {
    enum Kind { Toggle, Seg, Slider, Drop, Note } kind;
    juce::String label, key; float def = 0; juce::StringArray opts; float lo = 0, hi = 1, step = 1; juce::String suffix, hint, tip;
    std::function<float()> get; std::function<void(float)> set;   // set for options the app really honours; otherwise stored under x_<key>
};

class SettingsView : public juce::Component {
public:
    std::function<void()> onChanged;           // channel layout changed
    std::function<void()> onSettingsChanged;   // preferences changed
    SettingsView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r) : mx(m), table(m, r) {
        table.onChanged = [this] { if (onChanged) onChanged(); };
        vp.setViewedComponent(&inner, false); vp.setScrollBarsShown(true, false); addAndMakeVisible(vp);
        inner.addAndMakeVisible(table); table.rebuild();
        auto mk = [this](MockButton& b, const char* t, const char* tip, std::function<void()> f, juce::Component& parent) { b.setButtonText(t); b.setTooltip(tip); b.onClick = std::move(f); b.bold = true; b.radius = 6.0f; b.fill = mock::btn; parent.addAndMakeVisible(b); };
        mk(hideB, "Hide unused", "Hide channels that are not routed to any bus", [this] { table.hideUnused(); }, inner);
        mk(resetB, "Reset names", "Back to In 1, In 2, ... and clear icons and colours", [this] { table.resetNames(); }, inner);
        mk(exportB, "Export settings...", "Save the settings to a file", [this] { exportSettings(); }, *this);
        mk(importB, "Import...", "Load settings from a file", [this] { importSettings(); }, *this);
        const char* st[3] = {"Inputs", "Playbacks", "Outputs"};
        for (int i = 0; i < 3; ++i) { auto* b = modeB.add(new MockButton(st[i])); b->bold = true; b->radius = 5.0f; b->onClick = [this, i] { setMode(i); }; inner.addAndMakeVisible(b); }
        modeB[0]->setToggleState(true, juce::dontSendNotification);
        const char* sn[5] = {"Channel layout", "Mixer", "Preferences", "Devices", "Remote"};
        for (int i = 0; i < 5; ++i) { auto* b = subB.add(new MockButton(sn[i])); b->bold = true; b->radius = 5.0f; b->fill = juce::Colours::transparentBlack; b->edge = juce::Colours::transparentBlack; b->textCol = mock::mute; b->onClick = [this, i] { gotoSection(i); }; addAndMakeVisible(b); }
        subB[0]->setToggleState(true, juce::dontSendNotification);
        buildCards();
    }
    void rebuild() { table.rebuild(); rebuildDrops(); resized(); }
    void refresh() { table.refresh(); resized(); }
    void syncSettings() { for (auto& c : cards) for (auto& r : c.rows) r->sync(); }
    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        g.setColour(mock::dark); g.fillRoundedRectangle(0.0f, 0.0f, float(subW), 28.0f, 6.0f); g.setColour(mock::line); g.drawRoundedRectangle(0.5f, 0.5f, float(subW) - 1.0f, 27.0f, 6.0f, 1.0f);
    }
    void resized() override {
        auto r = getLocalBounds();
        int x = 3; for (auto* b : subB) { int w = int(textWidth(juce::Font(10.5f, juce::Font::bold), b->getButtonText())) + 20; b->setBounds(x, 3, w, 22); x += w + 2; } subW = x + 2;
        importB.setBounds(r.getRight() - 80 - 8, 2, 80, 24); exportB.setBounds(importB.getX() - 6 - 112, 2, 112, 24);
        vp.setBounds(r.withTrimmedTop(36));
        const int w = juce::jmax(640, vp.getWidth() - vp.getScrollBarThickness());
        inner.layoutAll(w);
        inner.setSize(w, inner.totalH);
    }
private:
    struct OptRow : juce::Component {
        OptSpec s; SettingsView* owner;
        juce::ToggleButton tog; juce::Slider sld; juce::ComboBox drop; juce::OwnedArray<MockButton> segs;
        float val() const { return s.get ? s.get() : wm::mixer::settings().getOpt(s.key.toStdString(), s.def); }
        void put(float v) { if (s.set) s.set(v); else wm::mixer::settings().setOpt(s.key.toStdString(), v); if (owner->onSettingsChanged) owner->onSettingsChanged(); }
        OptRow(const OptSpec& sp, SettingsView* o) : s(sp), owner(o) {

            if (s.kind == OptSpec::Toggle) { tog.setTooltip(s.tip); tog.onClick = [this] { put(tog.getToggleState() ? 1.0f : 0.0f); }; addAndMakeVisible(tog); }
            if (s.kind == OptSpec::Slider) {
                sld.setSliderStyle(juce::Slider::LinearHorizontal); sld.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18); sld.setRange(double(s.lo), double(s.hi), double(s.step));
                sld.setTextValueSuffix(s.suffix); sld.setColour(juce::Slider::thumbColourId, col::teal); sld.setDoubleClickReturnValue(true, double(s.def)); sld.setTooltip(s.tip);
                sld.onValueChange = [this] { put(float(sld.getValue())); }; addAndMakeVisible(sld);
            }
            if (s.kind == OptSpec::Seg) for (int i = 0; i < s.opts.size(); ++i) { auto* b = segs.add(new MockButton(s.opts[i])); b->radius = 3.0f; b->bold = true; b->fontSize = 10.0f; b->onClick = [this, i] { put(float(i)); sync(); }; addAndMakeVisible(b); }
            if (s.kind == OptSpec::Drop) { fill(s.opts); drop.onChange = [this] { put(float(drop.getSelectedId() - 1)); }; addAndMakeVisible(drop); }
            sync();
        }
        void fill(const juce::StringArray& o) { s.opts = o; drop.clear(juce::dontSendNotification); for (int i = 0; i < o.size(); ++i) drop.addItem(o[i], i + 1); sync(); }
        void sync() {
            const float v = val();
            tog.setToggleState(v > 0.5f, juce::dontSendNotification); sld.setValue(double(v), juce::dontSendNotification);
            for (int i = 0; i < segs.size(); ++i) segs[i]->setToggleState(i == int(v + 0.5f), juce::dontSendNotification);
            if (drop.getNumItems() > 0) drop.setSelectedId(juce::jlimit(0, drop.getNumItems() - 1, int(v + 0.5f)) + 1, juce::dontSendNotification);
        }
        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colour(0xffeceae4)); g.setFont(juce::Font(11.0f)); g.drawText(s.label, 0, 0, getWidth() * 60 / 100, getHeight(), juce::Justification::centredLeft, true);
            if (s.hint.isNotEmpty()) { g.setColour(s.kind == OptSpec::Note ? juce::Colour(0xffffd100) : juce::Colour(0xffb9b6ae)); g.setFont(juce::Font(10.0f)); g.drawText(s.hint, getLocalBounds().withTrimmedRight(s.kind == OptSpec::Toggle ? 30 : 0), juce::Justification::centredRight, true); }
        }
        void resized() override {
            auto r = getLocalBounds();
            if (s.kind == OptSpec::Toggle) tog.setBounds(r.removeFromRight(24).withSizeKeepingCentre(22, 22));
            if (s.kind == OptSpec::Slider) sld.setBounds(r.removeFromRight(juce::jmin(190, r.getWidth() * 55 / 100)));
            if (s.kind == OptSpec::Drop) drop.setBounds(r.removeFromRight(juce::jmin(120, r.getWidth() / 2)).reduced(0, 1));
            if (s.kind == OptSpec::Seg) { int x = r.getRight(); for (int i = segs.size() - 1; i >= 0; --i) { int w = int(textWidth(juce::Font(10.0f, juce::Font::bold), s.opts[i])) + 18; x -= w; segs[i]->setBounds(x, 2, w, r.getHeight() - 4); } }
        }
    };
    struct Card { juce::String title; std::vector<std::unique_ptr<OptRow>> rows; juce::Rectangle<int> r; int col = 0, rowTop = 0; };
    struct Inner : juce::Component {
        SettingsView* o = nullptr; int totalH = 0; juce::Rectangle<int> layoutCard; int y1 = 0, y2 = 0, y3 = 0;
        void paint(juce::Graphics& g) override {
            g.fillAll(col::bg);
            drawCard(g, layoutCard, juce::String(juce::CharPointer_UTF8("Channel layout \xC2\xB7 names, icons and colours show everywhere (mixer, routing, MIDI hub)")), col::title);
            for (auto& c : o->cards) drawCard(g, c.r, c.title, col::title);
        }
        void layoutAll(int w) {
            const int gap = 8, rowH = 24, pad = 10;
            layoutCard = {0, 0, w, 34 + 30 + o->table.neededHeight() + 8};
            o->modeB[0]->setBounds(10, 30, 52, 22); o->modeB[1]->setBounds(66, 30, 66, 22); o->modeB[2]->setBounds(136, 30, 62, 22);
            hideB().setBounds(w - 10 - 100 - 6 - 100, 28, 100, 24); resetB().setBounds(w - 10 - 100, 28, 100, 24);
            o->table.setBounds(10, 60, w - 20, o->table.neededHeight());
            int y = layoutCard.getBottom() + gap; y1 = y;
            const int cw = (w - 2 * gap) / 3;
            auto place = [&](int ci, int col, int top) {
                auto& c = o->cards[size_t(ci)]; c.r = {col * (cw + gap), top, cw, 30 + int(c.rows.size()) * rowH + pad}; int yy = c.r.getY() + 30;
                for (auto& r : c.rows) { r->setBounds(c.r.getX() + pad, yy, cw - 2 * pad, rowH); yy += rowH; }
                return c.r.getBottom(); };
            int b1 = 0; for (int i = 0; i < 3; ++i) b1 = juce::jmax(b1, place(i, i, y));
            for (int i = 0; i < 3; ++i) o->cards[size_t(i)].r.setBottom(b1);
            y = b1 + gap; y2 = y;
            int b2 = 0; for (int i = 3; i < 6; ++i) b2 = juce::jmax(b2, place(i, i - 3, y));
            for (int i = 3; i < 6; ++i) o->cards[size_t(i)].r.setBottom(b2);
            y = b2 + gap; y3 = y;
            auto& rc = o->cards[6]; rc.r = {0, y, w, 30 + int(rc.rows.size()) * rowH + pad}; int yy = y + 30;
            for (auto& r : rc.rows) { r->setBounds(pad, yy, w - 2 * pad, rowH); yy += rowH; }
            totalH = rc.r.getBottom() + 8;
        }
        MockButton& hideB() { return o->hideB; } MockButton& resetB() { return o->resetB; }
    } inner;
    void setMode(int m) { table.mode = m; for (int i = 0; i < 3; ++i) modeB[i]->setToggleState(i == m, juce::dontSendNotification); table.rebuild(); resized(); }
    void gotoSection(int i) {
        for (int k = 0; k < subB.size(); ++k) subB[k]->setToggleState(k == i, juce::dontSendNotification);
        int y = i == 0 ? 0 : i == 1 ? inner.y1 : i == 2 ? inner.y2 : i == 3 ? inner.y2 : inner.y3;
        vp.setViewPosition(0, y);
    }
    void addCard(const char* title, std::vector<OptSpec> specs) {
        cards.emplace_back(); auto& c = cards.back(); c.title = title;
        for (auto& sp : specs) { c.rows.push_back(std::make_unique<OptRow>(sp, this)); inner.addAndMakeVisible(*c.rows.back()); }
    }
    static OptSpec T(const char* l, const char* k, bool d, const char* tip = "") { OptSpec s{OptSpec::Toggle, l, k, d ? 1.0f : 0.0f}; s.tip = tip; return s; }
    static OptSpec S(const char* l, const char* k, int d, juce::StringArray o, const char* tip = "") { OptSpec s{OptSpec::Seg, l, k, float(d), o}; s.tip = tip; return s; }
    static OptSpec D(const char* l, const char* k, int d, juce::StringArray o, const char* tip = "") { OptSpec s{OptSpec::Drop, l, k, float(d), o}; s.tip = tip; return s; }
    static OptSpec L(const char* l, const char* k, float d, float lo, float hi, float st, const char* suf, const char* tip = "") { OptSpec s{OptSpec::Slider, l, k, d, {}, lo, hi, st, suf}; s.tip = tip; return s; }
    void buildCards() {
        inner.o = this;
        auto& st = wm::mixer::settings();
        auto real = [&](OptSpec s, std::function<float()> g, std::function<void(float)> f) { s.get = std::move(g); s.set = std::move(f); return s; };
        auto none = juce::StringArray{"None"};
        auto dim = real(L("Main out dim", "maindim", -20, -60, 0, 1, " dB", "How far Dim lowers the control room outputs"), [] { return 0.0f; }, [](float) {});
        dim.get = [this] { for (auto& b : mx.state().buses) if (b.controlRoom) return b.dimDb; return -20.0f; };
        dim.set = [this](float v) { for (auto& b : mx.state().buses) if (b.controlRoom) b.dimDb = v; };
        addCard("Mixer", {D("Talkback input", "talkIn", 0, none), L("Talkback dim", "talkDim", -20, -60, 0, 1, " dB"), D("Listenback input", "listenIn", 0, none),
            L("Main out recall", "recall", -10, -60, 0, 1, " dB"), dim, D("External input", "extIn", 0, none),
            S("Solo mode", "soloMode", 0, {"Solo", "PFL (live)"}), T("Exclusive solo", "exSolo", false), T("2-row: inputs only on top", "twoRowTop", false)});
        addCard("Level meters", {S("Clip after", "clip", 1, {"1", "3", "5"}, "Samples in a row before the clip light turns on"),
            real(L("Peak hold", "peak", 2, 0, 10, 0.5f, " s", "How long meter peaks stay. 0 = off"), [&st] { return st.peakHoldSec; }, [&st](float v) { st.peakHoldSec = v; }),
            T("RMS +3 dB", "rms3", true), real(T("Show compressor / auto level", "gr", true, "Shows the compressor gain reduction under a strip when its compressor is on"), [&st] { return st.showGrOnStrips ? 1.0f : 0.0f; }, [&st](float v) { st.showGrOnStrips = v > 0.5f; })});
        addCard("Mixer views", {T("FX send follows highest submix", "fxHigh", true), T("FX send follows Main", "fxMain", false), T("Center pan when changing mono/stereo", "centerPan", true), T("Layout presets remember open strips", "presetStrips", false)});
        addCard("Operation", {real(T("Double-click fader to reset", "dbl", true, "Double-click a fader or knob to put it back to its default"), [&st] { return st.doubleClickReset ? 1.0f : 0.0f; }, [&st](float v) { st.doubleClickReset = v > 0.5f; }),
            T("Mouse wheel on knobs", "wheelKnobs", true), T("Lock interface (password)", "lockUi", false),
            real(T("Save changes every minute", "auto", true, "Writes autosave.wmix in the app data folder"), [&st] { return st.autosave ? 1.0f : 0.0f; }, [&st](float v) { st.autosave = v > 0.5f; }),
            real(T("Info bubbles on hover", "tips", true, "Show a short explanation when the mouse rests on a control"), [&st] { return st.tooltips ? 1.0f : 0.0f; }, [&st](float v) { st.tooltips = v > 0.5f; }),
            S("Theme", "theme", 0, {"Dark", "Darker", "Light", "High contrast"})});
        addCard("Snapshots and workspaces", {T("Load room EQ with snapshot", "snapEq", false), T("Load groups with snapshot", "snapGroups", false), T("Soften volume jumps on snapshot load", "snapSoft", true),
            real(T("Open the autosave at start", "onstart", true, "Come back to where you left off"), [&st] { return st.loadAutosaveOnStart ? 1.0f : 0.0f; }, [&st](float v) { st.loadAutosaveOnStart = v > 0.5f; })});
        OptSpec hw{OptSpec::Note, "Hardware mixer control", "hwNote"}; hw.hint = juce::String(juce::CharPointer_UTF8("needs capture \xC2\xB7 software mix active"));
        addCard("Devices", {D("Interface", "iface", 0, {"Digiface USB", "Other"}), S("Sample rate", "rate", 0, {"44.1", "48", "88.2", "96"}), S("Buffer", "buf", 1, {"64", "128", "256", "512"}), hw, S("Built-in FX run on", "fxOn", 0, {"Computer", "Interface DSP"})});
        OptSpec midi = T("MIDI controller 1", "rcMidi", false); midi.hint = "In: none  Out: none";
        OptSpec osc = T("OSC", "rcOsc", false); osc.hint = "port in 7001, out 9001";
        OptSpec fk = T("F-keys A / B / C speakers", "rcFkeys", true); fk.hint = "F10 F11 F12";
        addCard("Remote control", {midi, T("Mackie Control", "rcMackie", false), T("Map stereo to 2 mono faders", "rcStereoMap", false), osc, S("OSC faders per bank", "rcBank", 0, {"8", "16"}), S("Keyboard volume keys control", "rcVolKeys", 0, {"Main", "Linux output"}), fk});
        rebuildDrops();
    }
    void rebuildDrops() {
        juce::StringArray in{"None"}; for (auto& c : mx.state().channels) if (!c.playback) in.add(juce::String(c.name));
        for (auto& c : cards) for (auto& r : c.rows) if (r->s.kind == OptSpec::Drop && r->s.key != "iface") r->fill(in);
    }
    void exportSettings() {
        chooser = std::make_unique<juce::FileChooser>("Export settings", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("whiskermix-settings.json"), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f != juce::File()) f.replaceWithText(juce::String(wm::mixer::settings().toJson())); });
    }
    void importSettings() {
        chooser = std::make_unique<juce::FileChooser>("Import settings", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File() || !f.existsAsFile()) return;
            if (wm::mixer::settings().fromJson(f.loadFileAsString().toStdString())) { syncSettings(); if (onSettingsChanged) onSettingsChanged(); } });
    }
    wm::mixer::Mixer& mx; LayoutTable table; juce::Viewport vp; MockButton hideB, resetB, exportB, importB; juce::OwnedArray<MockButton> modeB, subB;
    std::vector<Card> cards; std::unique_ptr<juce::FileChooser> chooser; int subW = 0;
};

}  // namespace wmui
