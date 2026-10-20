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
### 1. Channel gains: bulk OUT endpoint 0x0B (CONFIRMED for inputs 1 and 2)
32-bit little-endian words: value (16 bits), channel byte, flag byte. Writes every 20 ms while a fader moves.
- Channel byte = 0x48 + 2 x input + side (0 = left output, 1 = right output) for the selected submix. Input 1 writes 0x48 and 0x49, input 2 writes 0x4A and 0x4B. Inputs 3 and up: EXTRAPOLATED.
- Software playback channel 1 uses the same bytes as input 1 in the capture; the two "gesture start" flag words differ (0x0900 / 0x0B01 for playback 1, 0x0800 / 0x0A01 for input 1, 0x0802 / 0x0A02 for input 2). WhiskerMix does not send gesture-start words; they look informational. How playback channels are addressed on their own: UNKNOWN.
- Pan (no pan law): panning right lowers only the left output gain down to 0, panning left lowers only the right one. Centre = both 0x9000.
- Phase L writes 0xF000 to the left bytes, phase R to the right bytes; off writes the gain back.
- Mute, solo, width: no USB write at all. WhiskerMix does mute and solo as gain 0 (the hardware never knows), width in software.
- Stereo link on inputs 1+2: one burst of 12 words: 0x8B50 on 0x48..0x4F, then flag words (flag 0x40) 0x4C=0x0600, 0x4D=0x0401, 0x4E=0x0200, 0x4F=0x0001. Off: 8 words, 0x9000 on 0x48..0x4B and 0 on 0x4C..0x4F.
- M/S processing on inputs 1+2: 12 words: 0x8800 on 0x48, 0x4A, 0x4C..0x4F and 0x4000 on 0x49, 0x4B, same flag words. Off: same 8-word burst as stereo off.
- These bursts are sent only for the first pair. Other pairs: link is software only until a capture shows their burst.
- Main mono: ON = 24 words, 0x8800 on 0x40..0x4F plus flag words 0x48=0x0700, 0x49=0x0501, 0x4A=0x0702, 0x4B=0x0503, 0x4C=0x0300, 0x4D=0x0101, 0x4E=0x0302, 0x4F=0x0103. OFF = 16 words, 0x9000 on 0x40..0x47 and 0 on 0x48..0x4F. Because OFF zeroes the pair gains, WhiskerMix resends them right after.

### 2. Main output volume: vendor control OUT (CONFIRMED)
bmRequestType 0x40, bRequest 0x15, wValue = gain value, wIndex: 0x0100 and 0x0101 = Main A left and right (always written as a pair), 0x0102 and 0x0103 = Main B (Speaker B).
- Dim: Main A = level x 0.1 (exactly -20 dB); off writes the old value back.
- Speaker B on: A pair = 0, B pair = B's level. Off: B pair = 0, A pair = old level back.

## How WhiskerMix uses it
`Sync` (`DigifaceSync.h`) keeps a copy of what was last written per byte and sends only the differences every 20 ms. The first sync sends every input gain; stereo, M/S and mono bursts go out only when they change. Everything runs the same against a `RecordingTransport` (dry run, tests, Windows/macOS builds).

## Still unknown: captures that would help
One action per capture, named clearly (for example `digiface_input3_fader.pcapng`), with a one-line note of what was done:
- Input 3 fader, then input 16 fader (confirms the byte layout for all inputs).
- A software playback channel fader with no input touched (how playbacks are addressed).
- The same input fader after choosing another submix (Mon B, Phones): how the submix is selected.
- Stereo link on inputs 3+4 (the burst for other pairs).
- A hardware output fader (output gains).
- Nothing touched for 10 s with a steady tone on input 1, then on input 2: the meter stream (interrupt IN 0x83 8-byte packets, bulk IN 0x84 512-byte blocks).
- Phones and Mon B volume, talkback, cue.
