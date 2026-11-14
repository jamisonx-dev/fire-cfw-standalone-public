# FLAME (Akai Fire custom firmware): user guide

Custom firmware that turns the Akai Fire into a **standalone step sequencer**. It keeps its own time,
lights itself and sends MIDI notes over USB to whatever it's plugged into (a computer, a USB host, the
ORC). Since v0.36 it is called **FLAME** and has **two USB MIDI ports**:

| Port | Linux / macOS name | Windows 10 name | What it is |
|---|---|---|---|
| 1 | `FLAME seq` | `FLAME` | the sequencer: its notes, CCs and clock, MIDI clock in, the host protocol (below). Up to v0.35b this was the only port, named `ORC FW seq`. |
| 2 | `FLAME DAW` | `MIDIIN2 (FLAME)` / `MIDIOUT2 (FLAME)` | **DAW mode** only: Mackie Control / HUI for a DAW (below). Nothing else uses it. |

(The Windows names come from Windows itself, which names a device's extra ports this way. Newer Windows MIDI may show
the Linux names.)

**What needs what.** Everything in this guide works with just the Fire and whatever it plays, except three things that
only do something with the ORC, the owner's Raspberry Pi rig: **ORC mode**, **the DAW page** (PERFORM twice) and the
host commands the Pi sends. Without the ORC they stay out of the way.

Version described: **v0.39** (2026-11: **FLAME** with two USB ports (v0.36), **DAW mode** = Mackie Control / HUI on its
own port (v0.36), **PAINT mode** (v0.37), **BROWSER > About** with the version and the crash on record (v0.35), one
second view per mode button (v0.38: STEP x2 track levels, NOTE x2 UNIVERSAL, DRUM x2 the MPC remote, PERFORM x2 the
DAW page). **Removed in v0.36** to make room for DAW mode: the Pi VU bars and the ORC pads page; the light shows went
in v0.31r). Older history: **v0.29** (2026-10-24: **32 tracks** (T17-T24 added), **CC tracks**, **undo / redo**, the **roller / looper**; sections marked v0.29 below). **v0.21** (2026-10-14 night: ORC surface pads talk to the ORC by SysEx `0C`, no MIDI channel). **v0.20** (2026-10-14: knob banks on KNOB MODE, BROWSER menu pages Playing / Knobs / Display / Shortcuts / ORC surface, beat-mark settings). **v0.19**: ORC surface mode. **v0.18**: idle lights off by default. **v0.17** (2026-10-14: the 2026-10-13 review fixes wired in + host status 07). **v0.15** (2026-10-13; lights, menu, screens, PI VU and the heat looks were played and liked by the owner the same evening; the crash recorder came the same night). ("v0.16" is skipped: that name went to the parked HOST-mode firmware on branch `host-mode`.) v0.14 was (2026-10-07). Played and confirmed by the owner up to v0.11 ("This is awesome");
v0.12 is a round of fixes from a code review (see the end of this guide).

## The layout

- **32 tracks** (v0.29): 24 drum tracks T1-T24 and 8 melodic tracks M1-M8 (below). Scrolling down the
  list: T1-T16, T17-T24, then M1-M8. A drum track plays one note on one MIDI channel (or sends a CC: see
  CC tracks); the defaults are a GM drum kit on channel 10 (36 kick, 38 snare, 42 / 46 hats, 39 clap,
  37 rim, 41 / 45 / 48 toms, 49 crash, 51 ride, 56 cowbell, 54 tambourine, 70, 75, 63), and T17-T24 on
  notes 60, 61, 62, 64, 65, 66, 67, 68 on channel 10. The firmware never guesses what a note sounds
  like: that depends on where it's sent. Patterns saved by v0.28 or older load unchanged, with T17-T24 empty.
- **Up to 128 steps** per track, and **each track has its own length**, so tracks of different
  lengths drift against each other (polyrhythms).
- **32 patterns.** Each pattern keeps its own steps and track lengths, and has a colour. Track sounds
  (note, channel) and mutes are shared by all patterns, like a kit.
- The **pads show 4 tracks x 16 steps** at a time. Scroll through the tracks and page through the steps.

## CC tracks (v0.29)

Any drum track (T1-T24) can send a **CC instead of a note**: hold its row button and turn **knob 4** clockwise
(back = a note track again). The screen's top line says `1D CC` / `1D note`. On a CC track:

- **knob 2** (row held) picks the CC number (1-127), on the track's channel (knob 1);
- each lit step sends that CC once, with the value of the step's **knob 2 on page 2** (hold the step, KNOB MODE
  for page 2; no lock CC needed), or, if it has none, its level's velocity (soft 60, normal 100, accent 127);
- ratchets, gate, TIE / SLIDE and the track volume do nothing (a CC has no length); chance and conditions work;
- recorded DRUM-mode hits never go onto a CC track.

## Undo (v0.29)

**SHIFT + REC** puts a pattern's steps and track lengths back to how they were before its last edit, and says
`undo P3` on the screen. Press it again to **redo** (`redo P3`). One edit is:

- one step pad from press to release, including every knob turn while it was held;
- one SHIFT + pad (velocity) or ALT + pad (length) tap;
- one ALT + PATTERN copy (undo gives back the pattern it copied INTO);
- a whole **REC take**: from the first recorded hit until REC goes off or playing stops.

Holding a step just to look at it doesn't count as an edit. Undo covers only steps and lengths, not settings
(channels, notes, mutes, tempo, CC tracks). It holds one pattern of up to about 330 drum / 160 melodic steps;
a bigger one says `undo: too big`. It is lost at power-off.

## The roller / looper (v0.29)

