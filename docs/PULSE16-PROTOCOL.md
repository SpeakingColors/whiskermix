# Ferrofish Pulse 16 DX: SysEx protocol (reverse engineered)

Learned from USB captures of RemoteFish 2.1 talking to a Pulse 16 DX (ADAT only, firmware v5.04) through a class compliant USB MIDI interface. Code: `src/backends/pulse16/`, tests: `tests/pulse16_test.cpp`. Not affiliated with or endorsed by Ferrofish.

Status words: CONFIRMED = matched the on-screen action in a clean capture. PROBABLE = seen once, not proven. UNKNOWN = not decoded.

## Frame
`F0 00 11 22 <id: 0B 3C 45 4F 0B> <cmd> <sub> <data...> F7`. The id bytes were identical in every capture from one unit. They may differ per unit (UNVERIFIED), so a future version should read them from the probe answer.

## Connect
1. Probe `F0 00 11 22 00 00 00 00 00 02 00 F7`, answer `<id> 03 00`.
2. `<id> 02 01`. The unit answers with its whole state: a 51 byte block (`03 00`), a 47 byte block (`01 00`), gains (`50 00`), routing (`51 00`), and two blocks `12 00` (140 bytes) and `13 00` (76 bytes) that are not decoded.
3. RemoteFish then sends `01 04 01`, which selects headphone source Analog Out. WhiskerMix does not.
4. Meter poll `02 04` about every 0.22 s, answer `04 00 01` plus 32 bytes (UNKNOWN).

Every command is echoed back unchanged.

## Commands (CONFIRMED unless noted)
| Command | Meaning |
| --- | --- |
| `10 <idx> <vv>` | idx 00..0F input gain 1..16, vv = 0..28 dB. idx 10..1F output 1..16, vv = 28 + dB (1C = 0 dB). |
| `11 <row> <col>` | routing: row = output block, col = source block, both 0-based in screen order. Col 0 ANALOG IN 1-8, 1 ANALOG IN 9-16, 2 ADAT IN 1-8, 3 ADAT IN 9-16, 4 ADAT IN 17-24, 5 ADAT IN 25-32, 6..13 MADI. |
| `01 01 vv` | clock source: 00 Master, 01 ADAT, 02 BNC. |
| `01 02 vv` | master sync frequency: 00 32k, 01 44.1k, 02 48k, 03 64k, 04 88.2k, 05 96k, 06 128k, 07 176.4k, 08 192k. |
| `01 03 vv` | headphone volume: dB = vv - 79, vv 0 = Off. |
| `01 04 vv` | headphone source: 00 Analog In, 01 Analog Out, 02 ADAT IN, 03 ADAT OUT, 04 MADI IN, 05 MADI OUT, 06 DANTE IN, 07 DANTE OUT. |
| `01 05 vv` | headphone channel (source Analog Out). Stereo: 0x40 + (first channel - 1) / 2. Mono: channel - 1. |
| `01 0e vv` | display brightness 00 darkest .. 0F brightest. |
| `01 0d vv` | key click 00 off, 01 on (PROBABLE). |
| `01 00 vv` | language, 00 = English (PROBABLE). |
| `05 00 06 vv` | headphone mute (PROBABLE). |
| `02 07 <n-1>` | save preset n (1..6), ack `34`. Overwrites the slot. |
| `02 06 <n-1> 07` | load preset n (1..6), ack `35`, then the state blocks. |

Headphone volume is not part of a preset.

## State blocks
- `50 00` + 16 input gains (dB) + 16 output levels (28 + dB).
- `51 00` + 22 bytes: source column per output block row (same numbering as `11`). Rows 0..5 analog and ADAT outs, 6..13 MADI, 14..21 unidentified (probably Dante).

## Warnings
- On the ADAT only DX, RemoteFish sent `01 02 <index>` (the sample rate command) for its MIDI routing, MADI 96k frame, MADI short frame and BNC termination controls. One stray command left a unit running at 44.1 kHz while the screen said 48 kHz. Do not map those controls to `01 02`; their real commands are UNKNOWN.
- The unit's screen can show a rate it is not running, and RemoteFish sends nothing when you re-pick the value it shows. Send the rate explicitly.
- Clock: only one device may be master. With nothing on the BNC input and the clock set to BNC or ADAT, the Pulse shows "no lock".
- Not captured: delay compensation (`01 10 00/01` PROBABLE), MADI/Dante specifics, meters.
