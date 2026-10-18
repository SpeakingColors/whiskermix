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
}
