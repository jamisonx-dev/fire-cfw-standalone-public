# Akai Fire hardware map

Everything the custom firmware needs to drive an Akai Fire (fw 0.21 unit, USB 09e8:0043).

**How it was found.** Start-up behaviour came from running Akai's own firmware in an emulator
(`tools/emu_init.py`, `tools/emu_stock.py`) and logging every hardware register it touched.
Everything marked **measured** was then confirmed on the real Fire with probe firmwares
(`fw/probe/`), with the owner pressing buttons and watching lights (2026-10-06). Anything still
**inferred** is flagged as such.

## The chip

| | |
|---|---|
| Part | **STM32F401RC**, 64-pin. (The firmware uses PC0-PC12 and PD2, which only exist on the 64-pin package; interrupt table and memory sizes match the F401xC.) |
| Crystal | **8 MHz** external. PLL M=8 N=336 P=4 Q=7 -> 84 MHz core, exactly 48 MHz for USB (`RCC_PLLCFGR = 0x07415408`) |
| Clocks | Core 84 MHz, APB1 42 MHz (its timers run at 84 MHz), flash 2 wait states |
| RAM | 64 KB |
| Security | Read protection level 0 (unlocked), no write protection |
| Debug | SWD (PA13/PA14) left at default, so an ST-Link can attach |

### Flash layout (256 KB)

