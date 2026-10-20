# Roadmap: WhiskerMix

Standalone studio hub: TotalMix-style mixer for the RME Digiface USB, MIDI hub, Pulse 16 support and built-in FX.

## Next
- [ ] Capture TotalMix to Digiface USB traffic (USBPcap) to learn the control messages; the Linux kernel only covers audio plus sync/format/rate, not the matrix mixer, output gain or meters
- [ ] JUCE app on cat-ui: mixer page first (inputs, playbacks, outputs, mute red, solo yellow, per-strip settings panel that opens and closes without folding the channel)
- [ ] Channel layout like TotalMix: name, color, hide per tab
- [ ] Own FX (the Digiface has no DSP): reverb, echo, chorus, compressor, limiter, EQ, on inputs too
- [ ] Routing and aggregation page, MIDI hub, MIDI-over-MADI or Dante
- [x] Pulse 16 over DIN MIDI: SysEx codec, session and page (clock, sample rate, headphones, levels, routing, presets 1-6, brightness, key click). Built from RemoteFish captures, compiled and rendered headless; not yet tested on real hardware
- [ ] Pulse 16: real commands for MIDI routing, MADI 96k frame, MADI short frame, BNC termination, delay compensation; meters; decode the undecoded state blocks (see docs/PULSE16-PROTOCOL.md)
- [ ] Snapshots and workspaces, talkback and listenback, main out recall and dim, MIDI/Mackie and OSC ports

## Every cat plugin
- Draw through `catui::RenderGate` so meters and animations stop when the window is not in front (audio, automation and MIDI keep running)
- Settings page with MIDI filters on by default, selectable preset/backup folders, UI size reset to 100%

## Notes
Ferrofish: the SysEx protocol is not documented but not restricted, and Ferrofish is fine with a Linux control panel.
