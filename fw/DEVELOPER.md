# Fire custom firmware: developer guide

How the custom Akai Fire firmware is built, flashed, recovered and structured. Hardware facts live
in `../FIRE_HARDWARE_MAP.md`; the sequencer's controls in `fireseq/USER_GUIDE.md`.

## Folders

| Folder | What |
|---|---|
| `boomerang/` | First test: runs, waits, reboots into update mode. Proved the boot path. |
| `hello/` | USB MIDI device + echo + update-mode command. Proved USB. |
| `oled/` | hello + the screen. Proved the SSD1306 driver. |
| `probe/` | Hardware-mapping firmware (chain bits, LED walk, analog channels, velocity experiments) and the Pi scripts used with it. |
| `fireseq/` | **The sequencer** (the real firmware). |
| `fire_flash.sh`, `watch_usb.sh` | Flashing and boot-watching scripts (run on the Pi). |
| `../tools/` | Emulator and analysis scripts for Akai's firmware (PC). |

Nothing here belongs in the public `orc/` repo yet: `../` also holds Akai's firmware dumps.

## Building (on the Pi)

Toolchain: Debian's `gcc-arm-none-eabi` 14.2 + newlib, installed on the Pi. USB stack:
[TinyUSB](https://github.com/hathach/tinyusb) (MIT) in `~/fire_fw/deps/tinyusb`, with its STM32F4
dependencies fetched by `python3 tools/get_deps.py stm32f4` (ST's CMSIS device headers and startup
file, ARM's CMSIS core).

```bash
cd ~/fire_fw/fw/fireseq && make
```

`make` produces `fireseq.bin`: **exactly 98304 bytes** (the app area, 0x08008000-0x0801FFFF) with
`EndOfFlashToken` in its last 16 bytes. The Makefile refuses to finish otherwise.

- `fire_app.ld` places the app at 0x08008000 and the token section at 0x0801FFF0.
- `token.c` holds the token; `objcopy --gap-fill 0xFF` pads the space in between.
- The program sets `SCB->VTOR = 0x08008000` first thing (Akai's bootloader owns 0x08000000).

The PC mirror is `fire_firmware/fw/` on the owner's desktop; copy sources to the Pi with `scp`
(`.sh` files need `sed -i 's/\r$//'` after copying from Windows).

## Flashing (on the Pi, no buttons needed)

```bash
~/fire_fw/fw/fire_flash.sh dfu                 # running Fire (ours or Akai's) -> update mode
~/fire_fw/fw/fire_flash.sh app <file.bin>      # write the 96 KB app area, verify by reading the chip back
~/fire_fw/fw/fire_flash.sh boot [seconds]      # leave update mode through Akai's bootloader, log USB changes
~/fire_fw/fw/fire_flash.sh stock               # put Akai's app back, verify against the backup
```

- `dfu` sends SysEx `F0 47 7F 43 72 00 00 F7` to a port named "FL STUDIO FIRE" or "ORC FW ...".
  Both Akai's firmware and ours answer it.
- `app` and `stock` **never write sectors 0, 1 or 5** (bootloader, Akai's settings, our saved
  patterns). The readback verify checks sectors 0-4 and reports how full sector 5 is.
- Backup of the whole chip: `~/fire_fw/fire_flash_v0.21_a.bin` (md5 `efede8a5...`).

## Recovery

In order of preference:
1. **`fire_flash.sh dfu`**, if the firmware still speaks MIDI.
2. **Watchdog / crash recorder:** our firmwares start a ~2 s watchdog first thing. Since 2026-10-13 (fireseq) a
   freeze or a fault is **recorded** in the RTC backup registers and the app **restarts itself**; only a crash loop
   (3 crashes in a row, each within 30 s of booting) reboots into update mode. Read the record with
   `host_crash.py --elf <the ELF of that build>` (USER_GUIDE "Crashes"). The older test firmwares still reboot
   straight into update mode.
3. **A missing token:** an interrupted or broken app write leaves no `EndOfFlashToken`, so the
   bootloader goes to update mode by itself.
4. **The button combo:** hold STEP + NOTE + DRUM + PERFORM while plugging in.
5. **ST-Link** on the SWD pins (the owner has one).

Akai's bootloader (sector 0) is never written, so 1-4 always remain available.

## Rules for any firmware on the Fire

- **Never put "FIRE" in the USB product name.** Until 2026-10-13 the ORC's brain claimed a Fire by the substring
  "FIRE"; it now uses one name rule (`orc1_config.is_fire_port`: "FL STUDIO FIRE" or "ORC FW host"), but keep
  sequencer builds named "ORC FW seq ..." so they stay plain MIDI inputs.
- **Never send Akai's SysEx cmd 0x78 / 0x79** to a stock Fire (they write the settings sector).
- Keep the safety prelude at the top of `main()`: VTOR, the watchdog-flag check (record it; a crash loop -> update
  mode), watchdog start. Keep the fault handlers recording + restarting (a loop still ends in update mode).
- **Flash only an image whose `ORCBUILD=` tag you know**, take a full-chip backup first, and keep each flashed
  `.bin` + `.elf` on the Pi (`~/fire_fw/fireseq_<step>_<date>.*`): a crash address means nothing without its ELF.
- Test new boot or storage code in the emulator or host tests before it touches the hardware.

## The sequencer's code (`fireseq/`)

| File | What |
|---|---|
| `main.c` | Sequencer: patterns, playback timing, controls, screen, MIDI, saving |
| `fire_hw.c/.h` | Hardware layer: the chain refreshed from a 2 kHz timer interrupt (TIM4), LED brightness by blinking (8 levels), 1 ms debounce and an input event queue, SELECT decoding, knob angles |
| `fire_map.h` | The measured input/LED map |
| `oled.c/.h` | SSD1306 init and DMA frame push (non-blocking) |
| `draw.c/.h`, `font5x7.h` | Framebuffer drawing and the font |
| `store2.c/.h` | **Current storage** (see below) |
| `store.c/.h` | The v0.2/v0.3 storage format, kept only to convert old saves |
| `store_hw.c` | Flash erase/program for sector 5 only |
| `steps.c/.h` | **Sparse step storage** (v0.10): only lit steps take RAM |
| `play.c/.h` | What a step sends: locks, chords, TIE/SLIDE, conditions (v0.11) |
| `lights.c/.h`, `lights_tab.h` | Pad light shows (the ORC's shows ported, TRACKS, PI VU bars, TEXT), no libm |
| `screens.c/.h` | Picture-style OLED screens (header, step dots, faders, menu, popups) |
| `crash.c/.h` | Crash recorder logic: restart-vs-update-mode decision, the host reply (v2: build, SP, stack low point) |
| `ctl.c/.h` | The control decisions from the 2026-10-13 review as pure functions (Show list, host parsing, idle start, frame timer, PI VU fallback, CYCLE, lazy save) |
| `*_test.c`, `wiring_test.py`, `*_mutate.py` | Host tests + mutation scripts (see Testing) |
| `fire_live_test.py` | Behaviour checks ON the Fire through host status command 07 |
| `host_vu.py` (service `orc1-firevu`), `host_text.py`, `host_crash.py` | Pi-side senders / readers of the host protocol |
| `usb_descriptors.c`, `tusb_config.h` | USB MIDI device (VID/PID 1209:0001, pid.codes test ID) |

Timers: TIM2 = 1 MHz free-running step clock, TIM4 = 2 kHz chain refresh, SysTick = 1 ms.

### Playback timing

- Step boundaries run on a straight grid (`grid_us += step length`). Swing only delays the odd steps'
  **notes**; it never moves the grid or the clock.
- MIDI clock out: 6 ticks per step, each anchored to the step's start (`step_start + k x step / 6`),
  so the clock can't drift against the notes (an earlier version added a rounded step/6 repeatedly
  and drifted ~2 us per step). Measured: 90.01 BPM, note-to-tick offset steady to 0.01 ms over 40 s.
- Each track's position = (steps played) mod (track length).
- **All of a step's MIDI messages go to USB in one write.** One write per note made the first go
  out alone and the rest a USB frame later (2-6 ms spread, measured).

### Steps in RAM (`steps.c`, v0.10)

Only lit steps take memory. Each (pattern, track) has a 128-bit map of which steps are lit, and its
lit steps' data packed in step order in one **shared 16 KB pool** (drum tracks 1 byte per lit step =
the step byte; melodic tracks 2 bytes = step byte + note). Blocks sit in (pattern, track) order, so a
pattern's data is one contiguous run. Lookups count the set bits before the step. A full pool refuses
the new step (screen: FULL) and changes nothing. RAM: ~27 KB bss + 4 KB stack of 64 KB (v0.9 with
full-size patterns: ~45 KB).

- Step byte: bits 0-1 level (off / soft / normal / accent), 2-3 probability, 4-5 ratchet, 6-7 gate.
  Level 0 = off = the step and its settings are removed.

### Storage (`store2.c`)

A variable-length append log in sector 5 (128 KB):

- Records start on 256-byte boundaries: header (magic "ORC3", kind, length, sequence number, CRC,
  commit word), then the payload.
- **Write order (v0.10): header with CRC + commit blank, payload, CRC, then commit = 0.** A record
  only counts if committed and its CRC matches, so a save cut off at any point never loads.
- Kinds: 0 = global settings (128 bytes; tempo, swing, mutes, track notes/channels, song, grids ...),
  **9-16 = patterns, sparse** (24 lengths + 24 x 16-byte maps + the pattern's pool data; ~410 bytes
  for a typical pattern). 1-8 = the old full-size patterns (v0.4-v0.9, 2064 / 4120 bytes): converted
  at boot by `steps_convert_full`, read in place from flash, and dropped at the next compaction.
  Kinds the firmware doesn't know are skipped whole.
- Saved structs only ever **grow at the end**; zero always means "default", so older saves load.
- **Saving happens in the background, from the live data (no copy):** `store2_begin_src` claims the
  space, `store2_step` writes 8 words per main-loop pass (never within 1.5 ms of a due step),
  reading the payload through a callback, CRC'ing it as it goes and reading each word back. An edit
  to the record being written calls `store2_cancel`: it stays uncommitted (never counts) and is
  redone 3 s after the last edit. (A blocking 2 KB save once stalled playback ~30-50 ms.)
- **Edits vs moving around:** `edited_*` starts the 3 s save timer; `touched_global` (pattern choice,
  mutes, octave, keyboard track, song on/off) only marks the settings dirty, so they ride along with
  the next save. The settings record is never written if it equals the last one written (`G_saved`).
- When the area is full, it is erased and rewritten with the current state: **only while stopped**
  (the chip freezes ~1-2 s during the erase; the watchdog is stretched to ~8 s for it). With sparse
  records the whole state is ~4.3 KB, so this is rare.
- Conversions: v0.4 converted the v0.2/v0.3 format (backup `../patterns_backup_v03.bin`); v0.10
  converted full-size patterns to sparse, verified byte for byte on the owner's real saves (Pi:
  `~/fire_fw/sector5_before_v010.bin`, `sector5_after_v010.bin`).

## Testing

On the Pi, with plain `gcc` (simulated NOR flash, including power cuts):

```bash
cd ~/fire_fw/fw/fireseq
F="-O2 -Wall -fsanitize=bounds -fsanitize-undefined-trap-on-error"     # array-bounds traps on
gcc $F -o steps_test steps_test.c steps.c && ./steps_test                      # 15 checks
gcc $F -o store2_test store2_test.c store2.c store.c && ./store2_test          # 34 checks
gcc $F -o convert_test convert_test.c steps.c store2.c && ./convert_test ~/fire_fw/sector5_before_v010.bin   # 37 (45 on an area the Fire already re-saved)
gcc -O2 -Wall -o store_test store_test.c store.c && ./store_test               # 68 checks (old format)
```

Since v0.15-v0.17 also (counts are self-checked: a suite whose count drifts fails):
```bash
gcc -O1 -Wall -Wextra -I. ctl_test.c ctl.c lights.c -lm -o ctl_test && ./ctl_test             # 73
gcc -O1 -Wall -Wextra -I. lights_test.c lights.c -lm -o lights_test && ./lights_test          # 1441
./lights_test dump | python3 lights_vs_orc.py ~/orc/orc1/orc1_fire.py                         # 4928 pads, 0 differ
gcc -O1 -Wall -Wextra -I. screens_test.c screens.c draw.c -o screens_test && ./screens_test    # 14
gcc -O1 -Wall -Wextra -I. crash_test.c crash.c -o crash_test && ./crash_test                  # 32
gcc -O1 -Wall -Wextra -I. play_test.c play.c -o play_test && ./play_test                      # 41
python3 wiring_test.py                      # 23: main.c really calls the tested rules
python3 -B host_vu_test.py                  # 113
python3 wiring_mutate.py . ; python3 lights_mutate.py ~/orc/orc1/orc1_fire.py ; python3 -B host_vu_mutate.py   # all red
python3 fire_live_test.py                   # ON THE FIRE (idle, ~7 min): every review fix, measured via status 07
```

`steps_test` mirrors 40,000 random edits / copies / clears / record round trips on a plain dense
model, fills the pool, and feeds malformed records. `convert_test` converts a REAL save area read
off the Fire (`dfu-util -a 0 -s 0x08020000:131072 -U file`, in update mode; read-only) and checks every
step. Mutation-checked: each suite goes red for planted bugs (gap bookkeeping, rank, stale offsets in
a copy, missing validation, cancel ignored, CRC over the wrong bytes, unknown kinds indexed: the last
only under the bounds sanitizer).

`store2_test` cuts the power at every word of a pattern save and checks the previous version still
loads; it also covers a full area, compaction, a cut-off header and reading old-format saves.
Mutation-checked: ignoring the commit word, or committing before the payload, both fail it.

Timing checks, run on the Pi while the Fire plays (`fireseq/`):
- `hitch_test.py [s]`: flags steps more than 2 ms off the grid, and how spread a step's notes are.
  Python on the Pi adds its own 2-4 ms blips (a no-save control run shows them too).
- `clock_lock_test.py`, `timing_log.py`: clock rate and clock-to-note lock.

Emulator (`../tools/`, PC, Unicorn):
- `emu_boot.py <app.bin> [--tick ADDR] [--wdg]`: runs Akai's bootloader -> our app -> resets on the
  real image. `--wdg` pretends the last reset was the watchdog (must end in update mode).
- `emu_init.py`, `emu_stock.py`: Akai's firmware start-up, with simulated DMA interrupts.

## Status and next steps

- v0.6 (2026-10-06): ratchets and probability confirmed by the owner. GM drum names were dropped on his
  call (a note only means "cowbell" if it lands on a GM kit); tracks are T1-T16 with musical note names.
- v0.7: song mode. v0.8 (2026-10-06): live play (NOTE keyboard, DRUM = four 4x4 grids with their own
  channel/note map, REC into the pattern), fixed velocity. Saved settings live in `global_t`'s former
  `reserved2` bytes (zero = default), so v0.7 saves load unchanged.
- v0.9: melodic tracks M1-M8 appended to `pattern_t` (`len_m`, `step_m`, `note_m`; old records
  zero-fill), so a pattern record is 4120 B (`S2_MAX_RECORD` 4352). Step byte bits 6-7 = gate.
  `steprow()`/`lenp()` hide the drum/melodic split; `_Static_assert`s pin the record sizes.
  RAM ~45 KB bss + 4 KB stack of 64 KB.
- v0.10: sparse steps (`steps.c`), live-data saves with cancel, position changes don't trigger saves,
  identical settings aren't re-written, bitwise CRC (no 1 KB table). RAM ~31 of 64 KB incl. stack.
  Timing with saves running (the owner editing, 120 s at 180 BPM) vs a hands-off control (60 s):
  notes > 2 ms off their 20 neighbours 2.1% vs 2.3%, worst 3.7 vs 5.8 ms: saves add nothing measurable.
  (Judge each note against its neighbours: a stop/start resets the grid and shifts all later notes.)
- v0.11: `play.c` (what a step sends: locks, chords, TIE/SLIDE hand-over, conditions; host test
  `play_test.c`, 28 checks), steps.c v2 entries (drum 3 B, melodic 6 B, pool 20 KB) in self-describing
  records (kinds 17-24: format byte + entry sizes; fields only appended, so no future migration), v0.10
  records (9-16) converted at boot. TX FIFO 2048, batch 768, offs/ratchet queues 128. RAM ~40/64 KB.
- v0.12-v0.14 (2026-10-07): review fixes, faders, 32 patterns + colours.
- v0.15 (2026-10-13): menu, light shows, screens, host protocol, crash recorder. ("v0.16" = the parked HOST-mode
  firmware, branch `host-mode`.)
- v0.17 / v0.17b (2026-10-14): the review fixes wired in (`ctl.c`), host status 07, build id, stack canaries
  (`-fstack-protector-strong`), stack high-water mark. RAM: 49 KB bss+data, ~16 KB left for the stack, of which
  normal use measured 1.3 KB. Flash: ~57.5 of 96 KB.
- v0.29 (2026-10-24): **32 tracks** (storage 0-15 T1-T16, 16-23 M1-M8, 24-31 T17-T24 with drum entries; the screen
  order is `vtrack()`: T1-T24, M1-M8), pattern records **v3** (byte 3 = the track count, so the next growth needs no
  new format; v2 records load with tracks 24-31 empty; `convert_test` runs on the owner's real v0.28 area in
  `run_tests.sh`). `global_t` 380 -> 418 -> 566 (x_note / x_chan / x_mute / x_lock_cc / x_trk_lvl for T17-T24, `cc_trk`
  bits); the old per-track arrays stay 24 long (`TRACKS_V28`): reach them only through `lock_cc_p()` /
  `trk_lvl_p()` / `tnote()` / `set_tnote()` / `tchan()` / `set_tchan()`. **CC tracks**, **undo** (2 x 1536 B
  record buffers, gesture model in main.c), **roller** (`ctl_roll_*`, tested in ctl_test; play_step plays
  `roll_g(t, g)` while gstep runs on), **16 levels** (DRUM group map 2), **knob banks per section** (STEP keeps
  `kb_*`, NOTE / DRUM / MPC remote = appended `kbx_*`, copied from STEP once; `kctx()`). `global_t` ends at 566.
  RAM: 60.3 KB bss+data (map 16 KB, pool 20 KB, undo 3 KB), ~5.2 KB left for the stack. Flash ~73 of 96 KB.
- Open: the 2026-10-13 22:09 crash (USER_GUIDE "Crashes").
- Next candidates: pad velocity (research log and leads: `VELOCITY_NOTES.md`), knob touch.
