// One list of channel names for the whole system. Generates the WirePlumber rule that gives the
// device and its channels the same names in KDE sound settings, patchbays and DAWs.
// Experimental: the exact node name and property format must be checked with tools/wm-diagnose.sh.
#pragma once
#include <string>
#include <vector>

namespace wm::names {

struct DeviceNames {
    std::string nodeNameMatch;   // glob or regex against node.name, e.g. "~alsa_.*Digiface.*"
    std::string description;     // shown by GUI apps (node.description)
    std::string nick;             // short name (node.nick)
    std::vector<std::string> channels;  // one per port, in order (node.channel-names)
};

inline std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

// WirePlumber 0.5 style conf.d fragment.
inline std::string wirePlumberRule(const DeviceNames& d) {
    std::string out = "monitor.alsa.rules = [\n  {\n    matches = [ { node.name = \"" + escape(d.nodeNameMatch) +
                      "\" } ]\n    actions = {\n      update-props = {\n        node.description = \"" +
                      escape(d.description) + "\"\n        node.nick = \"" + escape(d.nick) + "\"\n";
    if (!d.channels.empty()) {
        out += "        node.channel-names = [";
        for (const auto& c : d.channels) out += " \"" + escape(c) + "\"";
        out += " ]\n";
    }
    out += "      }\n    }\n  }\n]\n";
    return out;
}

}  // namespace wm::names
