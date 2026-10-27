# Roadmap: WhiskerMix

Standalone studio hub: TotalMix-style mixer for the RME Digiface USB, MIDI hub, Pulse 16 support and built-in FX.

## Next
- [x] TotalMix to Digiface USB captures done and decoded: gain encoding, channel words, pan, phase, stereo, M/S, main volume, dim, speaker B (see project docs digiface-usb-notes and digiface-captures-labeled; codec in src/backends/digiface). Mute is gain 0 (no USB write seen), solo and width not seen on USB
- [x] Digiface backend: Sync (diff writes), studio layout from TotalMix, dry-run fallback, status in the toolbar
- [ ] Digiface: first run on real hardware (udev rule, bulk interface claim next to the kernel driver, every control)
- [ ] Later: other interface brands and a native audio fallback when the sound card is not there (software mixer only)
- [x] Digiface: handle table, every input and playback strip, every hardware output fader (all-faders capture 2026-10-20)
- [ ] Digiface: still undecoded: meters (interrupt IN 0x83 and bulk IN 0x84), submix selection, the second register set, mono output faders, startup writes
- [ ] App window built from the approved mockups (same layout, look, wood cheeks); today's JUCE pages are functional stand-ins
- [ ] JUCE app on cat-ui: mixer page first (inputs, playbacks, outputs, mute red, solo yellow, per-strip settings panel that opens and closes without folding the channel)
- [x] Channel layout: name, icon, colour, stereo link and hide per strip (Settings page, saved in the workspace). Per-tab layouts not done
- [x] Own FX (the Digiface has no DSP), in code with unit tests: channel EQ and compressor, plate, hall, shimmer and velvet reverb, shimmer delay, pitch shifter; FX Off is a true bypass
- [x] Per-channel FX chain in the engine (Reverb, Echo, Chorus, Phaser, Flanger, M/S, MB Comp, Limiter, Purr Trio, Opto) with Off = bypass, tested; FX panel in the real window (first version, needs a look on screen)
- [ ] Tune the FX by ear against the mockup (reverb size, opto release, MB comp crossovers), oversampling above 2x for Purr Trio
- [x] Routing matrix page and MIDI hub page (live log, filters, named snapshots, anonymised debug report)
- [ ] Aggregation, MIDI-over-MADI or Dante
- [x] Pulse 16 over DIN MIDI: SysEx codec, session and page (clock, sample rate, headphones, levels, routing, presets 1-6, brightness, key click). Built from RemoteFish captures, compiled and rendered headless; not yet tested on real hardware
- [x] Pulse 16 backend: unit id learned from the probe, connection watchdog and meter polling, front panel echoes, backup and restore file, apply-state
- [ ] Pulse 16: real commands for MIDI routing, MADI 96k frame, MADI short frame, BNC termination, delay compensation; meter scale and channel order (needs a capture with a steady tone); decode the undecoded state blocks (see docs/PULSE16-PROTOCOL.md)
- [ ] Pulse 16: test on real hardware with the UM-ONE on Bazzite (Find Pulse, connect, every control)
- [x] Named snapshots (full state copy) and workspace files (.wmix) with autosave
- [x] Mixer: stereo split and join on hardware pairs like the TotalMix Stereo button
- [x] Release: one zip per system with installers, uninstallers and PDF manual (test builds, not tested on real machines)
- [ ] Talkback and listenback, main out recall, MIDI/Mackie and OSC ports

## Every cat plugin
- Draw through `catui::RenderGate` so meters and animations stop when the window is not in front (audio, automation and MIDI keep running)
- Settings page with MIDI filters on by default, selectable preset/backup folders, UI size reset to 100%

## Notes
Ferrofish: the SysEx protocol is not documented but not restricted, and Ferrofish is fine with a Linux control panel.