| Address | Size | Contents |
|---|---|---|
| 0x08000000 | 16 KB (sector 0) | **Akai's bootloader. Never write this.** It is what makes remote recovery possible. |
| 0x08004000 | 16 KB (sector 1) | **Akai's settings:** a serial-number-like string (0x00), the velocity curve (0x15-0x93: 127 entries, a straight line 32 -> 127), a `0xDEADBEEF` marker (0x94), and per-pad calibration (0x98, 3 bytes per pad, most likely red/green/blue LED trim). Only Akai's firmware uses it. |
| 0x08008000 | 96 KB (sectors 2-4) | **The application** (Akai's or ours). Must end with the 16-byte marker `EndOfFlashToken` at 0x0801FFF0. |
| 0x08020000 | 128 KB (sector 5) | **Empty on a stock Fire.** Our firmware stores its patterns here. |

## Booting and update mode

Akai's bootloader runs on every power-on:

1. It checks for `EndOfFlashToken` at 0x0801FFF0. There is **no checksum** of the app.
2. It scans the buttons for the update combo (**STEP + NOTE + DRUM + PERFORM held while plugging in**).
3. Marker present and no combo -> it starts the app at 0x08008000. Marker missing, or combo held ->
   it hands over to **ST's ROM update mode** (USB `0483:df11`, "STM32 BOOTLOADER", `dfu-util`).
4. It passes this decision across a reset through RTC backup register **BKP0R** (0x40002850):
   1 = go to update mode, 2 = start the app.

Consequences:
- **A half-written app never bricks the Fire:** no marker -> update mode by itself.
- **Any firmware can reboot itself into update mode:** enable backup-register writes, set BKP0R = 1,
  reset. Akai's firmware does this on SysEx `F0 47 7F 43 72 00 00 F7` (cmd **0x72**); ours does the
  same on the same message.
- ⚠ **Never send Akai's firmware SysEx cmd 0x78 or 0x79**: both write Akai's settings sector.
  (0x78 was sent by mistake once on 2026-10-06; restored from the backup.)
- **SysEx cmd 0x06** (one data byte) enters Akai's hidden factory test mode. The same code also runs
  from start-up (probably a button combo). Nothing reachable from it writes flash (static check +
  emulator + a readback after trying it, 2026-10-06). On the hardware, pressing a pad then shows
  "pad N", a coordinate and three numbers that match the per-pad bytes in the settings sector: most
  likely each pad's red/green/blue LED calibration.

## Pins

| Pin | Function | Status |
|---|---|---|
| PA11 / PA12 | USB D- / D+ (AF10). No VBUS sensing. | measured (our firmware's USB works) |
| PB12 | Shift-register chain **latch** | measured |
| PB13 / PB14 / PB15 | SPI2 SCK / MISO / MOSI (AF5): the **button + LED chain** | measured |
| PA7 | **LED master enable, active low** (high = all LEDs dark) | measured |
| PC10 / PC12 | SPI3 SCK / MOSI (AF6): **OLED** data | measured |
| PC11 | OLED D/C (low = command) | measured |
| PC13 | OLED chip select (active low) | measured |
| PD2 | OLED reset (active low) | measured |
| PA2 | **OLED brightness**: low = bright. Akai's firmware holds it high (dim). | measured |
| PA0 | ADC1 ch0: the **4 knobs** (through the selector) | measured |
| PC0 / PC1 / PC2 | Analog **selector**, binary position 0-7 | measured |
| PA8 / PA9 | **SELECT encoder** (inputs, external pull-ups) | measured |
| PA1 | ADC1 ch1: pad strength signal (see "Pad velocity") | partly measured |
| PC4 / PC5 | Knob-touch drive (outputs in Akai's firmware) | inferred |
| PC6 / PC7 | Knob-touch sense (TIM3 input capture in Akai's firmware) | inferred (they react to fingers) |
| PB9 / PB10 | Inputs with pull-ups in Akai's firmware, purpose unknown | unknown |

## The button + LED chain (SPI2)

One transfer clocks **29 bytes out** (LEDs) and **29 bytes in** (buttons and pads) at the same time.

- **Sequence:** pulse PB12 low -> high, then clock 29 bytes. SPI mode 0, MSB first, 5.25 MHz.
- **Inputs:** a set bit = pressed. Bytes 0-10 carry inputs; bits 8.5-8.7 always read 1; bytes 11-28 read 0.
- **Outputs:** active low (0 = LED on). PA7 must be low for any LED to light.
- **Brightness:** the chain is plain on/off. Our firmware refreshes it at 2 kHz and blinks each LED
  for brightness (8 levels).
- Bit notation below: index = byte x 8 + bit (byte 0 = first byte on the wire).

### Inputs

| Control | Input |
|---|---|
| Pad at row r (0 = top), column c (0-15) | n = r x 16 + c -> byte 7 - n/8, bit n mod 8 |
| STEP / NOTE / DRUM / PERFORM / SHIFT | 75 / 76 / 77 / 78 / 79 |
| ALT / PATTERN-SONG / PLAY / STOP / REC | 64 / 65 / 66 / 67 / 68 |
| Row buttons 1-4 (top -> bottom) | 87 / 72 / 73 / 74 |
| Knob-mode / BROWSER / SELECT push | 86 / 83 / 82 |
| PATTERN "up" / "down" (as the firmware uses them) | 84 / 85 |
| GRID left / GRID right | 81 / 80 |

The pads on this chain are **on/off switches**. How hard a pad is hit comes from a separate analog
signal (below).

### LEDs

**Pads (output bytes 0-23).** Row r (0 = top), column c, colour j (0 red, 1 green, 2 blue):
`L = 3c + j`, `byte = (3 - r) x 6 + 5 - L/8`, `bit = L mod 8`. Each row is one 48-bit run of
R,G,B per pad, left to right, in its 6 bytes in reverse order; the bottom row comes first on the wire.

**Buttons (output bytes 24-28).** Y = yellow, R = red, G = green.

| Control | Output bits |
|---|---|
| STEP / NOTE / DRUM / PERFORM | 213 Y + 214 R / 215 Y + 200 R / 201 Y + 202 R / 203 Y + 204 R |
| SHIFT / ALT | 205 Y + 206 R / 207 Y |
| PATTERN-SONG | 192 Y + 193 G |
| PLAY / STOP / REC | 194 Y + 195 G / 196 Y / 197 Y + 198 G |
| Row buttons 1-4 (the button itself, green) | 217 / 220 / 223 / 210 |
| Row side LEDs, red + green | row 1: 218 + 219, row 2: 221 + 222, row 3: 208 + 209, row 4: 211 + 212 |
| GRID right / GRID left / BROWSER | 224 / 225 / 226, red |
| PATTERN "up" / "down" (as the firmware uses them) | 227 / 228, red |
| Knob-mode LEDs CHANNEL / MIXER / USER 1 / USER 2 | 229 / 230 / 231 / 216, red |
| (unused) | 199 |

All of this is in code form in `fw/fireseq/fire_map.h`.

## Knobs and SELECT

- **The 4 knobs are endless potentiometers with two wipers each**, read on PA0 through the PC0-PC2
  selector: knob 1 = positions 0 + 1, knob 2 = 2 + 3, knob 3 = 4 + 5, knob 4 = 6 + 7. Readings are
  steady to about +-6 of 4095. Our firmware turns the two wipers into an angle (`atan2`) and reports
  64 steps per turn; increasing angle = clockwise.
- **SELECT** is a quadrature encoder on PA8/PA9: both rest high and dip low once per click.
  Clockwise = PA8 dips first. Its push is chain input 82.
- **Knob touch** uses PC4-PC7 (the pins react to a finger); the method isn't reproduced yet.

## The OLED

- **SSD1306**, 128 x 64, on SPI3 (write-only, about 2.6 MHz in Akai's firmware).
- Akai's start-up sequence (captured in the emulator): `AE D5 80 A8 3F D3 00 40 8D 14 A1 C8 DA 12
  81 CF D9 F1 DB 40 A4 A6 20 00 22 00 07`, then a cleared frame, then `AF`. After that it simply
  pushes whole 1 KB frames (8 pages x 128 columns, top-left first).
- Our firmware uses the same sequence but contrast `FF` and PA2 low, because Akai's level was
  "almost unlit" (the owner). Frames go out by DMA (SPI3 TX = DMA1 stream 7, channel 0).

## Pad velocity (sensing solved 2026-10-14; not yet in fireseq)

- PA1 is ONE shared force signal for the whole pad area (same on every PC0-PC2 selector code).
- Akai's method (read from its code): TIM5 8 kHz -> 16-conversion ADC burst (PA0/PA1 alternating,
  15 cycles), PC0-PC2 stepped in Gray order once per burst from the DMA-complete interrupt; per 8 bursts,
  each PA1 burst average x16, an 8-tap FIR of gain 1; a peak counts once filtered > 280, clamp 280-800,
  scale to 1-127, look up the curve. One velocity goes to every pad pressed during the measurement.
- **Measured with `fw/velprobe/` (our firmware, Akai's chain):** rest ~200, idle noise up to ~236; light
  AND medium presses stay in the noise (+15..30) -> 32; firm hits 372-708 -> 48-109. Soft vs medium is
  NOT separable on this sensor (stock behaves the same: soft = 32). The old "something is missing"
  conclusion was a x16 units mistake. Full log: `fw/VELOCITY_NOTES.md`.

## Akai's firmware: other facts

- **Graphics:** 4 fonts (5x7, 6x16, 10x16, 12x32) and 4 boot screens (AKAI PROFESSIONAL + FL fruit,
  fruit, AKAI, FIRE + fruit). Rendered: `fire_fonts_and_logos.png`.
- **SysEx command table** (header `F0 47 7F 43`, then cmd, length hi/lo): 0x06 0x07 0x08 0x09 0x0D
  0x0E (OLED bitmap) 0x0F 0x40 0x41 0x44 0x65 (pad colours) **0x72 (reboot to update mode)**
  **0x78 and 0x79 (write settings: never send)**; 0x06 = factory test mode.
- **USB:** one MIDI port named "FL STUDIO FIRE Jack 1". (The USB library's name table has 16 "Jack"
  names, but only one port is used.)
- **Built** with ARM's compiler (Keil/armcc C library).

## Backups

`fire_flash_v0.21_a.bin` / `_b.bin`: the full 256 KB chip, read twice, identical
(md5 `efede8a5094b655e6a125ca47cb42ea6`). Also on the Pi in `~/fire_fw/`. Option bytes in
`optbytes.bin`, OTP in `otp.bin` (blank). These hold Akai's copyrighted code: keep them private.
