# ORC Fire sequencer: user guide

Custom firmware that turns the Akai Fire into a **standalone step sequencer**. It keeps its own time,
lights itself and sends MIDI notes over USB to whatever it's plugged into (the ORC, a computer, a
USB host). On USB it appears as **"ORC FW seq"**.

Version described: **v0.21** (2026-10-14 night: ORC surface pads talk to the ORC by SysEx `0C`, no MIDI channel). **v0.20** (2026-10-14: knob banks on KNOB MODE, BROWSER menu pages Playing / Knobs / Display / Shortcuts / ORC surface, beat-mark settings). **v0.19**: ORC surface mode. **v0.18**: idle lights off by default. **v0.17** (2026-10-14: the 2026-10-13 review fixes wired in + host status 07). **v0.15** (2026-10-13; lights, menu, screens, PI VU and the heat looks were played and liked by the owner the same evening; the crash recorder came the same night). ("v0.16" is skipped: that name went to the parked HOST-mode firmware on branch `host-mode`.) v0.14 was (2026-10-07). Played and confirmed by the owner up to v0.11 ("This is awesome");
v0.12 is a round of fixes from a code review (see the end of this guide).

## The layout

- **24 tracks**: 16 drum tracks T1-T16 and 8 melodic tracks M1-M8 (below). A drum track plays one
  note on one MIDI channel; the defaults are a GM drum
  kit on channel 10 (36 kick, 38 snare, 42 / 46 hats, 39 clap, 37 rim, 41 / 45 / 48 toms, 49 crash,
  51 ride, 56 cowbell, 54 tambourine, 70, 75, 63). The firmware never guesses what a note sounds
  like: that depends on where it's sent.
- **Up to 128 steps** per track, and **each track has its own length**, so tracks of different
  lengths drift against each other (polyrhythms).
- **32 patterns.** Each pattern keeps its own steps and track lengths, and has a colour. Track sounds
  (note, channel) and mutes are shared by all patterns, like a kit.
- The **pads show 4 tracks x 16 steps** at a time. Scroll through the tracks and page through the steps.

## Melodic tracks M1-M8 (v0.9)

Below T16 come **8 melodic tracks, M1-M8** (scroll down with SELECT; the track list says "mel").
On a melodic track **every step has its own note**; T1-T16 stay drum tracks (one note per track).

| Do | Gets |
|---|---|
| Tap an unlit pad (M track) | step on, with the track's current note (starts at C4) |
| Hold a lit step + knob 3 | that step's note (also becomes the track's current note) |
| Hold an M row button + knob 1 | the track's MIDI channel (defaults M1 = 1 ... M8 = 8) |
| Hold an M row button + knob 2 | the note new steps get |

**Gate (any track):** hold a lit step + **knob 4** = 25 / 50 / 75 / 95 % of the step (50 = default).
A long gate never cuts the next hit of the same note: the old note is ended first. On a swung step,
the gate and ratchets fit into the (shorter) time until the next step.

## Step settings: two knob pages (v0.11)

Hold a lit step and turn the knobs. **KNOB MODE** (the button left of the knobs) **pressed while you hold
the step** switches the page; its CHANNEL light = page 1, MIXER light = page 2. (With nothing held, KNOB MODE
switches the free-knob bank instead, see Knobs below.) Steps with any extra setting pulse gently.

| | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| **Page 1** | ratchet 1-4x | chance 100/75/50/25 % | note (melodic) | gate 25/50/75/95 %, then **TIE**, **SLIDE** |
| **Page 2** | **condition** | **lock value** | **chord** (melodic) | - |

- **Conditions** count the track's own loops since PLAY from the start: **A:B** plays on the A-th of
  every B loops (1:2, 2:2, 1:3 ... 4:4); **1st** / **not 1st**; **FILL** / **not FILL** (hold **PERFORM**
  = fill, its light shows it). They work together with chance. The count keeps going across pattern and
  song changes, so **1st** means the first loop after PLAY from the start, not after a pattern switch.
