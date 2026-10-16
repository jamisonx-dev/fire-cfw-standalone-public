#!/usr/bin/env python3
"""The ORC's audio -> the Fire's pad VU bars (host protocol command 01).

Reads the newest ~43 ms of a skipback RAM ring (the same way the ORC's visuals and the sampler do:
straight from tmpfs, read-only, no JACK client, so it can never make the recorder late), splits it into
16 log-spaced bands, and sends 16 levels (0-127) ~23 times a second. On the Fire: BROWSER > Lights >
"Pi VU bars" shows them on the pads (they fall by themselves; with nothing sent for 3 s the pads come back).

  python3 host_vu.py              # run until stopped (a service does this)
  python3 host_vu.py --print      # also draw the bars in the terminal (to see what it hears)
  python3 host_vu.py --start      # also switch the Fire's pads to the bars right away

Skipback comes first: this runs at idle CPU priority and does a little numpy per 43 ms (~1% of a core).
Which ring: a RAM stereo track named like MAIN (the master), else any RAM ring (same choice as the ORC visuals).
Needs numpy and mido (+ python-rtmidi).
"""
import argparse
import json
import os
import sys
import time

import numpy as np

SKIPBUF = os.path.expanduser("~/.skipbuf")
HDR = [0x7D, 0x4F, 0x52, 0x43]               # F0 7D 'O' 'R' 'C' ... F7
N = 2048                                      # analysis window: ~43 ms at 48 kHz
RATE = 48000                                  # the rings are 48k-native
NBANDS = 16                                   # one per pad column
F_LO, F_HI = 40.0, 14000.0                    # band range, log spaced
TILT_DB_OCT = 3.0                             # music falls ~3-4.5 dB/octave: lift the highs so the bars use the whole width
RANGE_DB = 36.0                               # the bars span this many dB below the loudest band
REF_FLOOR_DB = -66.0                          # the loudest band is never taken to be quieter than this (hiss stays low); a band reads
                                              # ~4-6 dB under its loudest bin (it averages MIN_BINS bins), hence not -60
REF_DECAY_DB_S = 3.0                          # how fast the "loudest" reference relaxes when the music gets quieter
GATE = 10 ** (-68 / 20.0)                     # rms below this = silence (the ORC visuals use the same gate)
# Steadiness (tuned offline on 12 s of the real master: raw levels jumped ~12 points window to window, the pads got a
# half-row jump ~2x a second per band; these settings cut that ~5x and kept the bars as lively):
MIN_BINS = 3                                  # a band reads at least this many FFT bins (the low ones are narrower than a bin)
AVG_WINDOWS = 2                               # band power is averaged over this many windows
ATTACK = 0.6                                  # share of the way to a higher level taken per window (~46 ms): fast
RELEASE = 0.25                                # ... and to a lower one: slower, so the bars glide down

_warned = set()


def warn_once(key, msg):
    """Print a problem to the journal once per key (a loop that runs 60x a second must not flood the log)."""
    if key not in _warned:
        _warned.add(key)
        print(f"[firevu] {msg}", flush=True)


