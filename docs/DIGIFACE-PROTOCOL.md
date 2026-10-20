# RME Digiface USB: hardware mixer protocol (reverse engineered)

Learned from USB captures of the vendor mixer (TotalMix FX) talking to a Digiface USB (2a39:3fa0) on Windows, one action per capture. Code: `src/backends/digiface/`, tests: `tests/digiface_codec_test.cpp`, `tests/digiface_sync_test.cpp`. Not affiliated with or endorsed by RME.

Status words: CONFIRMED = matched the on-screen action in a labeled capture. EXTRAPOLATED = follows the confirmed pattern but no capture shows it. UNKNOWN = not decoded.

## What the Linux kernel already does
Audio, clock and sample rate (the kernel quirk added in 2024). Not the matrix mixer, output gains or meters. WhiskerMix talks to the device for those with libusb, without detaching the kernel audio driver: audio keeps running. `packaging/udev/70-whiskermix-digiface.rules` gives the user access without root.

## Gain value (CONFIRMED)
16 bits, linear amplitude in two ranges:
- value < 0x8000: amplitude = value / 32768. 0 = off (-inf).
- value >= 0x8000: amplitude = (value - 0x8000) / 4096. 0x9000 = 0 dB, 0xA000 = +6.02 dB (fader top), 0x8800 = -6 dB.
- The vendor mixer truncates: a typed -3 dB is written as 0x8B53. `encodeGain()` does the same.
- 0xF000 is not a gain: it is the phase invert marker.

## Two write paths
### 1. Strip gains: bulk OUT endpoint 0x0B, a handle table (CONFIRMED by the "all faders" capture, 2026-10-20)
32-bit little-endian words: value (16 bits), handle byte, flag byte. Writes every 20 ms while a fader moves.
- The device keeps 16 handles 0x40..0x4F. A word with flag 0x40 binds a handle to a signal path:
  value = (reg << 9) | (playback << 8) | hardwareChannel. Handles are reused freely by the vendor mixer: 0x48..0x4B for the input strip being moved, 0x40..0x43 or 0x44..0x47 for playback strips.
- Every gesture starts with one 8-word block: for each of the strip's four handles a bind word and a gain word. A stereo strip binds its left channel to reg 2 and 0 and its right channel to reg 3 and 1; a mono strip binds its one channel to all four. Hardware channels are 0-based: inputs ADAT 1..16 = 0..15, playbacks 1..16 = 0..15, the phones playback pair = 32/33. Example SYNTH (inputs 1+2): 0x48=0x0400, 0x49=0x0601, 0x4A=0x0000, 0x4B=0x0201; B1 (input 3, mono): 0x0402, 0x0602, 0x0002, 0x0202; COMPUTER (playback 1+2): 0x0500, 0x0701, 0x0100, 0x0301; PH (playback 33+34): 0x0520, 0x0721, 0x0120, 0x0321.
- After that block, gain words (flag 0) on the same four handles follow for the rest of the gesture.
- reg 0 and 2 feed the left output of the submix, reg 1 and 3 the right one (pan and phase act on them). Why there are two register sets (0/1 and 2/3), always written with the same gain, is UNKNOWN.
- Which submix the paths belong to (Main here) is not in these words: UNKNOWN until a capture with another submix selected.
- Pan (no pan law): panning right lowers the left output paths down to 0, panning left the right ones. Centre = 0x9000 on all four.
- Phase L writes 0xF000 to the left paths, phase R to the right ones; off writes the gain back.
- Mute, solo, width: no USB write at all. WhiskerMix does mute and solo as gain 0 (the hardware never knows), width in software.
- Stereo link on inputs 1+2 (captured on handles 0x48..0x4F): straight paths at 0x8B50 (-3 dB) and four cross paths (L -> right out, R -> left out) bound on the next four handles at -3 dB. Off: straight paths back to 0x9000, cross paths to 0. WhiskerMix rebuilds the same burst for any strip from its hardware channels (EXTRAPOLATED beyond inputs 1+2).
- M/S processing: same burst with 0x8800 on the straight and cross paths and 0x4000 on the right-input paths (0x4000 is its own marker, not decoded). Off: same as stereo off.
- Main mono: ON = 24 words, 0x8800 on 0x40..0x4F plus binds 0x48=0x0700, 0x49=0x0501, 0x4A=0x0702, 0x4B=0x0503, 0x4C=0x0300, 0x4D=0x0101, 0x4E=0x0302, 0x4F=0x0103 (playbacks 1..4 cross-mixed at -6 dB). OFF = 16 words, 0x9000 on 0x40..0x47 and 0 on 0x48..0x4F. WhiskerMix sends every strip block again after either burst.

### 2. Hardware output volume: vendor control OUT (CONFIRMED)
bmRequestType 0x40, bRequest 0x15, wValue = gain value, wIndex = 0x0100 + hardware output channel (0-based). A stereo strip writes both channels with the same value: ADAT 5/6 = 0x0104/0x0105, ADAT 9/10 = 0x0108/0x0109, phones 33/34 = 0x0120/0x0121, Main (ADAT 1/2) = 0x0100/0x0101, Speaker B (ADAT 3/4) = 0x0102/0x0103.
- Dim: Main = level x 0.1 (exactly -20 dB); off writes the old value back.
- Speaker B on: Main pair = 0, Speaker B pair = its level. Off: Speaker B pair = 0, Main pair = old level back. With the two linked and Speaker B off, moving Speaker B's fader writes Main.
- The two mono outputs (INSERT = ADAT 15, PREAMP = ADAT 16) wrote only zeros on 0x010E / 0x010F during their fader sweep: UNKNOWN why (mono outputs may use another path).

## How WhiskerMix uses it
`Sync` (`DigifaceSync.h`) keeps what was last written per strip and per output and sends only the differences every 20 ms: a changed strip costs one 8-word block (bind + gains), a changed output one or two control writes. The first sync sends every strip and output; stereo, M/S and mono bursts go out only when a mode changes after that. Everything runs the same against a `RecordingTransport` (dry run, tests, Windows/macOS builds).

## Still unknown: captures that would help
One action per capture, named clearly, with a one-line note of what was done (Wireshark filter: `usb.device_address == 6 && usb.transfer_type != 0x00`):
- Every pan from left to right, strip by strip (confirms the left/right paths on every strip).
- The same input fader after choosing another submix (Mon B, Phones): how the submix is selected.
- Stereo link on inputs 3+4, and on a playback pair.
- A mono hardware output fader alone (INSERT or PREAMP): what carries its level.
- Mute, solo, cue and talkback, one each (expected: nothing on USB, worth confirming).
- Nothing touched for 10 s with a steady tone on input 1, then on input 2: the meter stream (interrupt IN 0x83 8-byte packets, bulk IN 0x84 512-byte blocks).
- Start the capture before opening the vendor mixer: what it writes at startup.
