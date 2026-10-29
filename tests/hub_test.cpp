#include <cassert>
#include <cstdio>
#include "../src/mixer/MidiHub.h"
#include "../src/mixer/StateJson.h"
#include "../src/mixer/Settings.h"
using namespace wm::mixer;
int main() {
    MidiHub h;
    assert(h.rescan({"drumlogue MIDI 1", "MODX M6"}) == 2);
    assert(h.rescan({"drumlogue MIDI 1", "MODX M6"}) == 0);
    assert(h.rescan({"MODX M6"}) == 0 && !h.devices()[0].online && h.devices()[1].online);
    h.devices()[0].name = "drumlogue"; h.devices()[0].chLo = 6; h.devices()[0].chHi = 17; h.devices()[0].multi = true; h.devices()[0].serial = "ABC123";
    h.store("Studio");
    h.devices()[0].name = "changed"; h.store("DJ set");
    assert(h.recall("Studio") && h.devices()[0].name == "drumlogue");
    assert(h.recallByProgram(0) && h.devices()[0].name == "changed");   // "DJ set" sorts before "Studio"
    assert(!h.recall("nope") && !h.recallByProgram(9));
    // log
    h.push({"22:41:03.120", "a", "OUT", "SysEx", "Patch request F0 43 20 7E 00 01 02 03 04 05", LogKind::SysEx, false});
    h.push({"22:41:03.621", "b", "IN", "-", "no reply (500 ms)", LogKind::Other, true});
    h.push({"22:41:04.010", "c", "IN", "CC", "Part 3 Cutoff = 88 /home/maxime/x", LogKind::CC, false});
    assert(h.filtered(0).size() == 3 && h.filtered(2).size() == 1 && h.filtered(3).size() == 1 && h.filtered(4).empty());
    assert(h.filtered(0)[0].type == "CC");   // newest first
    assert(h.lastWarning() == "no reply (500 ms)");
    auto rep = h.debugReport({}, "0.1", "ALSA /home/maxime/.config/x", "crash at /Users/bob/app");
    assert(rep.find("maxime") == std::string::npos && rep.find("bob") == std::string::npos && rep.find("ABC123") == std::string::npos);
    assert(rep.find("dev1") != std::string::npos && rep.find("drumlogue") == std::string::npos && rep.find("Patch request F0...") != std::string::npos);
    ReportOptions raw{false, false, false, false, false, true, true, true};
    assert(h.debugReport(raw, "0.1", "", "").find("ABC123") != std::string::npos);
    assert(MidiHub::scrubPaths("C:\\Users\\Al\\Doc") == "C:\\Users\\user\\Doc");
    for (int i = 0; i < 6000; ++i) h.push({"t", "r", "IN", "CC", "x", LogKind::CC, false});
    assert(h.logSize() == 5000);
    // json round trip
    MidiHub h2; assert(h2.fromJson(h.toJson()) && h2.devices().size() == 2 && h2.devices()[0] == h.devices()[0] && h2.devices()[0].chHi == 17);
    // channel extras round trip
    State st; st.channels.resize(1); st.buses.resize(1);
    auto& c = st.channels[0]; c.name = "MODX"; c.colour = 4; c.emoji = "\xF0\x9F\x8E\xB9"; c.width = 1.4f; c.midSide = true; c.fxSendDb = -20; c.cue = true; c.hidden = true;
    State b; assert(fromJson(toJson(st), b));
    assert(b.channels[0].colour == 4 && b.channels[0].emoji == c.emoji && b.channels[0].midSide && b.channels[0].cue && b.channels[0].hidden && std::abs(b.channels[0].width - 1.4f) < 1e-3 && b.channels[0].fxSendDb == -20);
    Settings a; a.tooltips = false; a.peakHoldSec = 99; a.autosave = false;
    a.setOpt("theme", 2); a.setOpt("clip", 3);
    Settings b2; assert(b2.fromJson(a.toJson()) && b2.getOpt("theme", 0) == 2 && b2.getOpt("clip", 0) == 3 && b2.getOpt("none", 7) == 7 && !b2.tooltips && !b2.autosave && b2.peakHoldSec == 10.0f && b2.doubleClickReset);
    std::puts("hub_test ok");
}