- **Parameter locks:** hold a track's row button + **knob 3** to pick the CC it sends (none / 1-127).
  A step's lock value is sent on the track's channel just before its note. Steps without a value send
  nothing, so the last value stays. (Until the track has a CC, knob 2 does nothing and the screen says
  so.)
- **TIE** holds the note until the track's next note; if that note is the same, it carries on without a
  new strike. **SLIDE** also holds, but the next note starts before the old one ends (legato: mono synths
  glide). A hold ends at the latest one loop of the track later, and when the pattern changes.
- **Chords** (melodic steps): the step's note plus up to two more. Shapes: oct, 5th, maj, min, sus2, sus4,
  dim, aug, maj7, m7, 7, 5+oct. In NOTE mode with REC on, notes played together land on one step as a chord.

## Controls

### Steps

| Do | Gets |
|---|---|
| Tap an unlit pad | step on (lights at once) |
| Tap a lit pad | step off (on release) |
| SHIFT + lit pad | velocity: accent (127) -> soft (60) -> normal (100) |
| ALT + pad | that track's length = up to that step (1-128) |
| Hold a lit pad + knob 1 | ratchet: 1x, 2x, 3x or 4x hits within the step |
| Hold a lit pad + knob 2 | chance: 25 / 50 / 75 / 100 % (clockwise = more likely) |

Steps past a track's end stay dark. Steps with any setting beyond plain on (ratchet, chance, gate,
condition, lock, chord, tie / slide) gently pulse. A held step stays tied to that step: if the view,
page or pattern moves while you hold it, the hold is dropped (let go and press again).

### Moving around

| Do | Gets |
|---|---|
| GRID left / right | step page: 1-16, 17-32 ... 113-128 |
| SELECT turn | scroll tracks one row (clockwise = down the list) |
| SHIFT + PATTERN up / down | scroll tracks four rows |

### Patterns

| Do | Gets |
|---|---|
| PATTERN up / down (quick press) | next / previous pattern (1-32). While playing, the switch waits for the end of the 16-step bar (the arrow blinks until then). |
| **Hold** PATTERN up or down | the **pattern picker**: the top two rows are patterns 1-16 and 17-32. Tap one to switch to it (queued to the bar while playing). Bright = current, blinking = queued, **grey = empty**. |
| Hold PATTERN + knob 1 | recolour the current / just-tapped pattern (16 colours, the screen names it) |
| ALT + PATTERN up / down | copy the current pattern into the next / previous one and go there (in song mode: copy only, the song keeps playing) |

### Song mode

A song is up to **16 slots**; each slot plays one pattern for 1-8 bars (16 steps each), then the
next slot, looping at the end. The song ends at the first empty slot.

| Do | Gets |
|---|---|
| PATTERN/SONG | song mode on / off (green LED = on). Turned on while playing, the song joins at the next bar. |
| SHIFT + PATTERN/SONG | open / close the song editor (yellow LED blinks) |
| In the editor: top row | select a slot (white). A dim pad marks where the next slot can go. |
| In the editor: rows 2 and 3 | the selected slot's pattern: 1-16, 17-32 (coloured; empty patterns grey) |
| In the editor: bottom row, pads 1-8 | the selected slot's number of bars |
| In the editor: knob 1 | recolour the selected slot's pattern |
| In the editor: SHIFT + slot | clear that slot (later slots move up) |
| PATTERN up / down (song mode) | jump to the next / previous slot at the end of the bar |

PLAY after STOP starts the song from slot 1. The screen's top line shows e.g. **S2/4 P3 1/2**:
slot 2 of 4, pattern 3, bar 1 of 2.

### Playing

| Do | Gets |
|---|---|
| PLAY | start / stop (resumes where it stopped) |
| STOP | stop and rewind |
| SHIFT + SELECT turn | tempo +-1 BPM (add ALT: +-10), 40-300 |
| Knob 1 | swing 50-75 % (delays every second 16th) |
| SHIFT + PLAY | MIDI clock out on / off |