Like the BeatStep Pro's: **hold KNOB MODE and the pads become the roller** (after a moment the pads change; a
quick tap of KNOB MODE still steps the knob bank). Hold a roller pad and the track(s) repeat; let go and they
carry on **exactly where they would have been**, because the sequencer's own count never stopped.

| Pads in each row | Do |
|---|---|
| Left 8 (amber, then cyan) | **all tracks** |
| Right 8 (that row's track colour) | **only the track on that row** |
| Left to right in each half | **1 bar, 1/2, 1/4, 1/8, 1/16** = loop that many steps (16 / 8 / 4 / 2 / 1) from the step about to play; then **1/32, 1/48, 1/64** = that step repeated 2 / 3 / 4 times per step (1/48 = sixteenth triplets) |

- Once a roller pad is down, KNOB MODE may be let go: the roll lasts while the pad is held.
- Several pads at once: a track's own pad beats the all-tracks ones; the newest pad wins. Sliding to another
  length keeps the loop's start.
- The playhead shows where a rolling track really is; the row's green side light = that track is rolling.
- It follows swing and an external clock (the same timing as normal playback). Recording while rolling records
  at the real position.

## Melodic tracks M1-M8 (v0.9)

Below T24 come **8 melodic tracks, M1-M8** (scroll down with SELECT; the track list says "mel").
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
| PATTERN/SONG | open / close the song editor (yellow LED blinks). Changed 2026-10-19: this used to be SHIFT + it. |
| SHIFT + PATTERN/SONG | song mode on / off (green LED = on). Turned on while playing, the song joins at the next bar. Changed 2026-10-19: this used to be a plain tap. |
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
| **ALT + STEP**, tapped (v0.24) | **Tap tempo.** Tap the beat: the 2nd tap sets the tempo (the big number pops up) and every further tap refines it, averaged over the last 8 taps so a sloppy tap barely moves it. A pause of over 2 s starts a new count (the first tap of a count changes nothing); a sudden big change of speed (or a missed or very late tap) restarts the count instead of averaging two tempos, and the OLD tempo stays until a 3rd tap confirms the new one (so one sloppy tap never flashes a wrong tempo); two taps under 150 ms apart count as one. Does nothing while an external clock drives the tempo. A plain STEP is unchanged. |

### Tracks

| Do | Gets |
|---|---|
| Tap a row button | mute / unmute that track (red side LED = muted). Several rows can be pressed together: each one mutes (2026-10-19; before, only the last one pressed did) |
| Hold a row button + knob 1 | that track's MIDI channel (1-16, default 10) |
| Hold a row button + knob 2 | that track's note (shown as a note name and number, e.g. C2 (36); 60 = C4); on a CC track its CC number |
| Hold a row button + knob 4 (v0.29) | drum tracks: a note track or a **CC track** (above) |

### The free knobs: 4 banks (v0.20)

When nothing is held, the four knobs do what the current **bank** says. **KNOB MODE** (nothing held), tapped,
steps banks 1 -> 2 -> 3 -> 4 -> 1 when it is let go (v0.29: held, it is the roller); the four little lights next to it (CHANNEL, MIXER, USER 1, USER 2) show which.
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

**Each section has its own banks (v0.29):** STEP, NOTE, DRUM and the MPC remote each keep their own 4 banks and
their own current bank, so the knobs can do something different in each. KNOB MODE steps the bank of the section
you are in, and the lights show it. BROWSER > Knobs opens on the section you were in (its **Section** line picks
another). After the update all four start as copies of the banks you had, so nothing changes until you edit one.

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

Each group has its own **MIDI channel** (defaults 10, 1, 2, 3) and plays **16 notes** up from pad 1's note (default
36). So each group can drive a different box (ORC drums, MPC, SP-404 ...). (In the code a group is still called a
grid.) To press the MPC's own pads (and the rest of its panel), use the **MPC remote** (DRUM again, below); the groups'
old "MPC pads" map was dropped in v0.29b for it (a group saved on it plays notes again).

**16 LEVELS** (v0.29b, like the MPC's): tap the **top row button** (the mute button beside the top pad row; lit while
on). It takes the **last pad you hit**: all 16 pads of that pad's group play that pad's note, pad 1 softest (velocity 8)
up to pad 16 = 127 in even steps (SHIFT does nothing; the pads light dim to bright). Nothing hit yet: group 1's pad 1.
Tap it again for the normal pads. A recorded hit lands at the nearest step level (soft / normal / accent). Not saved:
it is off after power-up.

