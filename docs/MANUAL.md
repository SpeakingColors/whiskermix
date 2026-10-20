# WhiskerMix manual

WhiskerMix is a mixer and studio hub by Studio 87 for an RME Digiface USB with a Ferrofish Pulse 16 DX behind it. It was made for that setup and tested only there. Other RME interfaces may work for the audio part and may not for the hardware mixer; you are welcome to try.

## How the mixer works
The mixer is a blend of two things:
- **Hardware control**: the faders, pan, phase, stereo link, M/S, main volume, dim and speaker B are written to the Digiface's own mixer over USB (the same messages the vendor mixer sends). Audio stays in the interface, no added latency.
- **Audio on the computer**: everything the hardware does not take over USB (mute, solo, width, EQ, dynamics, reverb, echo, chorus, limiter) runs in WhiskerMix through PipeWire or JACK on Linux, and through the system audio or ASIO on Windows and macOS. Your DAW sees the same device names.
If no Digiface is found the app runs in dry run: every control works, the same messages are prepared, nothing is sent. The toolbar shows which mode you are in.

## Install
See INSTALL.txt in your system's folder. Linux needs PipeWire and libusb; the installer adds the udev rule so the Digiface can be reached without root, then replug the interface.

## Pages
### Mixer
- Four rows like TotalMix: hardware inputs, software playback, hardware outputs, control room (Main and Speaker B).
- Fader: drag, mouse wheel, double-click resets to 0 dB. The dB value is under every fader.
- M mutes (red), S solos (yellow). Pan knob above the fader.
- Double-click a channel name to rename it. Names, icons and colours show everywhere (mixer, routing, MIDI hub).
- The wrench opens that channel's settings: stereo link, width, M/S, phase L/R, EQ, dynamics, FX send. The x closes only the panel.
- Main: volume, Dim (-20 dB), Mono, Speaker B. Snapshots store the whole mixer under a name; workspaces 1 to 5 are quick slots; Save/Load write .wmix files.
- Undo and redo cover every change.

### Routing
Inputs and playbacks on the rows, outputs on the columns. Tap a square to route, drag up or down for the level, double-click resets, wheel steps by 1 dB. The Cables view lists the active routes.

### MIDI hub
Every MIDI device in one place: name, colour, icon, channel, in and out port, clock, filters (PC, CC, transport, SysEx). Snapshots recall a whole MIDI setup. The live log shows the last 10 minutes; filters narrow it. The debug report is anonymised and nothing is sent anywhere by itself.

### Pulse 16
Controls a Ferrofish Pulse 16 over DIN MIDI (SysEx) through any class compliant MIDI interface; RemoteFish is not needed and must be closed because it holds the port.
1. Connect the Pulse MIDI in and out to your interface (OUT of the interface to IN of the Pulse, and back).
2. Press Find Pulse (or pick the ports and press Connect). The page shows what the unit reports: gains, levels, routing, clock, rate, headphones, active preset, unit id.
3. Clock: one master only. With the Pulse on Master, set the Digiface to follow its ADAT input. Never set both to follow each other.
4. The Pulse screen can show a rate it is not running: Resend sends it again.
5. Routing: rows are output blocks, columns are input blocks, one source per row.
6. Presets 1 to 6 live in the unit. Save overwrites the slot and asks twice. Headphone volume is not part of a preset.
7. Backup writes the whole setup as text; Restore sends a saved backup back to the unit.
Brightness and key click are not reported by the unit, so the page only shows what you set. Signal lights are experimental.

### FX
Per channel: EQ (low cut, low, mid, high) and dynamics (compressor/expander with gain reduction meter). FX bus: reverb, echo, chorus with sends from every strip. Main out: limiter and room EQ per speaker set. All of it runs on the computer; the Digiface has no DSP.

### Settings
Channel layout (names, icons, colours, stereo, hide), mixer preferences, devices, remote control (Mackie, OSC), UI size reset to 100%, tooltips on or off, autosave. MIDI filters are on by default.

## Window
Fixed size, resizable from any corner or edge keeping the aspect ratio. Click a wood cheek or the size button to go back to 100%.

## Privacy and updates
Offline only. No telemetry, no update checks. The Settings page has a button that opens the GitHub page where new versions are listed.

## Support
Free and open source (AGPL-3.0). Issues and pull requests on GitHub. If it helps your music, a coffee on Ko-fi (studio87) keeps it coming.
