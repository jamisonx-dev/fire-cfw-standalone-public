# Pad velocity: research notes (unsolved as of 2026-10-06)

The pads report on/off on the button chain (see the hardware map), but **how hard** a pad is hit
comes from somewhere else. Akai's firmware gets it; ours doesn't yet. This file records everything
tried, so the next attempt starts where this one stopped.

## Ground truth: Akai's firmware does sense velocity

Measured 2026-10-06 (stock firmware, aseqdump on the Pi, one pad, the owner hitting soft -> hard):
soft taps **32** (= the velocity curve's floor), medium about **54-63**, hard up to **123**.

## What Akai's firmware does (summary of its code, read in the emulator)

- **ADC setup:** TIM5 channel 1 at 8 kHz triggers ADC1. Each trigger runs a 16-conversion scan
  alternating PA0 / PA1 (ch0, ch1, ch0 ...), 15-cycle sampling, ADC clock 21 MHz, circular DMA
  (DMA2 stream 0) into a RAM structure near 0x2000370C. Register values: SQR1 0x00F08020,
  SQR2/SQR3 0x02008020, SMPR2 0x9, CR1 SCAN, CR2 0x1A000301 (rising edge of TIM5_CC1), TIM5 ARR 10500.
- **Processing** (main loop, around 0x0800DE60-0x0800E020): takes the PA1 samples, runs them through
  an 8-tap symmetric smoothing filter (coefficients -672, -1208, 4310, 13910, 13910, 4310, -1208, -672
  in Q15: gain about 1.0, so it does NOT amplify), loops over 8 positions, waits for a value above
  **280**, tracks the peak, and settles after a short delay or once the signal starts falling.
- **Mapping** (function at 0x08011B0A): clamp the peak to **280-800**, scale to 0-127, look up the
  128-step velocity curve in Akai's settings sector (0x08004015, starts at 32).
- **Pad handler** (around 0x08011B50): a pad's chain bit going on marks the pad "pending"; the
  velocity is attached when the measurement above completes.

So with Akai's firmware, PA1 must reach **hundreds** of counts on a real hit.

## What we measured with our probe firmware

| Setup | PA1 on a hit |
|---|---|
| Slow scan (20 Hz), selector 0-7, LEDs disabled | ~14 at rest, no visible change |
| Same, LED drivers enabled (PA7 low), all LEDs off | one blip ~105 on all positions at once |
| Fast capture ~43 kHz, selector parked on one position | hard ~90-120, soft = noise (~23) |
| Selector swept at ~13k sweeps/s, 28-cycle sampling | ~20-115, **equal on all 8 positions**, not force-related |
| Same with all LEDs on | same |
| Fast capture, 480-cycle sampling: raw / smoothed / steepest rise | hard ~20-30 above rest, soft and medium invisible |
| + PC4 and PC5 driven low | no change |
| **Akai's exact ADC setup** (TIM5 8 kHz, 16-conversion ch0/ch1 scan, 15-cycle) | hard ~15-30, soft invisible |

Conclusion so far: the PA1 path responds a little, but something Akai's firmware sets up is
missing. The small blip being equal on all selector positions suggests it is electrical spill from
the press, not the real sensor output.

## Tried on the hardware 2026-10-06: factory test mode (SysEx 0x06)

Akai's firmware restored, `F0 47 7F 43 06 00 01 01 F7` sent (with the ORC brain still drawing on the
screen). Result: no separate test screen appeared, but **pressing a pad showed "pad 1", a coordinate
like "2,3" and three numbers, e.g. "243 255 243"**. After a while the numbers climbed to 255 on their
own. A full readback afterwards: bootloader, settings and app all **identical to the backup** (the
change lived only in RAM).

Reading: the three numbers match the per-pad triples stored in Akai's settings sector (e.g. F3 FF F3),
and the hidden screen's header is "RED GREEN BLUE". So they are most likely **per-pad LED colour
(white-balance) calibration**, not pad sensitivity: this mode is an LED/pad test, not a velocity
readout. "Cal TH" (one number) may still be a pad threshold, but it never appeared.

## Emulator trace of the ADC burst interrupt (2026-10-06, `tools/emu_adc.py`)

Akai's firmware run in the emulator with its ADC end-of-burst interrupt (DMA2 stream 0, IRQ 56,
handler 0x08008F35) fired ~8000 times per simulated second on synthetic samples (PA0 and PA1 marked
differently):

- The handler reads **all 16 samples** of the burst (8 PA0, 8 PA1).
- Between bursts it changes **only PC0-PC2**, stepping the selector in **Gray-code order**
  (1, 3, 2, 6, 7, 5, 4, 0). No other pin or peripheral (besides clearing the DMA flags).
  -> the selector stepping is real, but our hardware test with per-burst stepping (binary order)
  still showed no velocity signal, so the order is unlikely to matter.