| Do | Gets |
|---|---|
| ALT + tap a group | choose the group to edit (the others dim) |
| ALT held + knob 1 | its channel |
| ALT held + knob 2 | pad 1's note |
| Top row button | **16 levels** on / off (above) |
| REC | record on / off (yellow LED) |
| DRUM again | the **MPC remote** (below), and back (the DRUM light blinks while it's up) |

**The MPC remote** (v0.28): the whole MPC1000 / JJOS2XL front panel on the 64 pads, the owner's layout of his
remote map, all on **channel 14**. Each pad is that MPC button: press = note on / CC 127, release = note off /
CC 0, never recorded. The 16 pads at the left are the MPC's physical pads, so they do whatever the MPC's
screen makes them do. Each button has its own colour (the owner's picks: pads blue, banks amber, pad buttons orange,
F-keys indigo / purple alternating, REC + OVERDUB + UNDO SEQ red, nav white ...); white while held. The Q-sliders aren't on it.

| Row (top first) | Columns 1-4 | 5-8 | 9-10 | 11-16 |
|---|---|---|---|---|
| 1 | pads 13-16 | TAP, ERASE, UNDO SEQ, NUMERIC | FULL LEVEL, BANK A | F1-F6 |
| 2 | pads 9-12 | MODE, MAIN, WINDOW, AFTER 1 | 16 LEVELS, BANK B | -, STEP back, STEP forward, -, -, - |
| 3 | pads 5-8 | DATA -, up, DATA +, AFTER 2 | NEXT SEQ, BANK C | -, LOCATE back, LOCATE forward, -, -, PLAY START |
| 4 | pads 1-4 | left, down, right, SHIFT | TRACK MUTE, BANK D | -, -, REC, OVERDUB, STOP, PLAY |

**Recording:** with REC on and the sequencer playing, each hit is written at the step it's nearest to
**by when that step sounds** (with swing, odd steps sound later, and a hit just behind one belongs to
it). If that step hasn't played yet, it doesn't play its note a second time. A hit just before a bar
where the pattern (or song slot) changes goes into the pattern that's about to play. It goes to the track that already plays that note on that channel. If none
does, the first track with no steps in any pattern takes over that note and channel (screen:
`rec>T7`). If every track has steps, the hit plays but isn't recorded (`no track`). Recording never
turns an accent back into a normal step.

A pad lights white while held and flashes when the sequencer plays its note. The screen lists the
four groups (e.g. `G2 ch1 C2-D#3`; a 16-levels group: `G1 ch10 C2 16 lv`).

In NOTE and DRUM modes, the row buttons still mute the 4 tracks STEP mode last showed (in DRUM mode except the top one:
16 levels) (their lights
are off in these modes, so check the mute in STEP mode). REC records in NOTE and DRUM modes; in STEP
mode it only lights.

## Faders (v0.13)

**Tap PERFORM** to open the faders (its yellow light is on); tap it again, or press STEP / NOTE / DRUM,
to go back. (**Holding** PERFORM is still FILL; its red light shows that.)

Up to **32 faders**, 4 rows at a time. Each row is one fader in its own colour, lit up to its level (the pads are bright up to the level and the pad AT the level is dim; everything above it is dark; at level 0 the first pad is the dim one; changed 2026-10-19).
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

**Faders follow the host (v0.31c):** a Control Change sent TO the Fire over USB (from the Pi, e.g. REAPER's faders through a
bridge) on channel C, CC N sets every fader set up as channel C / CC N to that value, in any mode (the pads show it when
FADERS is on screen). Nothing is sent back and it starts no save (the level rides along with the next one, like a fader you move).

## The DAW page (v0.31d)

**Needs the ORC** (its `orc1-dawsurf` bridges REAPER to it). Not to be confused with **DAW mode** (below), which needs
no Pi. A second set of faders that the **host** (the Pi, e.g. bridging REAPER as a Mackie Control surface) defines: up to
**9 rows** (8 tracks + the master), each with the DAW's own track colour, its name (up to 8 characters) and its level.
You don't set anything up: the rows appear when the host sends them, and they are **never saved** on the Fire (RAM only:
after a power-on there is no DAW page until the host sends it again).

**Getting there (v0.38):** tap PERFORM: FADERS, **again: the DAW page** (only while the host has defined one; without it
the second tap goes back), again: back to where you were. Each mode button has one second view (STEP: track levels, NOTE:
UNIVERSAL, DRUM: the MPC remote, PERFORM: the DAW page), and its light blinks while it is up. STEP / NOTE / DRUM leave it too. If the
host removes the page while it is on screen, the Fire goes back to where you were before FADERS.

It plays **exactly like FADERS** (the same code):

| Do | Gets |
|---|---|
| Tap a column | jump to that level (left = 0, right = 127) |
| Knobs 1-4 | fine-tune the 4 visible rows (knob 1 = top row) |
| SELECT turn / SHIFT + PATTERN | scroll rows one / four at a time; **pushing past the first or last row asks the host for the previous / next bank** (1E), e.g. REAPER's tracks 9-16 |
| Row buttons | nothing (the host owns the rows: no add / remove / set-up) |

