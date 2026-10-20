# Manual (mockups)

Open a page from `docs/mockups/` in a browser with `support.js` beside it. Nothing makes sound yet.

## WhiskerMix
- Faders and pan respond; mute is red, solo is yellow.
- Wrench icon on a strip opens that channel's settings panel; the x closes only the panel, the channel stays.

## WhiskerRouting
Matrix of inputs, playbacks and outputs. Click a cell to toggle.

## WhiskerMIDI
Hub with port list and filters. Filters are on by default.

## Pulse 16 page
Controls a Ferrofish Pulse 16 over a MIDI interface (DIN MIDI SysEx), no RemoteFish needed.
1. Connect the Pulse MIDI in and out to your MIDI interface. Close RemoteFish (it holds the port).
2. Press Find Pulse, or pick the in and out ports and press Connect. The page then shows what the unit reports.
3. Clock: only one device may be master. With the Pulse on Master, set your audio interface to follow its ADAT input.
4. The Pulse screen can show a sample rate it is not running. Use Resend to send the rate again.
5. Save overwrites a preset stored in the Pulse and asks first.
Brightness and key click are not reported by the unit, so the page only shows what you set. Protocol notes: docs/PULSE16-PROTOCOL.md.

## WhiskerFX
Built-in effect chains (reverb, echo, chorus, compressor, limiter, EQ).

## WhiskerSettings
MIDI filters, preset/backup folders, UI size reset to 100%.

Animated meters pause when the page is hidden or not in front.