- It stores, **per selector position, both channels**: for each of PA0 and PA1 the burst's
  sum of 8 samples, a 0 (likely an accumulator), the minimum and the maximum. Block at
  0x20000F10-0x20000F9F (8 positions x 2 channels x 4 halfwords).
- **~~Readers of that block~~ (CORRECTED 2026-10-14: this was a false lead).** The narrow-hook run
  reported code at **0x08010208-0x0801020E** and **0x080108EE-0x0801091A** "reading the store about
  200k times each". Disassembled, those are ST's USB OTG-FS driver routines: core reset
  (wait AHB idle, set CSRST, wait clear) and RX/TX FIFO flush. Register offset +0x10 is
  OTG_FS_GRSTCTL; each wait loop times out after 0x30D40 = 200,000 tries. The emulator's fake USB
  registers never changed, so they spun to the timeout: that is the 200k (and the same
  "stalls in the USB transmit path" noted below). Not measured by re-running, but register meaning,
  bit positions and the 200k all match. NOT velocity code.
  **Static scan instead** (literal-pool references in the stock app): the ADC DMA buffer
  0x2000370C is loaded at 13 sites, all within 0x08010D80-0x080117F8 (ADC1 0x40012000 at
  0x08010D68, 0x080111A0, 0x08011498): the velocity code very likely lives there. The per-position
  table 0x20000F10 is loaded at 0x0800FED0 (0x20000F14 at 0x080112BC, 0x080175F8). Start there.
- Earlier attempts to find the readers: Runs past ~6 M
  instructions stall in the USB transmit path (knob CCs triggered by the synthetic samples run away
  on the fake USB registers); with that routine stubbed out, a run tracing every RAM access was far
  too slow (stopped after an hour).

Emulator gotchas found on the way (for whoever continues):
- Never stop Unicorn inside an IT block (resume loses the condition): break only when the XPSR IT
  bits are 0.
- A memory hook over all of SRAM during start-up broke Unicorn's IT-block LDM/STM: attach it late.
- Respect PRIMASK/BASEPRI before firing simulated interrupts.
- Hook a NARROW address range (just the store block) for reader tracing; hooking all SRAM makes
  every load/store a Python call (that's what made the last run crawl). Print progress.

## Akai's velocity code, read from the stock image (2026-10-14, static disassembly only, nothing run)

Source: `fire_flash_v0.21_a.bin` (a stock dump); disassembly helpers in `tools/` (`fdis.py` stops at data,
so a skip-data sweep of 0x08008000-0x08020000 was used). Addresses are stock-firmware addresses.

- **One shared measurement, not per pad.** Main-loop function `0x0800DE08` gets, once per frame, 8 values
  (PA1, "positions" 0-7) from the ADC driver via `0x080112C4`, runs the 8-tap FIR (`0x08015950`, Q15,
  the coefficients listed above) over them *treated as a time series* (current frame + previous frame),
  and tracks ONE peak: it starts when filtered[1] > **280** (0x118), ends after the value has fallen
  twice and 10 ms have passed. It then calls `0x08011B0A` (clamp 280-800 -> curve) and applies that ONE
  velocity to **every pad whose button bit went on during the measurement** (loop over 64 pads,
  `0x08010AAA` = note-on with velocity). So PA1 is a single analog feed for the whole pad area.
- **The ADC driver** is an object at RAM `0x2000370C`, set up by `0x080112DC` (called from `0x08011298`
  with the config bytes at `0x080132BA`): **ADC pins `00 01` = PA0, PA1; selector pins `20 21 22` = PC0, PC1, PC2**
  (pin id = port<<4 | pin; terminators 0x10 / 0xB0). ADC pins -> analog mode, no pull. Selector pins ->
  **push-pull output, no pull, very-high speed, initial level 0**. Timer params 1250 / 1000 (-> the 8 kHz trigger).
- **Selector stepping** is `0x08011524`, called at the end of each burst from the DMA-complete callback
  (`0x08010D60` -> `0x08010E64` -> `0x08010DE4`): reset the next position's accumulators (sum, min = 0xFFFF,
  max = 0), advance the position counter, wrap at 2^bits (8), then write the **Gray-code table at
  `0x08016A1C`** (`00 01 03 02 06 07 05 04 ...`, 5-bit) to the selector pins. One burst per position, so
  the 8 positions are 8 consecutive bursts (~1 ms per full sweep at 8 kHz). Settings (curve + LED trim)
  are copied to RAM `0x20001018` at start-up (`0x0800E02E`).
