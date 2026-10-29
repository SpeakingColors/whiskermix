#include <cstdio>
#include <cstdlib>
#include "../src/mixer/RoutingMatrix.h"
#include "../src/mixer/StateJson.h"
#include "../src/mixer/UndoStack.h"
using namespace wm::mixer;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
int main() {
    Mixer m(2, 1);
    m.rename(0, "Kick \"in\""); m.state().channels[0].gainDb = -6.5f; m.state().channels[0].pan = -0.25f;
    m.state().channels[1].mute = true; m.state().channels[1].phaseR = true;
    m.state().buses[0].dim = true; m.state().buses[0].volumeDb = -30.0f;
    State back;
    CHECK(fromJson(toJson(m.state()), back));
    CHECK(back.channels.size() == 2 && back.channels[0].name == "Kick \"in\"");
    CHECK(std::abs(back.channels[0].gainDb + 6.5f) < 0.01f && std::abs(back.channels[0].pan + 0.25f) < 0.01f);
    CHECK(back.channels[1].mute && back.channels[1].phaseR && !back.channels[1].solo);
    CHECK(back.buses[0].dim && std::abs(back.buses[0].volumeDb + 30.0f) < 0.01f);
    m.state().channels[0].fx.eqOn = true; m.state().channels[0].fx.midGain = 4.5f; m.state().channels[0].fx.compOn = true;
    m.state().channels[0].fx.threshDb = -24.0f; m.state().channels[0].fx.ratio = 6.0f; m.state().channels[0].fx.lowCutHz = 80.0f;
    RoutingMatrix rr(2, 1); rr.set(0, 0, -3.0f); rr.set(1, 0, 2.5f);
    State b2; RoutingMatrix r2;
    CHECK(fromJson(toJson(m.state(), rr), b2, r2));
    CHECK(b2.channels[0].fx.eqOn && b2.channels[0].fx.compOn && std::abs(b2.channels[0].fx.midGain - 4.5f) < 0.01f);
    CHECK(std::abs(b2.channels[0].fx.threshDb + 24.0f) < 0.01f && b2.channels[0].fx.ratio == 6.0f && b2.channels[0].fx.lowCutHz == 80.0f);
    CHECK(!b2.channels[1].fx.eqOn && b2.channels[1].fx.ratio == 3.0f);
    CHECK(r2.inputs() == 2 && r2.outputs() == 1 && std::abs(r2.get(0, 0) + 3.0f) < 0.01f && std::abs(r2.get(1, 0) - 2.5f) < 0.01f);
    m.state().buses[0].reverbOn = true; m.state().buses[0].reverb.type = wm::engine::ReverbType::ShimmerHall;
    m.state().buses[0].reverb.p.decay = 0.77f; m.state().buses[0].reverb.shimmerSemis = 7.0f; m.state().buses[0].delayOn = true;
    m.state().buses[0].delay.timeMs = 333.0f; m.state().buses[0].delay.shiftSemis = 12.0f;
    State b3; CHECK(fromJson(toJson(m.state()), b3));
    CHECK(b3.buses[0].reverbOn && b3.buses[0].delayOn && b3.buses[0].reverb.type == wm::engine::ReverbType::ShimmerHall);
    CHECK(std::abs(b3.buses[0].reverb.p.decay - 0.77f) < 0.001f && std::abs(b3.buses[0].reverb.shimmerSemis - 7.0f) < 0.001f);
    CHECK(std::abs(b3.buses[0].delay.timeMs - 333.0f) < 0.01f);
    UndoStack u;
    State before = m.state(); u.push(before);
    m.state().channels[0].gainDb = 0.0f;
    State cur = m.state(); m.state() = u.undo(cur);
    CHECK(m.state().channels[0].gainDb == -6.5f && u.canRedo());
    m.state() = u.redo(m.state()); CHECK(m.state().channels[0].gainDb == 0.0f);
    RoutingMatrix r(2, 2);
    r.toggle(0, 1); CHECK(r.routed(0, 1) && !r.routed(1, 1));
    r.addOutput(); r.addInput(); CHECK(r.routed(0, 1) && r.inputs() == 3 && r.outputs() == 3 && !r.routed(2, 2));
    std::puts("state/routing/undo: passed");
    return 0;
    {   // stereo split / join keep the routing in step: duplicate a row, erase a row
        RoutingMatrix r(3, 2);
        r.set(0, 0, 1.0f); r.set(1, 1, -3.0f); r.set(2, 0, -6.0f);
        r.duplicateInput(1);
        CHECK(r.inputs() == 4);
        CHECK(r.get(1, 1) == -3.0f && r.get(2, 1) == -3.0f && r.get(0, 0) == 1.0f && r.get(3, 0) == -6.0f);
        r.eraseInput(2);
        CHECK(r.inputs() == 3 && r.get(1, 1) == -3.0f && r.get(2, 0) == -6.0f);
        Mixer m; m.addChannel(); m.state().channels[0].trim = true;
        State b; CHECK(fromJson(toJson(m.state()), b) && b.channels[0].trim);
    }
}
