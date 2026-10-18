#include <cstdio>
#include <cstdlib>
#include "../src/names/NameSync.h"
int main() {
    wm::names::DeviceNames d{"~alsa_.*Digiface.*", "Studio Interface", "Studio", {"Kick", "Snare \"top\""}};
    auto s = wm::names::wirePlumberRule(d);
    if (s.find("node.description = \"Studio Interface\"") == std::string::npos ||
        s.find("\"Snare \\\"top\\\"\"") == std::string::npos) { std::puts("FAIL names"); return 1; }
    std::puts("names: passed");
    return 0;
}
