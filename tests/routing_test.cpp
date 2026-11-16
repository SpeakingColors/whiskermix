#include <cstdio>
#include <cstdlib>
#include "../src/routing/AdatRouting.h"
#include "../src/mixer/Layout.h"
#include "../src/mixer/StateJson.h"
using namespace wm::routing;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
int main() {
    CHECK(adatChannels(48000) == 32 && adatChannels(44100) == 32);
    CHECK(adatChannels(96000) == 16 && adatChannels(88200) == 16);
    CHECK(adatChannels(192000) == 8 && adatChannels(176400) == 8);
    CHECK(adatChannels(32000) == 0 && adatChannels(64000) == 0);
    CHECK(adatPerPort(RateClass::Single) == 8 && adatPerPort(RateClass::Double) == 4 && adatPerPort(RateClass::Quad) == 2);
    CHECK(hwActive(15, 48000) && hwActive(15, 96000) && !hwActive(16, 96000) && !hwActive(8, 192000) && hwActive(7, 192000));
    CHECK(hwActive(32, 192000) && !hwActive(32, 32000) && !hwActive(-1, 48000));
    CHECK(hwName(0) == "ADAT 1" && hwName(15) == "ADAT 16" && hwName(32) == "Phones L" && hwName(-1) == "Software");
    auto n = channelNames(34, 96000); CHECK(n.size() == 34 && n[15] == "ADAT 16" && n[16] == "Ch 17");
    CHECK(pulseAdatChannel(9, 48000) == 9 && pulseSourceBlock(9, 48000) == 3 && pulseSourceBlock(0, 48000) == 2 && pulseSourceBlock(31, 48000) == 5);
    CHECK(pulseAdatChannel(3, 96000) == -1 && pulseSourceBlock(3, 96000) == -1);
    // clock
    auto a = planClock(ClockMaster::Digiface, 48000);
    CHECK(a.ok && a.sendPulseClock && a.pulseClock == wm::pulse16::Clock::Adat && !a.sendPulseRate);
    auto b = planClock(ClockMaster::Pulse, 96000);
    CHECK(b.ok && b.pulseClock == wm::pulse16::Clock::Master && b.sendPulseRate && b.pulseRate == wm::pulse16::Rate::k96);
    CHECK(!planClock(ClockMaster::Pulse, 64000).ok && !planClock(ClockMaster::Digiface, 32000).ok);
    CHECK(masterFromOpt(0) == ClockMaster::Digiface && masterFromOpt(1) == ClockMaster::Pulse);
    CHECK(rateFromOpt(96000.0f) == 96000 && rateFromOpt(1234.0f) == 48000 && rateFromOpt(-5.0f, 44100) == 44100);
    // links
    CHECK(shellQuote("a'b") == "'a'\\''b'");
    std::vector<BusLink> bl = {{"WM:Main L", "WM:Main R", 0, true}, {"WM:X L", "WM:X R", 14, true}, {"WM:Y L", "WM:Y R", -1, true}, {"WM:PH L", "WM:PH R", 32, true}};
    std::vector<std::string> sk;
    auto c = busLinkCommands(bl, "dev:playback_AUX", 192000, &sk);
    CHECK(c.size() == 2 && c[0] == "pw-link 'WM:Main L' 'dev:playback_AUX0'" && c[1] == "pw-link 'WM:Main R' 'dev:playback_AUX1'");
    CHECK(sk.size() == 2);
    CHECK(busLinkCommands(bl, "dev:playback_AUX", 48000).size() == 4);
    // layout check at 96 kHz: studio layout has ADAT 15/16 and outputs up to 16, still within 16; EXCITER at 9 fine
    auto built = wm::mixer::buildFromLayout(wm::mixer::studioLayout());
    CHECK(unavailableStrips(built.mixer.state(), 48000).empty());
    CHECK(unavailableStrips(built.mixer.state(), 96000).empty());
    auto u = unavailableStrips(built.mixer.state(), 192000); CHECK(!u.empty());
    // corrupt workspace must not crash and must not change the routing
    wm::mixer::State st; wm::mixer::RoutingMatrix rt(2, 2); rt.set(0, 0, -3.0f);
    CHECK(wm::mixer::fromJson("{\"routing\":{\"in\":-5,\"out\":99999999,\"g\":[1]},\"channels\":[],\"buses\":[]}", st, rt) == false);
    CHECK(rt.inputs() == 2 && rt.get(0, 0) == -3.0f);
    std::puts("routing: passed");
    return 0;
}