class RingSource:
    """The newest N frames of a skipback ring, mono (the channels averaged). Same ring choice as the ORC visuals'
    AudioTap: RAM rings first, a stereo one named like MAIN first of those; re-picked every 5 s.
    heads.json is the recorder's file, not ours: a track entry that is malformed (no folder, a size or channel count
    that is not a positive number) is skipped and named in the journal once, never a crash."""

    def __init__(self, root=SKIPBUF):
        self.root = root
        self.dir, self.size, self.ch = None, 0, 2
        self._pick_t = -99.0
        self._pos = None
        self._none_since = None                           # when we last had no ring to read (None = we have one)
        self._said_none = False

    NO_RING_SAY_AFTER = 30.0                              # s without a ring before the journal hears about it

    def _no_ring(self, now, why):
        """No ring to read. Said in the journal ONCE per outage (after NO_RING_SAY_AFTER s, so a recorder restart
        stays quiet): the service keeps running and reads as 'active' while the Fire's PI VU gets nothing."""
        self.dir = None
        if self._none_since is None:
            self._none_since = now
        elif not self._said_none and now - self._none_since >= self.NO_RING_SAY_AFTER:
            self._said_none = True
            print(f"[firevu] no skipback ring to read for {now - self._none_since:.0f} s ({why}): "
                  "the Fire's PI VU gets nothing until one appears", flush=True)

    def _ring_ok(self, d):
        if self._said_none:
            print(f"[firevu] skipback ring back: reading {d}", flush=True)
        self._none_since, self._said_none = None, False

    @staticmethod
    def _usable(tid, t):
        """-> (dir, buffer_bytes, channels) for a track entry we can read, else None (and say why, once)."""
        if not isinstance(t, dict):
            warn_once(("track", tid, "type"), f"skipback track {tid}: entry is not a table; skipped")
            return None
        d = t.get("dir")
        if not d or not isinstance(d, str):
            return None                                   # no folder (yet): nothing to read, nothing wrong
        try:
            size = int(t.get("buffer_bytes", 0))
            ch = int(t.get("channels", 2))
        except (TypeError, ValueError):
            warn_once(("track", tid, "int"), f"skipback track {tid}: buffer_bytes / channels are not numbers; skipped")
            return None
        if size <= 0 or ch <= 0:
            warn_once(("track", tid, "range"), f"skipback track {tid}: buffer_bytes {size} / channels {ch} not usable; skipped")
            return None
        return d, size, ch

    def pick(self, now):
        if now - self._pick_t < 5.0:
            return
        self._pick_t = now
        try:
            with open(os.path.join(self.root, "heads.json")) as f:
                tracks = json.load(f).get("tracks", {})
            if not isinstance(tracks, dict):
                raise ValueError("tracks is not a table")
        except Exception as e:
            self._no_ring(now, f"heads.json: {type(e).__name__}")
            return

        def rank(e):
            t = e[0]
            ram = t.get("store", "ram") == "ram"
            main = "MAIN" in str(t.get("name", "")).upper()
            return (not ram, not (main and e[1][2] == 2), not main)

        good = []
        for tid, t in tracks.items():
            u = self._usable(tid, t)
            if u is not None:
                good.append((t, u))
        if not good:
            self._no_ring(now, "no usable track in heads.json")
            return
        d, size, ch = sorted(good, key=rank)[0][1]
        self._ring_ok(d)
        new = os.path.join(self.root, d)
        if new != self.dir:
            self._pos = None
        self.dir = new
        self.size = size
        self.ch = ch

    def read(self, now):
        """-> mono float32 samples (N of them), or None when there is nothing new / no ring."""
        self.pick(now)
        if not self.dir or self.size <= 0:
            return None
        try:
            with open(os.path.join(self.dir, "ring.pos")) as f:
                pos = int(f.read().strip() or 0)
        except Exception:
            return None
        if pos == self._pos:
            return None                           # the recorder has not moved: no news
        fb = 2 * self.ch
        n = N * fb
        start = (pos - n) % self.size
        start -= start % fb
        try:
            with open(os.path.join(self.dir, "ring.raw"), "rb") as f:
                f.seek(start)
                raw = f.read(min(n, self.size - start))
                if len(raw) < n:
                    f.seek(0)
                    raw += f.read(n - len(raw))
        except Exception:
            return None
        if len(raw) != n:
            return None
        self._pos = pos
        a = np.frombuffer(raw, "<i2").astype("f4") / 32768.0
        return a.reshape(-1, self.ch).mean(axis=1)