- **Not yet tried on hardware:** our earlier "per-burst stepping" probe used BINARY order, not this
  Gray order, and was not tied to the DMA-complete interrupt; nothing we ran reproduced Akai's exact
  *timing* of the pin change relative to the next trigger. The gap to close is large (Akai needs peaks of
  hundreds of counts, ours saw 15-30), so suspect a missing enable, not a subtle ordering effect.
- Still unknown: what PA0 is for (it is sampled alongside PA1 but the velocity path only reads PA1),
  and the meaning of the `tbb` modes in `0x080115DC` (mode 1 is what the velocity path asks for).

## Result 2026-10-14: the signal was there all along (velprobe, measured on the Fire)

`fw/velprobe/` = Akai's chain reproduced (above); log `~/fire_fw/velprobe_run1_20261014.txt` (Pi).
Resting level ~200 on every selector code (noise +/-30, never reaches the 280 gate). the owner hit pads:
- light / ordinary presses: peak 214-260 (+15..60 over rest), gate never opens -> **32**, exactly like the
  stock firmware's "soft taps 32";
- firmer hits: peak 372 / 438 / 456 / 474 / 553 / 603 / 708 -> velocity **48 / 60 / 63 / 67 / 81 / 90 / 109**,
  in the stock range (medium 54-63, hard up to 123).

So the earlier conclusion "PA1 must reach hundreds of counts, something is missing" was a UNITS mistake: the
stock thresholds are on the driver's x16 scale (280-800 = ~17-50 raw counts), and our old probes' 15-30 count
hard hits were the real signal. The signal is global (same on all 8 selector codes), as the stock code assumes.
**Run 2 (labelled, one pad, `velprobe/vel_run2_soft_then_medium.txt`):** 10 soft = +20 over rest
(sd 3.7, range 15-25), 10 medium = +22 (sd 3.8, range 18-30). Idle noise alone reaches +20..35 over 30 ms,
so **soft and medium are indistinguishable**: the sensor only responds to firm strikes. Lowering the gate
cannot help (noise reaches ~236). Usable: "normal" (32) vs graded firm hits (~48-110). Finer soft/medium
control would need another source (e.g. a base-velocity knob, firm hits as accent on top).
Not yet done: the same labelled run on the stock firmware (optional), and velocity in fireseq itself
(stock note timing: the note is held back until the peak falls twice after 10 ms, or 20 ms).

## Leads, best first (pre-2026-10-14; superseded by the result above)

1. ~~**Akai's hidden factory test screen.**~~ Tried (above): an LED/pad test, not a velocity readout.
   Kept for the record: Its firmware contains a test screen ("FW Vers", "RED GREEN
   BLUE", "Cal TH: %d", three numbers per line): very likely live pad readings and calibration
   thresholds. **Found 2026-10-06: SysEx cmd 0x06 (one data byte, non-zero) enters it** (handler
   0x0800BA70 -> 0x0801200C; the screen is drawn by 0x08011D00). Nothing reachable from it (55
   functions) writes flash or the settings sector. Emulator check (`tools/emu_stock.py --call 0x0801200c --arg 1`): the call returns, the only
   flash-controller writes are the normal ACR (wait-state) setup, nothing programs or erases. The
   emulator can't show the screen (no real pad data). Next: flash Akai's firmware, send `F0 47 7F 43 06 00 01 01 F7`, and read the screen while hitting
   pads. ⚠ 0x78 and 0x79 write the settings: never send those.
2. **Step the selector per burst, like Akai's firmware probably does** (PC0-PC2 changed between
   bursts, so each burst reads one position). Our "Akai ADC setup" test held the selector still.
3. **Find a missing enable.** Candidates: PC4/PC5 driven HIGH (only low was tried); PB9/PB10 (Akai's
   firmware sets them as pulled-up inputs at start-up; maybe reconfigured later); a chain output bit
   used as a sensor strobe (bits 192/199 were odd; our tests used all-on or all-off, not Akai's
   running pattern); any pin first configured after start-up (the emulator never got that far).
4. **Run Akai's firmware further in the emulator.** (Partly done, see above: next, trace readers of
   the 0x20000F10 block with a narrow hook and a simulated pad press.) `tools/emu_stock.py` stops when its SPI2
   interrupt handling trips the ST driver's zero-length check (it expects transfer counters our DMA
   stand-in doesn't model). Fixing that lets the main loop run with simulated pad presses, and the
   log would show every pin/peripheral it touches while measuring.
5. **ST-Link on the live Fire** (the owner has one): watch Akai's ADC buffer near 0x2000370C in real
   time while hitting pads. The most direct answer, but it needs the Fire opened and SWD wired
   (PA13/PA14, GND, 3V3).

## Until then

The sequencer doesn't need velocity (pads toggle steps). A live-play mode can start with a fixed
velocity, or use per-pad accents.
