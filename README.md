<p align="center"><img src="assets/whiskermix-logo.svg" width="120" alt="WhiskerMix logo"></p>

# WhiskerMix

A TotalMix-style mixer and studio hub for Linux: RME Digiface USB control, MIDI hub, Pulse 16 support and built-in FX.

## Status
Design phase. The interactive mockups work in a browser; the app and the hardware backend are not built yet. Free and open source, donations welcome (link coming).

## What it will do
- Mixer: inputs, playbacks, outputs, mute (red), solo (yellow), per-channel settings panel
- Routing and aggregation matrix
- MIDI hub with filters, Pulse 16 over DIN MIDI
- Own FX (the Digiface has no DSP): reverb, echo, chorus, compressor, limiter, EQ, also on inputs
- Snapshots, talkback, dim, MIDI/Mackie and OSC control

## Docs
- [Manual (mockup guide)](docs/MANUAL.md)
- [Architecture and backend plan](docs/ARCHITECTURE.md)
- [Interactive mockups](docs/README.md)
- [Roadmap](ROADMAP.md)

## Build (planned)
JUCE app on [cat-ui](https://github.com/SpeakingColors/cat-ui), CMake, Linux first.

## License
AGPL-3.0-or-later, see [LICENSE](LICENSE).

Not affiliated with or endorsed by any hardware maker named here. All trademarks belong to their owners.