class Meter:
    """Audio window -> 16 band levels 0..127, with an automatic gain that follows the music."""

    def __init__(self, rate=RATE):
        self.win = np.hanning(N).astype("f4")
        freq = np.fft.rfftfreq(N, 1.0 / rate)
        edges = F_LO * (F_HI / F_LO) ** (np.arange(NBANDS + 1) / NBANDS)
        self.edges = edges
        self.idx = []
        centre = (edges[:-1] * edges[1:]) ** 0.5
        for lo, hi, c in zip(edges[:-1], edges[1:], centre):
            ix = np.nonzero((freq >= lo) & (freq < hi))[0]
            if len(ix) < MIN_BINS:                # a narrow band reads MIN_BINS bins round its centre (one bin alone jitters)
                k = int(np.argmin(np.abs(freq - c)))
                half = MIN_BINS // 2
                ix = np.arange(max(0, k - half), min(len(freq), k + half + 1))
            self.idx.append(ix)
        # The averaging matrix: row i holds 1/len(ix) on band i's bins, so W @ p = every band's mean power in one
        # numpy call (the per-band Python loop it replaces cost ~1 ms of the ~1.4 ms a window took on the Pi).
        # np.add.reduceat can't do this: the narrow low bands share bins (they overlap).
        self.W = np.zeros((NBANDS, len(freq)))
        for i, ix in enumerate(self.idx):
            self.W[i, ix] = 1.0 / len(ix)
        self.tilt = (TILT_DB_OCT * np.log2(centre / 1000.0)).astype("f4")
        self.ref = REF_FLOOR_DB
        self.t = None
        self.hist = []                            # the last AVG_WINDOWS power spectra
        self.s = np.zeros(NBANDS)                 # the smoothed levels 0..1

    def bands_db(self, mono):
        mag = np.abs(np.fft.rfft(mono * self.win)) / (N / 4.0)    # a full-scale sine reads 1.0 at its bin
        self.hist = (self.hist + [mag * mag])[-AVG_WINDOWS:]
        p = np.mean(self.hist, axis=0)                            # power averaged over the last few windows
        out = self.band_rms(p).astype("f4")
        return 20.0 * np.log10(out + 1e-9) + self.tilt

    def band_rms(self, p):
        """A power spectrum (len(freq) bins) -> 16 band levels: a band's level = the rms of its bins."""
        return np.sqrt(self.W @ p)

    def levels(self, mono, now):
        """-> 16 ints 0..127. Silence (under the gate) = all zeros and the gain relaxes."""
        dt = 0.0 if self.t is None else min(1.0, max(0.0, now - self.t))
        self.t = now
        rms = float(np.sqrt(np.mean(mono * mono)))
        self.ref = max(REF_FLOOR_DB, self.ref - REF_DECAY_DB_S * dt)
        if rms < GATE:
            self.hist, self.s = [], np.zeros(NBANDS)              # silence: nothing carries over
            return [0] * NBANDS
        db = self.bands_db(mono)
        self.ref = max(self.ref, float(db.max()))                 # peak follower: attack at once, relax slowly
        v = np.clip((db - (self.ref - RANGE_DB)) / RANGE_DB, 0.0, 1.0)
        self.s = self.s + (v - self.s) * np.where(v > self.s, ATTACK, RELEASE)   # fast up, glide down
        return [int(round(x * 127)) for x in self.s]


def frame(levels):
    """The host-protocol message body (mido adds F0 / F7): 7D 'ORC' 01 + 16 levels, all 0..127."""
    return HDR + [0x01] + [max(0, min(127, int(x))) for x in levels]


