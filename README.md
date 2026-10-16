# Akai Fire: custom firmware project

the owner's Akai Fire runs our own firmware: a standalone step sequencer ("ORC FW seq") that keeps its
own time, lights itself and sends MIDI over USB. Built from scratch on top of a full map of the
Fire's hardware, flashed and recovered remotely from the Pi without touching the device.

**Start here:**

| Document | For |
|---|---|
| [`fw/fireseq/USER_GUIDE.md`](fw/fireseq/USER_GUIDE.md) | Playing the sequencer: every control |
| [`FIRE_HARDWARE_MAP.md`](FIRE_HARDWARE_MAP.md) | The Fire's chip, pins, buttons, LEDs, knobs, screen, boot process |
| [`fw/DEVELOPER.md`](fw/DEVELOPER.md) | Building, flashing, recovery, code layout, storage format, tests |
| [`fw/VELOCITY_NOTES.md`](fw/VELOCITY_NOTES.md) | The unsolved pad-velocity investigation: what was tried, best next leads |

## Status (2026-10-14)

- **Running on the Fire: v0.17b** (`ORCBUILD=9e1e92d5`): the sequencer + the BROWSER menu, light shows on the pads
  (the ORC's shows ported, TRACKS, PI VU bars from the Pi's audio, scrolling TEXT), picture-style OLED screens, a SysEx
  host protocol (`F0 7D 'ORC' ...`, commands 00-07, USER_GUIDE), a crash recorder that restarts the Fire itself and
  names the build / stack state of a crash, and the 2026-10-13 review fixes (each measured on the Fire with
  `fw/fireseq/fire_live_test.py`).
- **One crash seen on v0.17 (2026-10-13 22:09), not found yet:** a smashed return address while the PI VU bars ran
  (USER_GUIDE "Crashes"). v0.17b adds stack canaries + stack/build data to the record so the next one names itself.
- v0.12-v0.14 (2026-10-07): review fixes, faders on PERFORM, 32 patterns with colours. History below.

## Status (2026-10-06)

- **Running on the Fire:** v0.8 = sequencer (v0.7 features: 16 tracks x 128 steps with per-track lengths, 8 patterns,
  song mode, accents, swing, ratchets, probability, per-track notes/channels, MIDI clock in and out,
  auto-save, all confirmed by the owner) + live play (below).
- **v0.8 flashed 2026-10-06, not yet played:** live play. NOTE = fourths keyboard; DRUM = four 4x4
  grids, each with its own channel + note map (chromatic / MPC), for the ORC, MPC1000, SP-404 etc.;
  REC records hits into the pattern. Fixed velocity 100, SHIFT = 127. Pad velocity still open (best route: ST-Link reading Akai's ADC store live, see `fw/VELOCITY_NOTES.md`).
- **v0.9 flashed 2026-10-06, not yet played:** 24 tracks = T1-T16 drum + M1-M8 melodic (a note per
  step); NOTE mode plays/records a melodic track; per-step gate.
- **v0.10 flashed 2026-10-06:** sparse step storage (RAM ~49 -> ~31 KB of 64), smaller saves,
  moving around no longer triggers saves. the owner's saves converted and verified byte for byte on the
  Fire. Timing under saves MEASURED 2026-10-07 (180 BPM, the owner editing): no difference from a
  no-save control run (2.1% vs 2.3% of notes > 2 ms off their neighbours = the Pi's Python noise).
- **v0.11 flashed 2026-10-07:** parameter locks, conditions (A:B, 1st, FILL on PERFORM), TIE / SLIDE,
  chords (+ chord recording); two step-knob pages (KNOB MODE); self-describing save records.
  The Fire's own v0.10 -> v0.11 conversion verified on a readback: identical except the one step
  the owner had since set to SLIDE (P3 T3 s1). the owner: "This is awesome".
- **Hardware fully mapped and measured**, except pad velocity, knob touch and two unexplained pins.
- **Akai's firmware** can be put back from the Pi in about a minute (`fire_flash.sh stock`), verified
  against a full backup of the chip.

## How we got here (2026-10-05 / 06)

1. Dumped the chip through ST's update mode (button combo) and backed it up twice.
2. Looked inside: the chip (STM32F401RC), the flash layout, Akai's fonts and boot logos, the boot
   check (just an end marker, no checksum).
3. Found Akai's "reboot to update mode" SysEx (0x72): from then on, everything was done remotely.
4. Emulated Akai's firmware to map the pins and capture the screen's start-up sequence.
5. Built firmwares step by step: boomerang (boot path) -> hello (USB MIDI) -> oled (screen) ->
   probe (mapped every button, pad, knob and LED with the owner's help) -> the sequencer.

## What's in this repo (and what isn't)

This repo holds **only our own work**: `fw/` (firmware
source, test and measurement scripts) and these docs. The author's working folder also has
things that are deliberately **not** published (`.gitignore` is an allow-list):

- `fire_flash_v0.21_a.bin`, `_b.bin`, `optbytes.bin`, `otp.bin`: backups of the stock chip.
  **Akai's copyrighted code.** Also on the Pi in `~/fire_fw/`.
- `patterns_backup_v03.bin`: the pattern area before the v0.4 format change.
- `fire_fonts_and_logos.png`: the graphics stored in Akai's firmware.
- `tools/`: the emulator and analysis scripts that run Akai's firmware.

The USB stack (TinyUSB, MIT) is not vendored: see `fw/DEVELOPER.md` for fetching it.
