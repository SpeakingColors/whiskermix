// MIDI hub model: device cards, snapshots of the device list, live log with filters, anonymised debug report.
// Pure C++ (no JUCE), tested in tests/hub_test.cpp. The JUCE page only displays and edits this.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace wm::mixer {

struct MidiDevice {
    std::string name, emoji, maker, inPort, outPort, serial;
    int colour = 0;                 // 0..8, same palette as mixer strips
    bool multi = false;             // single channel or a range
    int chLo = 1, chHi = 1;         // MIDI channels 1..16
    int clock = 0;                  // 0 receive, 1 send, 2 off
    bool fPC = true, fCC = true, fTransport = true, fSysEx = false;
    bool online = true;
    bool operator==(const MidiDevice& o) const {
        return name == o.name && emoji == o.emoji && inPort == o.inPort && outPort == o.outPort && colour == o.colour && multi == o.multi &&
               chLo == o.chLo && chHi == o.chHi && clock == o.clock && fPC == o.fPC && fCC == o.fCC && fTransport == o.fTransport && fSysEx == o.fSysEx;
    }
};

enum class LogKind { Note, CC, SysEx, Clock, Other };
struct LogEntry {
    std::string time, route, dir, type, text;   // dir: IN or OUT
    LogKind kind = LogKind::Other;
    bool warn = false;
};

struct ReportOptions {
    bool removeUser = true, removePaths = true, hashSerials = true, channelIds = true, truncateSysEx = true, lastLog = true, audio = true, crash = true;
};

class MidiHub {
public:
    int addDevice(MidiDevice d) { devs_.push_back(std::move(d)); return int(devs_.size()) - 1; }
    std::vector<MidiDevice>& devices() { return devs_; }
    const std::vector<MidiDevice>& devices() const { return devs_; }
    void forget(int i) { if (i >= 0 && i < int(devs_.size())) devs_.erase(devs_.begin() + i); }

    // Rescan: ports that appear get a new card, known ports go online, missing ones go offline. Returns number of new cards.
    int rescan(const std::vector<std::string>& ports) {
        int added = 0;
        for (auto& d : devs_) d.online = std::find(ports.begin(), ports.end(), d.inPort) != ports.end();
        for (auto& p : ports) {
            bool known = false;
            for (auto& d : devs_) known = known || d.inPort == p;
            if (known) continue;
            MidiDevice d; d.name = p; d.inPort = p; d.outPort = p; d.colour = int(devs_.size()) % 9;
            devs_.push_back(d); ++added;
        }
        return added;
    }

    // Snapshots store the whole device list (names, channels, filters, clock).
    void store(const std::string& n) { snaps_[n] = devs_; }
    bool recall(const std::string& n) { auto it = snaps_.find(n); if (it == snaps_.end()) return false; devs_ = it->second; return true; }
    std::vector<std::string> snapshotNames() const { std::vector<std::string> v; for (auto& k : snaps_) v.push_back(k.first); return v; }
    // Program Change on the control channel recalls the snapshot with that index (sorted by name order of creation is not kept, so by list order).
    bool recallByProgram(int pc) { auto n = snapshotNames(); if (pc < 0 || pc >= int(n.size())) return false; return recall(n[size_t(pc)]); }

