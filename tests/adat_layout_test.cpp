#include <cstdio>
#include <cstdlib>
#include "../src/mixer/AdatLayout.h"
using namespace wm::mixer;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
int main() {
    CHECK(speedForRate(44100) == Speed::Single && speedForRate(48000) == Speed::Single);
    CHECK(speedForRate(88200) == Speed::Double && speedForRate(96000) == Speed::Double);
    CHECK(speedForRate(176400) == Speed::Quad && speedForRate(192000) == Speed::Quad);
    // Documented totals: 32 / 16 / 8 over four ports
    CHECK(adatChannelsTotal(Speed::Single) == 32 && adatChannelsTotal(Speed::Double) == 16 && adatChannelsTotal(Speed::Quad) == 8);
    AdatSlot s;
    CHECK(adatSlotFor(8, Speed::Single, s) && s.port == 1 && s.slot == 0);     // ADAT 9 = port 2 at 48k
    CHECK(adatSlotFor(8, Speed::Double, s) && s.port == 2 && s.slot == 0);     // ADAT 9 = port 3 at 96k
    CHECK(adatSlotFor(7, Speed::Quad, s) && s.port == 3 && s.slot == 1);
    CHECK(!adatSlotFor(32, Speed::Single, s) && !adatSlotFor(8, Speed::Quad, s) && !adatSlotFor(-1, Speed::Single, s));
    CHECK(adatFirstChannelOfPort(2, Speed::Double) == 8 && pulseAdatColumn(0) == 2 && pulseAdatColumn(3) == 5);
    // The owner's layout uses ADAT 1..16 and phones: all of it at 48k; at 96k all 16 channels still fit (4 per port)
    auto l = studioLayout();
    CHECK(layoutForSpeed(l, Speed::Single).size() == l.size());
    CHECK(layoutForSpeed(l, Speed::Double).size() == l.size());
    // At quad speed only ADAT 1..8 and phones remain: strips on 9..16 vanish, phones stay
    auto q = layoutForSpeed(l, Speed::Quad);
    CHECK(q.size() < l.size());
    for (const auto& d : q) CHECK(d.hw < 0 || d.hw >= 32 || d.hw + (d.stereo ? 1 : 0) < 8);
    bool ph = false; for (const auto& d : q) if (d.name == "PH") ph = true; CHECK(ph);
    std::puts("adat layout: passed");
    return 0;
}