def bars(levels):
    """A one-line picture of the levels for --print."""
    return "".join(" .:-=+*#%@"[min(9, int(v) * 10 // 128)] for v in levels)


# ---------------------------------------------------------------- the Fire's crash record (crash.h in the firmware)
CRASH_CAUSES = {0: "none", 1: "HardFault", 2: "MemManage", 3: "BusFault", 4: "UsageFault", 5: "watchdog (a hang)",
                6: "stack smash (a buffer overran into a stack canary; PC = the function whose buffer it was)",
                7: "watchpoint (a CPU write hit TIM4's watched canary; PC = just after the writing instruction)"}
CRASH_STAGES = {1: "USB + MIDI + host protocol", 2: "sequencer", 3: "note-off / ratchet queues", 4: "input events",
                5: "idle lights + save gates", 6: "flash saver", 7: "lights", 8: "knobs", 9: "paint LEDs",
                10: "render + OLED", 11: "loop tail", 12: "start-up"}
CRASH_REPLY_LEN = 61                                # version 2 (+ build id, SP, stack low point); version 1 was 46 bytes
CRASH_REPLY_LENS = {1: 46, 2: 61}


def unpack32(b):
    """Five 7-bit bytes, low bits first -> a 32-bit value (crash_pack32 in the firmware)."""
    v = 0
    for x in reversed(list(b)[:5]):
        v = (v << 7) | (x & 0x7F)
    return v & 0xFFFFFFFF                           # (five 7-bit groups hold 35 bits; the firmware's uint32 keeps 32)


def decode_crash(data):
    """The Fire's reply to host command 06 (data = the SysEx bytes without F0/F7) -> a dict, or None if it is not one."""
    d = list(data)
    if len(d) < 6 or d[:5] != HDR + [0x06] or CRASH_REPLY_LENS.get(d[5]) != len(d):
        return None
    nf = 10 if d[5] >= 2 else 7
    f = [unpack32(d[11 + 5 * i:16 + 5 * i]) for i in range(nf)]
    return dict(valid=bool(d[6]), cause=d[7], cause_name=CRASH_CAUSES.get(d[7], "?"), stage=d[8],
                stage_name=CRASH_STAGES.get(d[8], "?"), count=d[9], total=d[10],
                pc=f[0], lr=f[1], psr=f[2], cfsr=f[3], hfsr=f[4], addr=f[5], up_ms=f[6],
                build=f[7] if nf == 10 else None,   # None = an older firmware's reply; 0 = recorded by an image without an id
                sp=f[8] if nf == 10 else None, stack_min=f[9] if nf == 10 else None)


STACK_WORDS = 64
STACK_REPLY_LEN = 4 + 1 + 1 + 1 + 5 + STACK_WORDS * 5
APP_LO, APP_HI = 0x08008000, 0x08020000             # the app's flash: a word in here with bit 0 set looks like a return address


def decode_stack(data):
    """The Fire's reply to host command 08 (the stack around the last fault, v0.17c) -> dict(valid, base, words), or None."""
    d = list(data)
    if len(d) != STACK_REPLY_LEN or d[:5] != HDR + [0x08] or d[5] != 1:
        return None
    return dict(valid=bool(d[6]), base=unpack32(d[7:12]), words=[unpack32(d[12 + 5 * i:17 + 5 * i]) for i in range(STACK_WORDS)])


def looks_like_code(w):
    """A Thumb return address into the app (odd, inside the app's flash)."""
    return APP_LO <= w < APP_HI and w & 1 == 1


# what was running when it faulted: xPSR's exception number (0 = the main loop)
EXCEPTIONS = {0: "the main loop", 2: "NMI", 3: "HardFault handler", 11: "SVCall", 14: "PendSV", 15: "SysTick interrupt",
              16 + 30: "TIM4 interrupt (LED chain + buttons)", 16 + 67: "USB interrupt (OTG_FS, TinyUSB)",
              16 + 47: "DMA1 stream 7 interrupt (OLED)"}


def running_in(psr):
    n = psr & 0x1FF
    return EXCEPTIONS.get(n, f"exception {n}" + (f" (IRQ {n - 16})" if n >= 16 else ""))


def describe_crash(c):
    """One readable line (what to look at: arm-none-eabi-addr2line -e fireseq.elf <pc>)."""
    if not c["valid"]:
        return "no crash on record"
    bits = []
    for bit, name in ((0, "instruction fetch from a no-execute region (MPU: a jump into RAM)"), (9, "precise bus error"), (8, "instruction bus error"), (16, "undefined instruction"), (17, "invalid state"),
                      (18, "bad EXC_RETURN"), (24, "unaligned access"), (25, "divide by zero"), (1, "data access violation")):
        if c["cfsr"] & (1 << bit):
            bits.append(name)
    why = ", ".join(bits) or "no detail"
    where = "" if c["cause"] == 5 else (f" PC 0x{c['pc']:08x} LR 0x{c['lr']:08x} fault address 0x{c['addr']:08x};"
                                         f" running: {running_in(c['psr'])}")
    return (f"{c['cause_name']} in stage '{c['stage_name']}' after {c['up_ms'] / 1000:.1f} s up ({why});{where} "
            f"[early crashes in a row {c['count']}, total {c['total']}]")


class Fire:
    """The Fire's MIDI port, found by name ("ORC FW"), and KEPT ALIVE: when the Fire reboots (a flash, a replug) its
    ALSA port is destroyed and recreated, often with the SAME client number, and a handle opened before that drops
    every message silently (no error: measured on this rig). So the link is checked by asking: a hello every few
    seconds, and the Fire's reply (HDR + 00 + version) must keep coming; if it stops, both ports are closed and
    opened again."""

    HELLO_EVERY = 3.0          # s between hellos
    DEAD_AFTER = 8.0           # s without a reply before the link is thrown away and re-made

    def __init__(self, mido_mod, on_crash=None):
        self.mido = mido_mod
        self.out = self.inp = None
        self._try_t = -99.0
        self.last_reply = self.last_hello = 0.0
        self.crash = None                           # the last crash report the Fire gave us
        self._crash_asked = False
        self._crash_seen = None
        self.on_crash = on_crash                    # called ONCE per new crash (the service: save a full report)

    def _drop(self):
        for port in (self.out, self.inp):
            try:
                if port is not None:
                    port.close()
            except Exception:
                pass
        self.out = self.inp = None

    def _open(self, now):
        try:
            outs = [n for n in self.mido.get_output_names() if "ORC FW" in n]
        except Exception as e:                      # the ALSA sequencer can refuse a listing (e.g. mid-replug)
            warn_once(("fire", "names", type(e).__name__), f"cannot list MIDI ports ({e}); will keep trying")
            return False
        if not outs:
            return False
        try:
            self.out = self.mido.open_output(outs[0])
            ins = [n for n in self.mido.get_input_names() if "ORC FW" in n]
            self.inp = self.mido.open_input(ins[0]) if ins else None
        except Exception as e:
            warn_once(("fire", "open", type(e).__name__), f"cannot open the Fire ({e}); will keep trying")
            self._drop()
            return False
        _warned.difference_update([k for k in _warned if k[0] == "fire"])   # a later outage is logged again
        self.last_reply = now                       # the clock for DEAD_AFTER starts now
        self.last_hello = -99.0                     # say hello at once
        self._crash_asked = False                   # ...and ask for its last crash record (once per connection)
        print(f"[firevu] Fire found: {outs[0]}", flush=True)
        return True

    def _replies(self, now):
        if self.inp is None:
            return
        try:
            while True:
                m = self.inp.poll()
                if m is None:
                    break
                if getattr(m, "type", "") != "sysex":
                    continue
                if list(m.data[:5]) == HDR + [0x00]:
                    self.last_reply = now
                elif list(m.data[:5]) == HDR + [0x06]:
                    c = decode_crash(m.data)
                    if c is not None:
                        self.crash = c
                        key = (c["total"], c["pc"], c["cause"], c["up_ms"])
                        if c["valid"] and key != self._crash_seen:       # tell the journal ONCE per crash
                            self._crash_seen = key
                            print("[firevu] the Fire reports a crash: " + describe_crash(c), flush=True)
                            if self.on_crash:
                                try:
                                    self.on_crash(c)
                                except Exception as e:
                                    print(f"[firevu] could not save the crash report: {e}", flush=True)
        except Exception:
            pass

    def service(self, now):
        """Keep the link healthy; call it EVERY loop, audio or not. Opens the Fire when it shows up (one look per
        2 s), drains its replies, says hello on schedule and throws a link away whose replies stopped. It must not
        depend on audio: when it only ran on a frame, a skipback pause of >= 8 s tore down a healthy link (no
        hellos went out, so no replies came back). -> True while a link is open."""
        if self.out is None:
            if now - self._try_t < 2.0:
                return False
            self._try_t = now
            if not self._open(now):
                return False
        self._replies(now)
        if self.inp is not None and now - self.last_reply > self.DEAD_AFTER:
            print("[firevu] the Fire stopped answering (rebooted or replugged?): reconnecting", flush=True)
            self._drop()
            self._try_t = now
            return False
        if now - self.last_hello > self.HELLO_EVERY:
            self.last_hello = now
            if not self._send(HDR + [0x00]):
                return False
            if not self._crash_asked:
                self._crash_asked = True
                if not self._send(HDR + [0x06]):
                    return False
        return True

    def send(self, data, now):
        """Send one SysEx body over the open link (service() opens and checks it). -> True if it went out."""
        if self.out is None:
            return False
        return self._send(data)

    def _send(self, data):
        try:
            self.out.send(self.mido.Message("sysex", data=data))
            return True
        except Exception as e:
            print(f"[firevu] Fire went away ({e}); looking again", flush=True)
            self._drop()
            return False


def save_crash_report(_c):
    """A new crash: write the full report (record + stack snapshot, symbolised against the kept ELF of that build) to
    ~/fire_fw/crashes/ before anything can overwrite it. host_crash.py --save, in the background (never --clear)."""
    import subprocess
    here = os.path.dirname(os.path.abspath(__file__))
    subprocess.Popen([sys.executable, os.path.join(here, "host_crash.py"), "--save"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    print("[firevu] saving the full crash report to ~/fire_fw/crashes/", flush=True)


def idle_priority():
    """Skipback first: this must never compete with anything."""
    try:
        os.sched_setscheduler(0, os.SCHED_IDLE, os.sched_param(0))
    except Exception:
        try:
            os.nice(19)
        except Exception:
            pass


def main(argv=None):
    ap = argparse.ArgumentParser(description="Send the ORC's audio levels to the Fire's pad VU bars.")
    ap.add_argument("--root", default=SKIPBUF, help="skipback folder (default ~/.skipbuf)")
    ap.add_argument("--print", action="store_true", help="draw the bars in the terminal")
    ap.add_argument("--start", action="store_true", help="also switch the Fire's pads to the bars now")
    ap.add_argument("--dry", action="store_true", help="don't open MIDI (just read + print)")
    args = ap.parse_args(argv)
    idle_priority()
    mido = None
    if not args.dry:
        import mido as mido_mod
        mido = mido_mod
    fire = Fire(mido, on_crash=save_crash_report) if mido else None
    src, meter = RingSource(args.root), Meter()
    st = {"started": args.start, "sent": 0}
    last_print = 0.0
    print("[firevu] running: skipback ring -> the Fire's pad VU (BROWSER > Lights > Pi VU bars)", flush=True)
    while True:
        now = time.monotonic()
        lv = tick(src, meter, fire, st, now)
        if lv is None:
            time.sleep(0.01)
            continue
        if args.print and now - last_print > 0.25:
            last_print = now
            print(f"[{bars(lv)}] ref {meter.ref:6.1f} dB  frames sent {st['sent']}", flush=True)
        time.sleep(0.015)


def tick(src, meter, fire, st, now):
    """One pass of the main loop. The link is serviced FIRST and every pass, whether or not the ring has news,
    so a paused or stopped recorder never costs a healthy link. -> the levels sent, or None (no new audio)."""
    if fire:
        fire.service(now)
    mono = src.read(now)
    if mono is None:
        return None
    lv = meter.levels(mono, now)
    if fire:
        if st["started"] and fire.send(HDR + [0x02, 1], now):
            st["started"] = False
        if fire.send(frame(lv), now):
            st["sent"] += 1
    return lv


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