### Tracks

| Do | Gets |
|---|---|
| Tap a row button | mute / unmute that track (red side LED = muted) |
| Hold a row button + knob 1 | that track's MIDI channel (1-16, default 10) |
| Hold a row button + knob 2 | that track's note (shown as a note name and number, e.g. C2 (36); 60 = C4) |

### The free knobs: 4 banks (v0.20)

When nothing is held, the four knobs do what the current **bank** says. **KNOB MODE** (nothing held) steps
banks 1 -> 2 -> 3 -> 4 -> 1; the four little lights next to it (CHANNEL, MIXER, USER 1, USER 2) show which.
(While you **hold a step**, KNOB MODE still flips that step's knob page, and the lights show the page.)

| Bank | Out of the box |
|---|---|
| 1 | knob 1 = **swing**, knobs 2-4 = relative CC **17 / 18 / 19** on channel 1 (exactly the old knobs) |
| 2 | CC 20 / 21 / 22 / 23 on channel 1 |
| 3 | CC 24 - 27 |
| 4 | CC 28 - 31 |

Set any knob up in **BROWSER > Knobs**: it sends **swing**, **tempo**, or **CC 1-127**, on any **channel**, as
**relative** (65 = one step clockwise, 63 = one step back) or **absolute** (the knob moves a value 0-127 and sends
it; it starts at 64 after power-up). Saved. Works with any gear; on the ORC, a knob's CC can be mapped to a named
module's parameter on `/pads` like a pad.

## Live play: STEP / NOTE / DRUM (v0.8)

The three mode buttons choose what the pads do (the lit one is active). The sequencer keeps playing
in every mode. Live pads play at a fixed velocity of **100**; hold **SHIFT** for an accent (**127**).
Letting go of a pad always ends the exact note it started, even after a mode or octave change.

| Mode | Pads |
|---|---|
| **STEP** | edit steps (everything above) |
| **NOTE** | a keyboard: each row a fourth (5 semitones) above the one below, 31 notes. C = blue, other white keys dim, black keys dark; held = green |
| **DRUM** | four 4x4 groups of pads side by side, one colour each. Pad 1 bottom-left, pad 16 top-right (MPC numbering) |

### NOTE mode

| Do | Gets |
|---|---|
| GRID left / right | octave down / up |
| SHIFT + GRID | semitone down / up |
| SELECT turn | which melodic track it plays (M1-M8), on that track's channel |
| ALT + SELECT turn | that track's MIDI channel (shown next to its name) |
| REC (while playing) | record what you play into that track at the nearest step, notes included |
| Two pads on the same note | the note keeps sounding until the last of them is let go |

A note played here also becomes that track's current note, so steps tapped in afterwards get it.

### DRUM mode

Each group has its own **MIDI channel** (defaults 10, 1, 2, 3) and **map**: **notes** (16 notes up from
pad 1's note, default 36) or **MPC pads** (since 2026-10-15): each pad presses the MPC's own pad through
the owner's JJOS2XL front-panel remote map, **CC on channel 14**: pads 1-12 = CC 20-31, pads 13-16 = CC
52-55 (bank A), 127 on press, 0 on release. That works on any MPC screen (NEXT SEQ, 16 levels ...); to just
play an MPC program's sounds, use notes on its channel. MPC pad presses are controls: they are never recorded,
and the group's channel is fixed at 14 while it's on MPC pads. So each group can drive a different box (ORC
drums, MPC, SP-404 ...). (In the code a group is still called a grid.)

| Do | Gets |
|---|---|
| ALT + tap a group | choose the group to edit (the others dim) |
| ALT held + knob 1 | its channel (not on MPC pads: always 14) |
| ALT held + knob 2 | pad 1's note (switches the group to notes) |
| ALT held + knob 3 | map: clockwise = MPC pads, back = notes |
| REC | record on / off (yellow LED) |

**Recording:** with REC on and the sequencer playing, each hit is written at the step it's nearest to
**by when that step sounds** (with swing, odd steps sound later, and a hit just behind one belongs to
it). If that step hasn't played yet, it doesn't play its note a second time. A hit just before a bar
where the pattern (or song slot) changes goes into the pattern that's about to play. It goes to the track that already plays that note on that channel. If none
does, the first track with no steps in any pattern takes over that note and channel (screen:
`rec>T7`). If every track has steps, the hit plays but isn't recorded (`no track`). Recording never
turns an accent back into a normal step.

A pad lights white while held and flashes when the sequencer plays its note. The screen lists the
four groups (e.g. `G2 ch1 C2-D#3`, or `G3 ch14 MPC pads`).

In NOTE and DRUM modes, the row buttons still mute the 4 tracks STEP mode last showed (their lights
are off in these modes, so check the mute in STEP mode). REC records in NOTE and DRUM modes; in STEP
mode it only lights.

## Faders (v0.13)

**Tap PERFORM** to open the faders (its yellow light is on); tap it again, or press STEP / NOTE / DRUM,
to go back. (**Holding** PERFORM is still FILL; its red light shows that.)

Up to **32 faders**, 4 rows at a time. Each row is one fader in its own colour, lit up to its level.
Each fader sends one CC on one channel.

| Do | Gets |
|---|---|
| Tap a column | jump to that level (left = 0, right = 127) |
| Knobs 1-4 | fine-tune the 4 visible faders (knob 1 = top row) |
| SELECT turn / SHIFT + PATTERN | scroll faders one / four at a time |
| Tap the row button of the empty row (dim mark, "+ row button: add") | add a fader: next channel, CC 7, next colour |
| SHIFT + a fader's row button | remove it (the ones below move up) |
| Hold a fader's row button + knob 1 / 2 / 3 | its CC / channel / colour (16 colours) |
| Tap a fader's row button | send its level again |

The first time, there are 16 faders: CC 7 (volume) on channels 1-16. Fader levels are kept with the
next save (moving a fader doesn't start one by itself); adding, removing and setting up faders save.

## The screen

(v0.15 redesign: the screen is mostly pictures now.)

- **Line 1:** pattern (and the queued one, e.g. `PAT 3>4`) or the song line, then the tempo. A `*` in
  the corner means there are unsaved changes.
- **Line 2:** a PLAY / STOP icon, what you're looking at (`P1/4` = step page 1 of 4, `FADERS`,
  `NOTE`, `DRUM`, `T2 step 5`), the last action (a new length, a knob's CC; cut to fit), `sw58` only
  when swing isn't 50, and `out` / `ext` when the clock isn't internal.
- **The step view:** the 4 visible tracks, one row each: a dot per step (small = off, bigger dot on
  every 4th, a small box = soft, a box = normal, a tall box = accent; nothing past the track's end).
  A muted track's steps are outlines and it shows `M`. A pair of lines marks the step each track just
  played, **per track**, so tracks of different lengths each show their own place.
- **Faders:** a bar and the value per fader (`F1 CC7 [=====   ] 100`).
- **Holding a step**, page 1: ratchet as four boxes, chance and gate as bars, the note for melodic
  steps, TIE / SLIDE as words. Page 2 and holding a row button are still text.
- **Tempo and swing** pop up as a big number for a second after you change them (SHIFT + SELECT for
  tempo, knob 1 for swing), with a bar, then the normal screen returns.

## MIDI

- **Out:** each track's note on its channel (default channel 10), velocity 60 / 100 / 127, the step's
  gate (default half a step; each ratchet hit gets its share). Locks send their CC first. With clock
  out on: start, continue, stop and 24 clocks per beat, locked to the steps.
- **In:** if the host sends MIDI clock, the sequencer follows it (start, continue, stop, 6 clocks per
  step) and PLAY is disabled meanwhile. Swing also applies to external clock, and gates / ratchets
  follow the host's tempo.

## Saving

Everything (steps, lengths, patterns, tempo, swing, mutes, track notes and channels, clock out) is
saved automatically **3 seconds after the last lasting change**, in the background, without
disturbing playback. Saves survive unplugging and firmware updates.

Moving around (switching patterns, muting, changing octave or the keyboard's track, song on/off) and
**playing live** (tempo, swing, clock out, hits you record with REC) don't start a save by
themselves (v0.15): they're remembered and written with the next real change, and **when you press
STOP** if anything is waiting. **While stopped** (v0.17), such a change (a tempo set before pressing PLAY,
a light show picked with STOP / SELECT, the light speed) is saved **20 seconds after the last one**, so
it can't be lost to unplugging; the `*` shows until then. Editing steps, step settings, song, track notes / channels, colours and
fader setup still saves 3 seconds after the last change.

**AUTOSAVE off** (BROWSER menu): nothing saves by itself; the `*` stays on until **Save now**. The
setting itself is always saved.

Now and then the save area gets tidied: **only while stopped**, the Fire freezes for a second or
two, then carries on. Nothing is lost. Since v0.10 saves are small, so this is rare.

**Room for steps:** all patterns share room for about 6,800 lit drum steps (3,400 melodic). If it's
ever full, the screen shows **FULL** and the new step isn't added (nothing else changes). Patterns
count at the larger of their current and their last saved size, so whatever is saved always fits back
in after a power cut.

## v0.15: the BROWSER menu, light shows, the host protocol (2026-10-13)

### The BROWSER menu
Press **BROWSER** (it lights). **SELECT** turns to move, **pushing SELECT** opens an item to change
(turn) and confirms; BROWSER backs out. PLAY / STOP still work.

The top page has **Autosave**, **Save now**, **ORC mode** (back to the ORC's pad layout after a mode button
left it; only once the ORC has set one up since power-on), **Playing >**, **Knobs >**, **Display >**, **Lights >**
and **Shortcuts >** (v0.20). Pushing SELECT on **Lights >** opens the
page `LIGHTS`; BROWSER goes back up one page (landing on the row you came from), and closes the
menu from the top page.

| Item | What it does |
|---|---|
| **Autosave** | ON / OFF (see Saving). |
| **ORC mode** | the ORC's pad layout again (see the ORC's `/pads`). |
| **Playing >** | **Tempo** (SHIFT = steps of 10), **Swing**, **Clock out** on / off: the same values SHIFT + SELECT, knob 1 and SHIFT + PLAY change. |
| **Knobs >** | **Bank** (the current one; same as KNOB MODE), **Knob** 1-4 to set up, then what it **Sends** (swing, tempo, CC 1-127; SHIFT = steps of 10), its **Channel**, its **Mode** (relative / absolute). |
| **Display >** | **Beat marks** on / off, their **Colour** (blue, white, red, green, yellow, purple, cyan) and **Spacing**: every beat (4 steps), every bar (16), or beat+bar (the bar mark brighter). They use the pads' lowest brightness. |
| **Shortcuts >** | read-only cheat sheets: Steps, Hold a step, Hold a row, Patterns, Song editor, Playing, Note mode, Drum mode, Faders, Knobs. SELECT scrolls, BROWSER goes back. |
| **Save now** | writes anything waiting; says `saving...` or `nothing new`. |
| **Lights >** | opens the lights page: |
| | **Start lights**: the pads become a light show. |
| | **Show**: PLASMA, SPARKLE, SWEEP, FLAME, BEAT, SPARSE, RAIN, RIPPLE, COMET, SCANNER, WAVE, TRACKS, **PI VU** (the Pi's audio levels while the Pi sends them; 3 s with none and the pads show SPARKLE instead, and the bars come back as soon as the Pi sends again; v0.17), **TEXT** (a message scrolls across the pads: HELLO until the Pi sends another with `host_text.py`) or CYCLE (a new time-driven show every 20 s). Turning it while lights run switches at once. |
| | **Speed**: x0.25 .. x4 (SHIFT = fine steps). |
| | **Idle start**: off / 15 s / 30 s / 1 m / 2 m / 5 m with the sequencer stopped and nothing touched. **Default OFF since v0.18** (the owner never wanted a screen saver; v0.18 also switches an existing save to off once). Start the lights by hand: BROWSER > Lights > Start lights. The time counts from when playing stops (also an external clock's STOP) and from when the pads last went back to the steps; nothing held (a step, a row, a live pad, PATTERN, SHIFT, ALT), not in the song editor or with REC armed (v0.17). |
| | **Smooth**: on = in-between brightness by a fixed per-pad dither (the pads only have 8 levels; it never flickers); off = plain. |
| | **Bar look**: how the VU bars (TRACKS, PI VU) look: **snappy** (a level goes straight to the bar, the bar falls in a steady line, the top pad partly lit; the default), **smooth** (the bar follows the level, the peak dot holds before it falls) **whole** (smooth, but only whole pads, never a half-lit one), **heat** (a heat-sensitive sheet: a pad's colour is how deep inside the bar it is, so the base is hot red and the tip cools through orange, yellow, green, cyan, blue and purple to black; snappy movement) or **heat smooth** (the same colours, smooth movement). |

### Light shows
The ORC's eleven time-driven shows, ported exactly (the code is checked against the ORC's Python, every pad): PLASMA,
**SPARKLE**, SWEEP, FLAME, BEAT, SPARSE, RAIN, RIPPLE, COMET, SCANNER, WAVE (the ORC's twelfth, a made-up time-driven VU, was removed: TRACKS and PI VU are the real ones; its number 11 stays a hole). Pace and tempo
lock are the ORC's (they follow the BPM). Plus **TRACKS**: VU bars that jump with the sequencer's own
hits (column = track, accents tallest) and fall by themselves. The bars' look is a menu setting (Bar look).

- Start: menu **Start lights**, the idle time, or the host. SPARKLE is the default show.
- While they run: **SELECT** = next / previous show, **knob 1** = speed, **STOP** = next show (when
  stopped), **BROWSER** = the menu. **The first touch of any pad or button only ends them** (it is
  not also an edit). Idle lights also end when playback starts; lights you started by hand keep
  running while it plays, also under an external clock (v0.17; they used to freeze).
- A frame is drawn two columns at a time and only when the next step, ratchet or note-off isn't
  about to be due (the rule a save follows), then shown all at once. Under an external clock the
  next step is predicted from the clock's step length.
- TRACKS / PI VU bars start empty each time the lights start, and when the Bar look changes.

### The host protocol (the Pi -> the Fire)
SysEx, id `7D` (non-commercial), then `ORC` (`4F 52 43`) and a command:

| Command | Data | Meaning |
|---|---|---|
| `00` | | hello: the Fire replies `F0 7D 4F 52 43 00 01 F7` (version 1) |
| `01` | 16 x 0-127 | VU column levels, left to right; bars jump up, fall alone, so send raw levels ~30/s |
| `02` | `0` / `1` | host lights on (VU bars take the pads; a touch ends them and the host must ask again; they also end 3 s after the last levels frame) / off: ends the host bars or any show the host started with 02 / 04 / 05 (v0.17) |
| `03` | 0-127 | light speed (64 = x1), saved like the menu setting |
| `04` | 0-12 (not 11) | show that effect (like Start lights); the saved setting isn't changed. It stays (CYCLE and PI VU levels leave it alone) until a touch, SELECT/STOP or `02 00` (v0.17) |
| `05` | up to 32 ASCII characters | scroll that message across the pads (3x4 font: capitals, digits, `! ? . , - : ' + / = * #`, and `~` is a heart; lower case shows as capitals). The first thing on screen is the message coming in from the right edge. Stays like `04`. An empty message is ignored. |
| `06` | optional `01` | the last crash on record: the Fire answers with a 61-byte record, version 2 (cause, main-loop stage, PC, LR, status registers, uptime, build id, SP, stack low point; see `crash.h`, decode with `host_crash.py`; version 1 = 46 bytes, no build/stack); `06 01` also clears it |
| `07` | | status (read only, v0.17): the Fire answers `F0 7D 4F 52 43 07` + 16 bytes: lights on, showing (mode, 127 = none), host bars, host-started, pinned, unsaved (`*`), lazy save armed, settings saves since boot (mod 128), playing, external play, Show setting, speed, menu open, frames drawn (mod 128), and the deepest stack use since boot in bytes (2 bytes, high 7 bits first). `fire_live_test.py` uses it. |
| `08` | | the stack around the last fault (read only, v0.17c): `F0 7D 4F 52 43 08 01 <valid> <base: 5 bytes> <64 words x 5 bytes> F7` (332 bytes of data). Decode with `host_crash.py`. |
| `0A` | first pad, count 1-10, then R G B (0-7) per pad | the ORC SURFACE's pad colours (v0.19); kept while the surface is hidden |
| `0B` | `01 <x>` / `00` | show the ORC surface / leave it (v0.19). The second byte was the surface's MIDI channel up to v0.20; since v0.21 it is accepted and ignored. The ORC sends `01 <x>` then `00` when the Fire appears, so the Fire learns there is a surface but stays on its steps; BROWSER > ORC surface (or `/pads` SHOW ON FIRE) shows it. |
| `0C` | **Fire -> ORC** (v0.21): `<pad 0-63> <velocity>` | a surface pad press (velocity 1-127) or release (0). The surface uses **no MIDI channel**, so the sequencer keeps all 16 (before v0.21 a surface pad was a note on the `0B` channel). |

**Senders (on the Pi, `~/fire_fw/`):**
- `host_text.py "message"` sends command 05.
- `host_vu_demo.py` is a 10-second example of command 01 with made-up levels.
- **`host_vu.py` = the real audio VU**, running as the service **`orc1-firevu`** (enabled at boot). It reads the
  newest ~43 ms of a skipback RAM ring (the master: a stereo RAM track named like MAIN, else any RAM ring; read-only,
  straight from tmpfs, no JACK client), splits it into 16 log bands (40 Hz to 14 kHz, +3 dB per octave tilt so highs
  aren't flat) and sends 16 levels ~23 times a second. The gain follows the music (the loudest band is full scale,
  the bars span 36 dB; it relaxes about 3 dB a second), a gate at -68 dBFS keeps hiss and the gaps between songs dark.
  On the Fire: **BROWSER > Lights > Show > PI VU** (then Start lights). It runs at idle CPU priority (about 3% of a core in
  silence, 6-8% while analysing music: measured), `MemoryMax=64M`, and is killed first under memory pressure: skipback comes first.
  `host_vu_test.py` (79 checks, 27 mutations all red) tests it against a fake ring; `host_vu_cost.py` measures its CPU.

### Crashes (since 2026-10-13)
On 2026-10-13 at 21:00 the Fire fell into DFU (update mode) 63 minutes after a flash, with no sign of why: every fault
handler just rebooted to DFU. Now a **fault or a watchdog reset is recorded and the Fire restarts itself** in 2-5 s:
- The record lives in the RTC backup registers (they survive a reset): the cause (HardFault / MemManage / BusFault /
  UsageFault / watchdog = a hang), the PC and LR, the fault status registers and fault address, the uptime, and which
  stage of the main loop was running (USB + MIDI, sequencer, queues, input events, gates, flash saver, lights, knobs,
  paint, render + OLED, loop tail, start-up).
- The header's status slot shows `FAULT RST` or `WDOG RST` after a restart.
- **A crash loop still falls back to DFU:** 3 crashes in a row, each within 30 s of booting, leave it in update mode so a
  bad image can be replaced without a debugger.
- Read it with `python3 host_crash.py [--elf fireseq.elf]` (the PC becomes a source line); `--clear` clears it. The VU
  service (`orc1-firevu`) also logs any crash the Fire reports when it connects.
- Proven on the real chip with a bench build (`crash_bench.py`: a bus fault, a hang, an undefined instruction, then the
  crash-loop fallback, 15/15). The bench hook (`7E`) is compiled out of the shipped image (`#ifdef CRASH_TEST`).
- A watchdog hang cannot say WHERE it hung (nobody was running), only which stage: that is why the stage markers exist.
- **Since v0.17 (2026-10-14) a record also says:**
  - the **build** that crashed. The id is a hash of the sources (`ORCBUILD=xxxxxxxx` in the image). `host_crash.py --elf`
    refuses to turn addresses into source lines against an ELF of another build, because those lines would be wrong.
  - the **stack pointer** at the fault, and the **deepest the stack has reached** since boot (the free RAM is painted at
    boot; a watchdog record reads the previous run's paint). Normal use measured 1,316 of ~16 KB.
  - a new cause, **stack smash**: the firmware is built with `-fstack-protector-strong`, so a function whose local buffer
    overruns is caught when it returns, and its PC names that function (proven on the chip with bench hook `7E 03`).
  The reply to `06` is now version 2 (61 bytes); hosts accept version 1 (46 bytes) too.
- **Since v0.17c (2026-10-14):**
  - **RAM is execute-never** (one MPU region). A jump into RAM (a smashed return address or function pointer) faults
    on its first instruction as a MemManage fault, with PC = the RAM address and **LR = the line that jumped**. Proven
    with bench hook `7E 04`: "LR -> host_cmd @ main.c:1952".
  - **The stack around the fault is kept** (64 words from the exception frame up, in RAM that a reset doesn't clear)
    and read with host command `08`. `host_crash.py` prints it and turns the words that look like return addresses
    into source lines (a likely call chain; leftovers of earlier calls can appear too). `--clear` reads first, then clears.
  - The description says **what was running** (main loop, USB interrupt, LED-chain interrupt ...) from the xPSR.
  - `render`, `draw_*`, `host_cmd`, `midi_*`, `handle_events`, `paint` are real functions now (`noinline`). Inlined into
    `main()`, which never returns, their canaries were never checked (found by the crash hunt).
- **2026-10-13 22:09, on v0.17 (build without these): a real crash, caught and restarted in 2 s:** HardFault, *invalid
  state*, PC `0x00000030`, LR `0x2000fd4f` (an address inside the stack), stage render + OLED, 486 s up, ~30 s into
  idle PI VU bars from `orc1-firevu`. That is the look of a smashed return address, and the conditions match the
  unexplained 21:00 DFU of the day before. Not found yet: the canaries above are there to name it next time.

## v0.14: 32 patterns with colours (2026-10-07)

Patterns 1-8 are the ones you had; 9-32 start empty. Empty patterns show grey everywhere (picker,
song editor) until they get steps. Each pattern's colour is saved; a copy (ALT + PATTERN) takes the
source's colour. Empty patterns take no room in the save area.

## v0.12: fixes from a code review (2026-10-07)

Three reviewers read everything since v0.7; every finding was checked against the code, and each fix
has a test that fails without it.

- Saving: a save cancelled at just the wrong moment could leave a gap that hid later saves after a
  reboot (now impossible, and the boot scan also looks past any gap); cancelled saves are queued again;
  the settings save works from a copy; in song mode the current pattern is remembered; the watchdog gets
  its long timeout before a tidy-up erase; a saved set of patterns always fits back in at boot.
- Playing: external clock + turning swing can no longer skip or double a step; REC lands on the
  nearest step by sound (swing) and in the right pattern at a bar change; ties end on a pattern change
  or after a loop; gates and ratchets follow the host's tempo and fit swung steps.
- Controls: a held step or row stays tied to what you pressed; chord recording starts fresh after STOP;
  ALT + PATTERN in song mode only copies; two NOTE pads on one note; screen text fits.

## Getting back to Akai's firmware

From the Pi: `~/fire_fw/fw/fire_flash.sh stock` (see `fw/DEVELOPER.md`). Saved patterns stay in
their own flash area and come back if this firmware is flashed again.