    // Live log keeps the newest entries only (the UI asks for "last 10 min"; the cap bounds memory).
    void push(LogEntry e) { log_.push_back(std::move(e)); while (log_.size() > kMaxLog) log_.pop_front(); }
    void clearLog() { log_.clear(); }
    size_t logSize() const { return log_.size(); }
    // Filtered, newest first.
    std::vector<LogEntry> filtered(int filter /*0 all,1 notes,2 cc,3 sysex,4 clock*/) const {
        std::vector<LogEntry> v;
        for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
            bool ok = filter == 0 || (filter == 1 && it->kind == LogKind::Note) || (filter == 2 && it->kind == LogKind::CC) ||
                      (filter == 3 && it->kind == LogKind::SysEx) || (filter == 4 && it->kind == LogKind::Clock);
            if (ok) v.push_back(*it);
        }
        return v;
    }
    // Last warning (device not answering etc.), empty when none.
    std::string lastWarning() const { for (auto it = log_.rbegin(); it != log_.rend(); ++it) if (it->warn) return it->text; return {}; }

    static uint32_t fnv(const std::string& s) { uint32_t h = 2166136261u; for (unsigned char c : s) { h ^= c; h *= 16777619u; } return h; }
    static std::string hashHex(const std::string& s) { char b[16]; std::snprintf(b, sizeof b, "%08x", fnv(s)); return b; }

    // Strip home directories and user names from text.
    static std::string scrubPaths(std::string s) {
        for (const char* root : {"/home/", "/Users/", "C:\\Users\\"}) {
            size_t p = 0; std::string r = root;
            while ((p = s.find(r, p)) != std::string::npos) {
                size_t st = p + r.size(), e = s.find_first_of("/\\ \n\"", st);
                if (e == std::string::npos) e = s.size();
                s.replace(st, e - st, "user"); p = st + 4;
            }
        }
        return s;
    }

    // Anonymised report. Nothing is sent anywhere; the user reviews the text first.
    std::string debugReport(const ReportOptions& o, const std::string& appVersion, const std::string& audioInfo, const std::string& crash) const {
        std::ostringstream r;
        r << "WhiskerMix debug report\nversion: " << appVersion << "\n\ndevices: " << devs_.size() << "\n";
        for (size_t i = 0; i < devs_.size(); ++i) {
            auto& d = devs_[i];
            r << "  " << (o.channelIds ? "dev" + std::to_string(i + 1) : d.name) << " ch " << d.chLo << "-" << d.chHi
              << " clock " << (d.clock == 0 ? "rx" : d.clock == 1 ? "tx" : "off") << " " << (d.online ? "online" : "offline");
            if (!d.serial.empty()) r << " serial " << (o.hashSerials ? hashHex(d.serial) : d.serial);
            r << "\n";
        }
        if (o.audio) r << "\naudio: " << (o.removePaths ? scrubPaths(audioInfo) : audioInfo) << "\n";
        if (o.lastLog) {
            r << "\nlast midi log (newest first)\n";
            int n = 0;
            for (auto it = log_.rbegin(); it != log_.rend() && n < 200; ++it, ++n) {
                std::string t = it->text;
                if (o.truncateSysEx && it->kind == LogKind::SysEx && t.size() > 16) t = t.substr(0, 16) + "...";
                r << it->time << " " << it->dir << " " << it->type << " " << (o.removePaths ? scrubPaths(t) : t) << "\n";
            }
        }
        if (o.crash && !crash.empty()) r << "\ncrash trace\n" << (o.removePaths ? scrubPaths(crash) : crash) << "\n";
        return r.str();
    }

    // Device list JSON (stored next to the workspace).
    std::string toJson() const {
        std::string o = "[";
        for (size_t i = 0; i < devs_.size(); ++i) {
            auto& d = devs_[i]; char b[160];
            std::snprintf(b, sizeof b, "%s{\"colour\":%d,\"multi\":%d,\"lo\":%d,\"hi\":%d,\"clock\":%d,\"pc\":%d,\"cc\":%d,\"tr\":%d,\"sx\":%d,\"name\":\"", i ? "," : "",
                          d.colour, d.multi, d.chLo, d.chHi, d.clock, d.fPC, d.fCC, d.fTransport, d.fSysEx);
            o += b; o += esc(d.name) + "\",\"emoji\":\"" + esc(d.emoji) + "\",\"in\":\"" + esc(d.inPort) + "\",\"out\":\"" + esc(d.outPort) + "\"}";
        }
        return o + "]";
    }
    bool fromJson(const std::string& s) {
        std::vector<MidiDevice> n;
        for (size_t p = s.find('{'); p != std::string::npos; p = s.find('{', p + 1)) {
            size_t e = s.find('}', p); if (e == std::string::npos) return false;
            MidiDevice d; std::string v;
            if (get(s, p, e, "name", v)) d.name = v;
            if (get(s, p, e, "emoji", v)) d.emoji = v;
            if (get(s, p, e, "in", v)) d.inPort = v;
            if (get(s, p, e, "out", v)) d.outPort = v;
            if (get(s, p, e, "colour", v)) d.colour = std::atoi(v.c_str());
            if (get(s, p, e, "multi", v)) d.multi = v == "1";
            if (get(s, p, e, "lo", v)) d.chLo = std::atoi(v.c_str());
            if (get(s, p, e, "hi", v)) d.chHi = std::atoi(v.c_str());
            if (get(s, p, e, "clock", v)) d.clock = std::atoi(v.c_str());
            if (get(s, p, e, "pc", v)) d.fPC = v == "1";
            if (get(s, p, e, "cc", v)) d.fCC = v == "1";
            if (get(s, p, e, "tr", v)) d.fTransport = v == "1";
            if (get(s, p, e, "sx", v)) d.fSysEx = v == "1";
            n.push_back(d); p = e;
        }
        devs_ = n; return true;
    }

private:
    static constexpr size_t kMaxLog = 5000;
    static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o.push_back('\\'); o.push_back(c); } return o; }
    static bool get(const std::string& s, size_t from, size_t to, const char* key, std::string& out) {
        std::string k = std::string("\"") + key + "\":"; size_t p = s.find(k, from);
        if (p == std::string::npos || p >= to) return false;
        p += k.size();
        if (s[p] == '"') { ++p; out.clear(); while (p < s.size() && s[p] != '"') { if (s[p] == '\\') ++p; out.push_back(s[p++]); } }
        else { size_t e = s.find_first_of(",}", p); out = s.substr(p, e - p); }
        return true;
    }
    std::vector<MidiDevice> devs_;
    std::map<std::string, std::vector<MidiDevice>> snaps_;
    std::deque<LogEntry> log_;
};

}  // namespace wm::mixer