Each row lights in the DAW's colour up to its level (bright up to the level, the pad AT the level dim, dark above; a
track with no colour shows white). The screen shows `name [bar] value`; a row the host sent no name for shows `row N`.
A level you move is sent to the host (1D) once per change, exactly when FADERS would send its CC; a level the host sends
(the DAW's fader moved) only moves the row, nothing is sent back. FADERS' "follow the host CCs" (above) never touches the
DAW page.

## DAW mode (v0.36): the Fire as a Mackie Control / HUI surface

Plug the Fire into a computer and it can drive a DAW the way a Mackie Control (MCU) or HUI controller does: faders,
mute / solo / record-arm / select, the transport, banking, the knobs as V-pots. No Pi, no driver. It talks on the
Fire's **second USB port** (`FLAME DAW`; on Windows 10 `MIDIIN2 (FLAME)` / `MIDIOUT2 (FLAME)`), so the sequencer on the
first port is not touched and keeps playing underneath.

**In the DAW:** add a Mackie Control (or HUI) control surface and give it the FLAME DAW port as both its input and its
output. Most DAWs open their MIDI ports only when they start, so plug the Fire in first.

**On the Fire:**
- **BROWSER > DAW protocol**: **auto** (the default: it waits and follows whichever protocol the DAW speaks first),
  **MCU** or **HUI**. Saved.
- **BROWSER > MCU/HUI/DAW** starts DAW mode (it says `no computer` when the Fire is not plugged into one). **SHIFT +
  BROWSER** leaves, as in ORC mode; the screen says `DAW off`.
- The screen's top line says `DAW auto` / `DAW MCU` / `DAW HUI`, with `waiting` until the DAW has said anything. Below:
  the 8 track names (the DAW's, else 1-8), the DAW's time display, and which knobs are which.
- What the DAW has sent is kept while you are out of DAW mode (the port is listened to from power-on), so going back
  in shows the DAW's current names and levels.

Two views; **PERFORM** = the fader view (it opens there), **STEP** = the button view (the lit button shows which):

| Fader view (PERFORM) | Does |
|---|---|
| Pads | 4 rows = 4 of the DAW's tracks (8 + Master with MCU, 8 with HUI); tap a column to set that track's fader (left = bottom). Plays exactly like FADERS. |
| Knobs 1-4 | fine-move the 4 visible faders |
| SELECT turn | scroll the rows; past the first / last one the DAW moves one track |
| Row buttons | that track's MUTE |

| Button view (STEP) | Does |
|---|---|
| Pads, top row to bottom | **REC** (red), **SOLO** (yellow), **MUTE** (orange), **SELECT** (white) for 8 tracks, 2 pads per track. Lit = on in the DAW, dim = off. |
| Knobs 1-4 | the V-pots of tracks 1-4; **push SELECT** for tracks 5-8 (the screen says which) |
| SELECT turn | the jog wheel |

| Either view | Does |
|---|---|
| PLAY / STOP / REC | the DAW's transport; their lights show the DAW's |
| PATTERN up / down | bank: the previous / next 8 tracks |
| GRID left / right | move one track left / right |

A fader you move is "touched" while you move it and let go half a second after you stop, so the DAW's automation
behaves.

**Tested:** MCU with REAPER on a Windows PC (the owner, on v0.36). **Not tested yet:** HUI with Pro Tools (or any HUI
host), Logic, Ableton, Cubase, Bitwig, FL Studio. The protocol bytes come from public documentation and other people's
work, not from a real HUI host: if your DAW does something odd, please report it (see the README). Meters are not shown.

## ORC mode (v0.33): the ORC takes the whole Fire

**Needs the ORC** (the owner's Raspberry Pi rig); with nothing answering it gives up after 5 seconds.


**BROWSER > ORC mode** hands the Fire to the ORC: the pads, every button, the four knobs, SELECT and the
screen are the ORC's (its own sequencer view, chord rows, keyboards, FX, faders: whatever the ORC's Fire
handler shows). **The Fire's own sequencer keeps playing underneath**, on its own clock or the external one;
you just cannot see or edit it until you come back.

- **Back to the sequencer: SHIFT + BROWSER.** It is the one combination the ORC never sees.
- The Fire also comes back **by itself** if the ORC stops answering for 5 seconds (unplugged, the ORC
  restarting): the status line says `ORC gone`. With no ORC listening, ORC mode shows `waiting for the ORC`
  and gives up after those 5 seconds.
- Entering lets go of anything held (live notes, a roll, latched UNIVERSAL pads), so nothing hangs.
- Nothing is saved or changed by ORC mode; it is off at power-on.
- The older, smaller **ORC pads** page (only the pads went to the ORC's `/pads` layout) was removed in v0.36.

### Brightness: 22 steps instead of 8 (v0.33)
Every light used to have 8 brightness steps; the ORC's colours looked washed out on them. Now there are 22.
The sequencer's own lights use the same 8 as before (each is exactly the same brightness: step k of 7 = fine
level 3 x k of 21), and ORC mode's colours use all 22 through a curve (`24`, below; 2.0 at power-on) so dim
colours stay dim and distinct. How it is done: a light is switched on in k of every 21 chain refreshes, spread
evenly, instead of k of 7. **The chain interrupt's timing and its button scan are unchanged** (still one pass
every 500 us): the 2026-10-13 attempt at finer brightness changed both and broke SHIFT + pad; this does not
touch them. The slowest blink, on the very dimmest step only, is 95 times a second.

## PAINT mode (v0.37): paint the pads

A very simple paint box, for kids and for art. **BROWSER > Paint** to start (no computer needed); **SHIFT + BROWSER**
goes back to the sequencer, which keeps playing underneath the whole time.

- **Tap a pad** to paint it the picked colour. Tap it again (same colour) to wipe it back to black.
- **Hold SHIFT** and the pads show the palette: 16 colours as big 2 x 2 blocks, red at the top left, the eraser (dim
  grey) at the bottom right. The picked one is the brightest. Tap a block to pick it, let go of SHIFT and paint.
- **The four MUTE row buttons are four saved paintings.** Tap one to load it; **hold one for a second** to save the
  canvas into it. A saved slot's button is lit green, an empty one dim. The screen says what happened.
- **SHIFT + STOP** clears the canvas (saved paintings are not touched).

The canvas stays until the Fire loses power; saved paintings are kept in flash (32 bytes each, save kinds 57-60) and
survive the save area's clean-up. The colours go through ORC mode's brightness curve. The knobs do nothing here.

## UNIVERSAL mode (v0.31)

**Tap NOTE on the keyboard** (so: tap NOTE twice; v0.38, it was PERFORM twice) for UNIVERSAL: the Fire as a general MIDI
controller. NOTE again goes back to the keyboard; STEP / DRUM / PERFORM leave it too. NOTE's yellow light blinks.
The sequencer keeps running underneath. Everything is **stored on the Fire**, so it works with no Pi (e.g. through the
Kenton USB host). The ORC's web page sets pages up (host commands 10-15); the Fire can edit them too.

- **8 pages**, GRID < > (the screen shows `Univ P3/8` and the page's name). A page never set up is empty: nothing
  plays, nothing starts by itself.
- **Pads:** each of the 64 can be a note, a note latch, a CC hold (on value, off value on release), a CC toggle, a CC
  press (on value only) or a program change, on any channel, in a colour. Dim = idle, bright = held or latched on.
  A pad can have a **second assignment played while SHIFT is held** (no second assignment: SHIFT plays the first).
- **Knobs:** 4 banks of 4 per page, KNOB MODE steps the bank (CHANNEL / MIXER / USER 1 / USER 2 lights). A knob sends
  a CC or pitch bend; it remembers its value (absolute, 0-127; pitch bend 64 = centre). The screen shows the bank's 4
  knobs with their values; the one turned last is marked.
- **Not saved:** knob values and latch / toggle states start fresh at power-on (each knob at its start value).
- **Latched notes** always end with exactly the note they started, and they end by themselves when the page changes,
  when you leave UNIVERSAL, when that pad is edited / cleared / copied onto, and when the ORC rewrites the page. Up to
  16 notes can be latched at once (a 17th says `16 latched` and plays nothing). CC toggles keep their state per page
  (nothing hangs on a CC), but start off again when the ORC rewrites the page.
- Row buttons do nothing here.

**Editing on the Fire** (hold ALT; the web page is the main way, this is for quick changes):

| Do | Gets |
|---|---|
| ALT + tap a pad | edit that pad (it blinks white) and hear it: it plays what it does now. With SHIFT also held: its SHIFT assignment |
| ... knobs 1-4 | 1 type, 2 channel, 3 number (note / CC / program), 4 on value (velocity for notes) |
| ... KNOB MODE | knob page 2: 1 off value, 2 colour (CHANNEL / MIXER light = page 1 / 2) |
| ... knob 1 to "off" | clear the pad (that layer) |
| ALT + tap it again | play it again with its new settings |
| ALT + hold pad A, tap pad B | copy A to B (the layer A was picked on) |
| ALT + turn a knob | edit that knob (the first turn picks it): 1 type (none / CC / pitch bend), 2 channel, 3 CC, 4 start value |
| let ALT go | done. Saved like any edit: 3 s later (AUTOSAVE off: SAVE NOW). Up to 8 edits wait at a time |

**Edits made here vs. the ORC.** The page the ORC writes wins: if it rewrites (or clears) a page that has edits made on
the Fire and not saved yet, those edits are dropped and the screen says `edits replaced`. And if unsaved edits can no
longer fit the budget when it is time to save them (only possible when the ORC made other pages bigger meanwhile),
they are dropped and the screen says `edits dropped`; they never stay waiting with no way to save them. While the ORC
writes a page, its new size is already counted, so an edit here that would not fit says `pages full` at once.

**Why a budget.** The pages live only in flash (RAM has no room for them). When the save area fills up it is erased
and rewritten (only while stopped); for that moment the pages have to sit in RAM, in the undo buffers and the MIDI
batch buffer. So all 8 pages together are kept within 4096 bytes (`14` reports it; the Fire refuses an edit with
"pages full"). A page's size = 9 + 6 per assigned pad / layer / knob. A clean-up drops the undo history if any page
exists. Rolling back to a firmware older than v0.31 keeps the pages until its first clean-up, then they are gone.

## The screen

(v0.15 redesign: the screen is mostly pictures now.)

- **Line 1:** pattern (and the queued one, e.g. `PAT 3>4`) or the song line, then the tempo. A `*` in
  the corner means there are unsaved changes.
- **Line 2:** a PLAY / STOP icon, what you're looking at (`P1/8` = step page 1 of 8, the 128-step maximum; it is the step page, not a pattern number, `FADERS`,
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
a bar look picked in the menu) is saved **20 seconds after the last one**, so
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

## The BROWSER menu, the host protocol (v0.15, 2026-10-13; updated for v0.39)

### The BROWSER menu
Press **BROWSER** (it lights). **SELECT** turns to move, **pushing SELECT** opens an item to change
(turn) and confirms; BROWSER backs out. PLAY / STOP still work.

The top page, in order: **Autosave**, **Save now**, **ORC mode**, **MCU/HUI/DAW**, **Paint**, **DAW protocol**,
**Playing >**, **Knobs >**, **Display >**, **Memory >**, **Shortcuts >**, **About >**. Pushing SELECT on an item with
`>` opens its page; BROWSER goes back up one page (landing on the row you came from), and closes the menu from the
top page.

| Item | What it does |
|---|---|
| **Autosave** | ON / OFF (see Saving). |
| **ORC mode** | v0.33, **needs the ORC**: the ORC takes the pads, buttons, knobs, SELECT and screen; the sequencer keeps playing. SHIFT + BROWSER comes back. |
| **MCU/HUI/DAW** | v0.36: **DAW mode**, the Fire as a Mackie Control / HUI surface for a computer (see "DAW mode"). SHIFT + BROWSER comes back. |
| **Paint** | v0.37: **PAINT mode** (see "PAINT mode"). SHIFT + BROWSER comes back. |
| **DAW protocol** | v0.36: auto / MCU / HUI, for DAW mode. Saved. |
| **Playing >** | **Tempo** (SHIFT = steps of 10), **Swing**, **Clock out** on / off: the same values SHIFT + SELECT, knob 1 and SHIFT + PLAY change. **Pad release** (v0.28; off / 10 / 20 / 30 / 40 / 60 ms, default 20): how long a pad you play live (NOTE, DRUM, the MPC remote) must stay up before its release counts. A pad that flickers as you land on it or roll off it no longer plays a second hit; back down sooner = the same hit. The press itself is never delayed. Raise it if hits still double, lower it for very fast repeats on one pad. |
| **Knobs >** | **Section** (v0.29: step / note / drum / MPC; it opens on the one the pads are in), **Bank** (that section's current one; same as KNOB MODE), **Knob** 1-4 to set up, then what it **Sends** (swing, tempo, CC 1-127; SHIFT = steps of 10), its **Channel**, its **Mode** (relative / absolute). |
| **Display >** | **Beat marks** on / off, their **Colour** (blue, white, red, green, yellow, purple, cyan) and **Spacing**: every beat (4 steps), every bar (16), or beat+bar (the bar mark brighter). They use the pads' lowest brightness. |
| **About >** (v0.35) | Read-only, for bug reports (take a photo of it): **Version** (the release, e.g. v0.35b), **Build** (the build id, the same as `ORCBUILD` in the image), then the crash on record: **Crash** (none, HardFault, watchdog, TIM4 slip ...), **In** (what the Fire was doing: screen, buttons, saving ...), **PC** / **LR** (the two addresses that, with the build, say where in the code; v0.35b: these six fill the first screen, so one photo is a whole report), **Crashes** (how many since the record was last cleared), **Uptime** (how long it had run when it crashed), and **Misreads** (the TIM4 one-bit misread counter, live). The record is the one read at power-on; host `06 01` clears it. |
| **Memory >** (v0.26) | Read-only: **Drum steps left**, **Note steps left** and **Memory used**: how many more lit steps still fit in the pool all 32 patterns share before the screen says FULL (a drum step takes 3 bytes, a melodic step 6; a pattern counts as the larger of what it holds now and what is saved). |
| **Shortcuts >** | read-only cheat sheets: Steps, Hold a step, Hold a row, Patterns, Song editor, Playing, Note mode, Drum mode, Faders, Knobs. SELECT scrolls, BROWSER goes back. |
| **Save now** | writes anything waiting; says `saving...` or `nothing new`. |

**Gone:** the light shows (v0.31r: PLASMA, SPARKLE ... WAVE, TRACKS, the scrolling text, CYCLE, the light speed, Idle
start), then the **Pi VU** bars and the **ORC pads** page (v0.36, to make room for DAW mode). The saved settings keep
their layout (the old bytes are simply unused), so nothing in the save area changes meaning.

### The host protocol (a computer or the Pi -> the Fire)
SysEx on the **first** port (`FLAME seq`), id `7D` (non-commercial), then `ORC` (`4F 52 43`) and a command. Most of it
is for the ORC; `00`, `06`, `07` and `08` are useful to anyone (the crash readout for bug reports uses `06` / `08`).

| Command | Data | Meaning |
|---|---|---|
| `00` | | hello: the Fire replies `F0 7D 4F 52 43 00 <version> F7` (the version list is under this table) |
| `01` `02` | | **gone since v0.36** (the Pi VU levels and on / off): ignored |
| `03` `04` `05` | | **gone since v0.31r** (light speed, a show, scrolling text): ignored like any unknown command |
| `06` | optional `01` | the last crash on record: the Fire answers with a 91-byte record, version 3 (cause, main-loop stage, PC, LR, status registers, uptime, build id, SP, stack low point, the TIM4 check's note; see `crash.h`, decode with `host_crash.py`; version 2 = 61 bytes without the TIM4 note, version 1 = 46 bytes, no build/stack); `06 01` also clears it |
| `07` | | status (read only, v0.17): the Fire answers `F0 7D 4F 52 43 07` + 16 bytes: lights on, showing (mode, 127 = none; since v0.31r always 13 = the bars when on), host bars, host-started, pinned (always 0 since v0.31r), unsaved (`*`), lazy save armed, settings saves since boot (mod 128), playing, external play, Show setting + speed (the saved bytes, unused since v0.31r), menu open, frames drawn (mod 128), and the deepest stack use since boot in bytes (2 bytes, high 7 bits first); since v0.19 the surface on + its channel; **since v0.31 the longest main-loop pass in microseconds** (2 bytes, high 7 bits first, 16383 = that or more; a clean-up while stopped takes ~1-2 s and shows as 16383): `07 01` answers the same and then starts the measurement over. `fire_live_test.py` uses it. |
| `08` | | the stack around the last fault (read only, v0.17c): `F0 7D 4F 52 43 08 01 <valid> <base: 5 bytes> <64 words x 5 bytes> F7` (332 bytes of data). Decode with `host_crash.py`. |
| `09` | `<address: 5 bytes>` | read one 32-bit word (v0.17d, for debugging): answered `F0 7D 4F 52 43 09 <the address as sent> <value: 5 bytes> 00 F7`. An address that isn't RAM, flash or a register faults (the Fire restarts with a crash record). |
| `0A` `0B` | | **gone since v0.36** (the ORC pads page's colours and on / off): ignored. `0C` (its pad presses, Fire -> ORC) is never sent. |
| `0D` | `<n 0-31>` | **queue a pattern** (v0.30, for the ORC's scenes): exactly what PATTERN up / down and the pattern picker do. Playing (own or external clock): pattern n starts at the next bar (the PATTERN light blinks, the screen shows `>n`); n = the pattern already playing clears the queue. Stopped: it switches at once. **Song mode:** n is a song SLOT (0 = the first), the song jumps there at the bar; a slot past the song's end is refused. The Fire answers `F0 7D 4F 52 43 0D <ok 0/1> <current pattern 0-31> <queued: pattern or slot, 127 = none> <song 0/1> F7`. |
| `0E` | `0` / `1` | **bar messages** off / on (v0.30). Off at power-on: nothing is sent until the host asks. |
| `0F` | **Fire -> ORC** (v0.30, while `0E 01`): `<pattern 0-31> <song slot, 127 = no song> <bar 0-127>` | a bar starts (step 0, 16, 32 ... while playing, own clock or external): sent right after that step's notes, the pattern = the one now playing (after a queued switch). The bar count runs from the start (0) and wraps at 128; a CONTINUE mid-bar sends nothing until the next bar. |

| `10` | `<page 0-7> <len hi> <len lo>` | **UNIVERSAL page write, begin** (v0.31). `len` = the page's size in bytes (9 + 6 x entries, 9-873; 14 bits, high 7 first). Answer: `F0 7D 4F 52 43 10 <result> <page> <done hi> <done lo> F7` (all writes answer in this shape; done = bytes in flash so far). A begin during an unfinished write restarts it (the old one never counts). |
| `11` | `<offset hi> <offset lo> <1-24 bytes>` | the next chunk: offset = the bytes sent so far (chunks go in order), 24 bytes each except the last. Answered (`11 <result> ...`) once the chunk is IN FLASH: send the next one after the answer. |
| `12` | `<sum hi> <sum lo>` | commit: sum = every byte of the page added up, 14 bits. The Fire checks the page in flash (sum + every field, see below) and only then makes it count; answered `12 0 ...` when it does. A wrong sum / bad page: answered 4 / 5, the page never counts, the old one stays. |
| `13` | `<page> <offset hi> <offset lo>` | read a page AS THE FIRE PLAYS IT (edits made on the Fire not saved yet included): `F0 7D 4F 52 43 13 <result> <page> <len hi> <len lo> <offset hi> <offset lo> <up to 24 bytes> F7`. A page never written reads as the 9-byte empty page. |
| `14` | | the page list: `F0 7D 4F 52 43 14 <8 pages> <budget hi> <budget lo> <used hi> <used lo> <page being written, 127 = none> <current page> <UNIVERSAL showing 0/1>` then for each page 0-7: `<len hi> <len lo> <sum hi> <sum lo> <name: 8 bytes>`, `F7`. |
| `15` | `<page>` | clear a page (write the empty page); answered `15 <result> ...` when it counts. |
| `1A` | `<n 0-9>` | **the DAW page** (v0.31d) has n rows (0 = no page; it leaves the screen if it was showing). Rows from n on are cleared; rows below n are kept. n > 9 is ignored. No answer. RAM only. |
| `1B` | `<row 0-8> <R> <G> <B> <level hi> <level lo> <name: 0-8 bytes>` | one DAW row in full: colour (0-127 per channel, e.g. REAPER's track colour; shown scaled so the brightest channel is full, black = white), a 14-bit level (v0.31f; hi = the top 7 bits; the pads show the top 7), a name (cut to 8, non-ASCII = `?`). Never sent back. No answer. |
| `1C` | `<row 0-8> <level hi> <level lo>` | one DAW row's 14-bit level only (v0.31f; a DAW fader moved). Never sent back. No answer. |
| `1D` | **Fire -> host** (v0.31f): `<row> <level hi> <level lo>` | the user moved a DAW row on the Fire, once per change. A pad tap jumps to its column's level exactly as in FADERS; a knob moves the full 14-bit level with acceleration (a slow click = 1 of 16383, quicker turns 8 / 32 / 128). Never for a level the host set. |
| `1E` | **Fire -> host** (v0.31f): `<0|1> <count 1-127>` | SELECT turned on the DAW page (`00` = back / up, `01` = on / down; count = detents, SHIFT + PATTERN = 4). The page never scrolls itself: the host moves its list (the Pi: one REAPER track per detent, the tracks then MASTER). |
| `20` | `<sub> ...` | **remote input** (v0.31r, for troubleshooting: `fire_press.py` plays the Fire from the Pi). `20 00 01` arms it (and keeps it alive), `20 00 00` disarms (every input it holds is released); `20 01 <input 0-87> <1 press / 0 release>` (the input numbers of `fire_map.h`: pads 0-63, buttons 64-87); `20 02 <0-3 knob, 4 SELECT> <1 clockwise / 0 back> <detents 1-127>`; `20 03` releases everything it holds. Every one is answered `F0 7D 4F 52 43 20 <sub> <result: 0 ok, 1 not armed, 2 bad, 3 full> <armed 0/1> F7`. **Off at power-on**; armed, it ends by itself 5 s after the last `20` (a dead script can't leave SHIFT held). The presses and turns go through exactly the code real ones do (SHIFT held from the Pi lights SHIFT and makes a pad an accent); only the button scan itself is skipped. At most 7 inputs can be held from the host at once. |
| `21` | `0` / `1` / `2` | **ORC mode** (v0.33) off / on / "still here". Always answered `F0 7D 4F 52 43 21 <0|1> F7` (the state now); the Fire sends the same frame by itself whenever the state changes (the menu, SHIFT + BROWSER, the 5 s quiet timeout). While on, the host must send any `21` at least every 5 s. |
| `22` | **Fire -> host** (v0.33, ORC mode): `<note> <vel>` | a pad (notes 54-117, 54 = top left, velocity 100) or a button (Akai's note numbers: STEP 44 .. REC 53, PATTERN 31 / 32, BROWSER 33, GRID 34 / 35, SELECT push 25, KNOB MODE 26, the row buttons 36-39; velocity 127) pressed, or let go (velocity 0). SysEx and not notes, because the sequencer's own notes leave on the same port. |
| `23` | **Fire -> host** (v0.33, ORC mode): `<k> <rel>` | knob k (0-3) or SELECT (4) turned: 1..63 = that many detents up, 127..65 = down. |
| `24` | `<10-30>` | the ORC-mode pad colour curve x 10 (v0.33): 10 = straight, 20 = squared (power-on), 30 = cubed. Not saved; applies to colours sent after it. No answer. |
| `25` | `<12-64>` / `00` | ORC mode's short LED slot (v0.34): sets the share its colours are split by; `25 00` only asks. Answered `F0 7D 4F 52 43 25 <share> <measured slot, us> <interrupt time, us> F7`. |
| Akai `65` / `0E` | as Akai's firmware takes them | **only in ORC mode** (v0.33): `F0 47 7F 43 65 <len hi> <len lo> {pad, R, G, B}... F7` pad colours (0-127 each), `F0 47 7F 43 0E <len hi> <len lo> <band0> <band1> <col0> <col1> <bits> F7` a screen bitmap (Akai's 7-bit packing, any window). Any length (they do not go through the 40-byte command buffer). Ignored outside ORC mode. |

Host values in `1A`-`1C` above 127 are clamped (a correct 7-bit frame never has them); a whole frame longer than 40 bytes is dropped, as for every command.

**Results:** 0 OK, 1 bad arguments, 2 busy (another write or a save is running: retry in ~50 ms), 3 out of order (no write begun, a wrong offset, a commit before the last chunk), 4 wrong sum, 5 bad page, 6 full (all pages together over the budget), 7 no room while playing (the save area needs its clean-up: stop the Fire and retry), 8 a flash write failed. A write with no chunk / commit for 3 s is dropped.

**The page format** (every byte 7-bit, exactly what is stored): `[0]` version 1, `[1-8]` name (ASCII 32-126, spaces = none), then entries of 6 bytes, in any order, one per pad + layer and one per knob:
- pad: `<pad 0-63> <layer (0 base, 1 SHIFT) + colour x 2 (0-15 the fader colours, 16 grey)> <type x 16 + channel 0-15> <number> <on value> <off value>`. Types: 1 note (velocity = on value; off on release), 2 note latch (on / off on alternate presses), 3 CC hold (on value, off value on release), 4 CC toggle (on value / off value on alternate presses), 5 CC press (on value on press only: the old /control CC), 6 program change (number).
- knob: `<64 + slot 0-15 (bank x 4 + knob)> <type 1 CC / 2 pitch bend> <channel 0-15> <CC> <start value> <0>`.

A pad or knob with no entry does nothing; pads are numbered `row x 16 + column` from the top left. The budget (all 8 pages together) is 4096 bytes: 8 pages of 64 pads + 16 knobs take 3912, the rest is room for ~30 SHIFT assignments. (Why a budget: see "UNIVERSAL mode".)

The hello's version: 1 up to v0.29, **2** in v0.30 (`0D` / `0E` / `0F`), **3** in v0.31-v0.31c (the UNIVERSAL pages, `10`-`15`),
**5** since v0.31d (the DAW page, `1A`-`1E`; 4 belonged to the rolled-back v0.32 and is never used); **6** since v0.31f (14-bit DAW levels, `1E <dir> <count>`). A host that sees 6 speaks the current DAW page. **7** since v0.31r (`20` remote input; `03`-`05` gone). **8** since v0.33 (`21`-`24` ORC mode). **9** since v0.34 (`25`); still 9 in v0.39 (v0.36 only dropped commands).

**Senders (on the Pi, `~/fire_fw/`):**
- `fire_press.py` plays the Fire remotely (command `20`, v0.31r): see `20` above. (`host_text.py`, command 05, went with the shows.)
- `host_crash.py` reads and decodes the crash record (`06` / `08`), see Crashes.
- `host_vu.py` (the Pi VU sender, service `orc1-firevu`) is retired: the firmware ignores its `01` / `02` since v0.36.

### Crashes (since 2026-10-13)
**If your Fire crashes** (it goes dark and restarts by itself, or ends up dark in update mode): open **BROWSER >
About** and take a photo of the first screen. Version, Build, Crash, In, PC and LR together say where in the code it
happened. Put the photo in a bug report (see the README). **Since v0.39 the last crash survives unplugging**, so there
is no hurry: unplug, take the Fire home, photograph About later. If the Fire is stuck dark in update mode (3 crashes in
a row), run Install again (keep your patterns), then look at About.

On 2026-10-13 at 21:00 the Fire fell into DFU (update mode) 63 minutes after a flash, with no sign of why: every fault
handler just rebooted to DFU. Now a **fault or a watchdog reset is recorded and the Fire restarts itself** in 2-5 s:
- The record lives in the RTC backup registers (they survive a reset): the cause (HardFault / MemManage / BusFault /
  UsageFault / watchdog = a hang), the PC and LR, the fault status registers and fault address, the uptime, and which
  stage of the main loop was running (USB + MIDI, sequencer, queues, input events, gates, flash saver, lights, knobs,
  paint, render + OLED, loop tail, start-up).
- The header's status slot shows `FAULT RST` or `WDOG RST` after a restart.
- **v0.39: the record is also kept in flash** (save kind 61, 64 bytes), because the backup registers do not survive a
  power-off (the Fire has no backup battery). At boot, a crash in the registers that flash does not hold yet is copied
  there (on its own, at a quiet moment, even with AUTOSAVE off); with nothing in the registers, the flash copy is the
  crash on record (About, `06`). `06 01` clears both. A boot with no crash writes nothing. The stack snapshot (`08`)
  stays RAM-only: it is lost at power-off.
- **A crash loop still falls back to DFU:** 3 crashes in a row, each within 30 s of booting, leave it in update mode so a
  bad image can be replaced without a debugger.
- Read it with `python3 host_crash.py [--elf fireseq.elf]` (the PC becomes a source line); `--clear` clears it. The
  ORC also saves any new crash the Fire reports when it connects.
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

With the installer from the release page: **Restore original Akai firmware** (`.bat` on Windows, `.command` on macOS /
Linux) puts back the copy of Akai's firmware that Install saved from your Fire the first time. From the owner's Pi:
`~/fire_fw/fw/fire_flash.sh stock` (see `fw/DEVELOPER.md`). Saved patterns stay in their own flash area and come back
if this firmware is flashed again (Restore clears them, after saving them in a backup).
