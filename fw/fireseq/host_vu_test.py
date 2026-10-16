#!/usr/bin/env python3
"""Tests for host_vu.py: the ring reader (against a fake skipback folder), the 16-band meter, the protocol
frame and the Fire's MIDI port handling (against a fake mido). Pure numpy, touches nothing on the rig.
    python3 host_vu_test.py
"""
import contextlib
import io
import json
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host_vu as H

EXPECTED = 121
passed = failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  ok   {name}")
    else:
        failed += 1
        print(f"  FAIL {name}  {detail}")


def tone(freq, amp, n=H.N, rate=H.RATE, phase=0.0):
    return (amp * np.sin(2 * np.pi * freq * np.arange(n) / rate + phase)).astype("f4")


def band_of(meter, hz):
    return next(i for i in range(H.NBANDS) if meter.edges[i] <= hz < meter.edges[i + 1])


def run(meter, mono, seconds=1.0, t0=0.0):
    """Feed the same window for a while (the gain settles); -> the last levels."""
    lv = None
    t = t0
    while t < t0 + seconds:
        lv = meter.levels(mono, t)
        t += 0.043
    return lv


# ---------------------------------------------------------------- bands + protocol
m = H.Meter()
check("16 bands, widening, from F_LO to F_HI", len(m.idx) == 16 and abs(m.edges[0] - H.F_LO) < 1e-6 and abs(m.edges[-1] - H.F_HI) < 1
      and all(m.edges[i] < m.edges[i + 1] for i in range(16)))
check("every band reads at least one FFT bin", all(len(ix) >= 1 for ix in m.idx))
check("bands ascend (a higher band never reads lower bins than the one below it)", all(m.idx[i][0] <= m.idx[i + 1][0] for i in range(15)))

fr = H.frame([0, 64, 127, 500, -5] + [10] * 11)
check("frame = 7D 'ORC' 01 + 16 levels", fr[:5] == [0x7D, 0x4F, 0x52, 0x43, 0x01] and len(fr) == 5 + 16)
check("frame clamps every level to 0..127 (a SysEx data byte must be < 128)", all(0 <= b < 128 for b in fr) and fr[5 + 3] == 127 and fr[5 + 4] == 0)
check("bars() draws 16 characters", len(H.bars([0] * 16)) == 16 and H.bars([127] * 16) == "@" * 16)

# ---------------------------------------------------------------- the meter hears the right band
for hz in (60, 100, 250, 1000, 4000, 8000, 12000):
    mm = H.Meter()
    lv = run(mm, tone(hz, 0.1))
    want = band_of(mm, hz)
    check(f"a {hz} Hz tone peaks in band {want}", int(np.argmax(lv)) == want, f"got {int(np.argmax(lv))} levels={lv}")

mm = H.Meter()
lv = run(mm, tone(1000, 0.1))
check("the loudest band reads full scale (127) once the gain follows", max(lv) == 127)
check("a pure tone leaves far-away bands low", lv[0] < 20 and lv[15] < 20, f"{lv}")

# ---------------------------------------------------------------- silence, gate, auto gain
mm = H.Meter()
check("silence = all zeros", run(mm, np.zeros(H.N, "f4")) == [0] * 16)
check("hiss under the gate = all zeros", run(H.Meter(), tone(1000, 0.0004)) == [0] * 16)

mm = H.Meter()
lv = run(mm, tone(1000, 0.002))                         # -54 dBFS: quiet Bluetooth-ish music
check("quiet music still reaches full scale (the gain follows it)", max(lv) == 127, f"{lv}")

mm = H.Meter()
run(mm, tone(1000, 0.5), 1.0)                           # a loud passage
quiet = tone(1000, 0.01)
first = max(run(mm, quiet, 1.0, 1.1))
after = max(run(mm, quiet, 25.0, 2.2))
check("after a loud passage a quiet one reads low (the bars have glided down, the gain still remembers)", first < 60, f"first={first}")
check("...and the gain relaxes: ~25 s later it reads high again", after > 100, f"after={after}")

mm = H.Meter()
two = tone(500, 0.1) + tone(2000, 0.001)                # one loud, one 40 dB quieter
lv = run(mm, two)
b_loud, b_quiet = band_of(mm, 500), band_of(mm, 2000)
check("a tone 40 dB quieter reads much lower than the loud one", lv[b_loud] - lv[b_quiet] > 50, f"{lv[b_loud]} vs {lv[b_quiet]}")

mm = H.Meter()
run(mm, np.zeros(H.N, "f4"), 60.0)
check("the reference never falls below its floor in long silence", mm.ref == H.REF_FLOOR_DB)

# ---------------------------------------------------------------- steadiness (the point of the smoothing)
rng = np.random.default_rng(7)


def jitter_levels(attack, release, avg):
    old = (H.ATTACK, H.RELEASE, H.AVG_WINDOWS)
    H.ATTACK, H.RELEASE, H.AVG_WINDOWS = attack, release, avg
    try:
        mm_ = H.Meter()
        out, t = [], 0.0
        for k in range(60):                                 # a steady tone + noise that changes every window: a raw meter jitters
            w = tone(1000, 0.05) + (0.02 * rng.standard_normal(H.N)).astype("f4") + tone(300, 0.03 + 0.02 * ((k * 7) % 5) / 5.0)
            out.append(mm_.levels(w, t))
            t += 0.046
        return np.array(out[10:])
    finally:
        H.ATTACK, H.RELEASE, H.AVG_WINDOWS = old


raw_j = float(np.mean(np.abs(np.diff(jitter_levels(1.0, 1.0, 1), axis=0))))
sm_j = float(np.mean(np.abs(np.diff(jitter_levels(H.ATTACK, H.RELEASE, H.AVG_WINDOWS), axis=0))))
check("smoothing makes the levels steadier (window-to-window change at most 0.6x the raw meter's)", sm_j < 0.6 * raw_j, f"smooth {sm_j:.2f} vs raw {raw_j:.2f}")
mm = H.Meter()
step = [mm.levels(tone(1000, 0.1), 1.0 + 0.046 * i) for i in range(4)]
check("it still answers a sudden sound fast: >= 90% of full scale within 3 windows", max(step[2]) >= 0.9 * 127, f"{[max(s_) for s_ in step]}")
check("...but not as a jump: the first window after silence is not already full scale (the attack glides)", max(step[0]) < 0.9 * 127, f"{[max(s_) for s_ in step]}")
check("attack and release both smooth, release the slower one (0 < RELEASE < ATTACK < 1)", 0 < H.RELEASE < H.ATTACK < 1, f"{H.ATTACK} {H.RELEASE}")
mm = H.Meter()
run(mm, tone(1000, 0.1), 1.0)
mm2 = H.Meter()
run(mm2, tone(1000, 0.1), 1.0)
after1 = max(mm2.levels(tone(1000, 0.003), 1.1))
check("a drop in level glides (one window later it is still over half of what it was)", after1 > 63, f"after one window: {after1}")

# the tilt: equal-amplitude tones read higher the higher they are (3 dB/octave), within the range
mm = H.Meter()
lv = run(mm, tone(250, 0.05) + tone(4000, 0.05))
check("the tilt lifts highs (equal tones: the 4 kHz band reads above the 250 Hz band)", lv[band_of(mm, 4000)] > lv[band_of(mm, 250)])

# ---------------------------------------------------------------- the ring reader
def make_ring(root, tracks, wave_l, wave_r, pos, size):
    os.makedirs(root, exist_ok=True)
    heads = {"tracks": {}}
    for tid, t in tracks.items():
        d = os.path.join(root, f"t{tid}")
        os.makedirs(d, exist_ok=True)
        heads["tracks"][str(tid)] = dict(t, dir=f"t{tid}", buffer_bytes=size)
        ch = t.get("channels", 2)
        pcm = np.zeros(size // 2, "<i2")
        if ch == 2:
            pcm[0::2], pcm[1::2] = wave_l, wave_r
        else:
            k = min(len(pcm), len(wave_l))
            pcm[:k] = wave_l[:k]
        with open(os.path.join(d, "ring.raw"), "wb") as f:
            f.write(pcm.tobytes())
        with open(os.path.join(d, "ring.pos"), "w") as f:
            f.write(str(pos))
    with open(os.path.join(root, "heads.json"), "w") as f:
        json.dump(heads, f)


with tempfile.TemporaryDirectory() as tmp:
    frames = 20000
    size = frames * 4
    ramp = (np.arange(frames) % 30000).astype("<i2")
    ramp_r = (-(np.arange(frames) % 30000)).astype("<i2")            # L = -R: the mono mix is zero
    tracks = {0: dict(name="MAIN", store="card", channels=2),
              1: dict(name="SMP MAIN", store="ram", channels=2),
              2: dict(name="IN3", store="ram", channels=1)}
    make_ring(tmp, tracks, ramp, ramp, 12000 * 4, size)
    src = H.RingSource(tmp)
    mono = src.read(0.0)
    check("a ring is read: N mono frames", mono is not None and len(mono) == H.N)
    check("it picks the RAM stereo MAIN ring (not the card one, not the mono one)", src.dir.endswith("t1"), src.dir)
    expect = (ramp[12000 - H.N:12000].astype("f4") / 32768.0)
    check("the newest frames before ring.pos are what is read", np.allclose(mono, expect))
    check("nothing new (ring.pos unchanged) = None", src.read(0.1) is None)
    with open(os.path.join(tmp, "t1", "ring.pos"), "w") as f:
        f.write(str(13000 * 4))
    mono2 = src.read(0.2)
    check("a moved ring.pos reads the new newest frames", mono2 is not None and np.allclose(mono2, ramp[13000 - H.N:13000].astype("f4") / 32768.0))
    with open(os.path.join(tmp, "t1", "ring.pos"), "w") as f:
        f.write(str(500 * 4))                                       # the head is near the start: the read wraps
    mono3 = src.read(0.3)
    want = np.concatenate((ramp[frames - (H.N - 500):], ramp[:500])).astype("f4") / 32768.0
    check("a read that wraps round the end of the ring is stitched correctly", mono3 is not None and np.allclose(mono3, want))
    with open(os.path.join(tmp, "t1", "ring.pos"), "w") as f:
        f.write("garbage")
    check("a garbage ring.pos = None, not a crash", src.read(0.4) is None)

    # a mono ring is read as mono; L = -R averages to silence
    make_ring(tmp, {5: dict(name="SOLO", store="ram", channels=1)}, ramp, ramp, 12000 * 2, frames * 2)
    src = H.RingSource(tmp)
    mm_ = src.read(0.0)
    check("a mono ring: N frames", mm_ is not None and len(mm_) == H.N and src.ch == 1)
    make_ring(tmp, {0: dict(name="MAIN", store="ram", channels=2)}, ramp, ramp_r, 12000 * 4, size)
    src = H.RingSource(tmp)
    m3 = src.read(0.0)
    check("stereo is averaged (L = -R gives silence)", m3 is not None and float(np.abs(m3).max()) < 1e-6)

    # end to end: a real tone in a fake ring -> the right band peaks -> a valid frame
    n_tone = 30000
    wave = (3000 * np.sin(2 * np.pi * 1000 * np.arange(n_tone) / H.RATE)).astype("<i2")
    make_ring(tmp, {0: dict(name="MAIN", store="ram", channels=2)}, wave, wave, 25000 * 4, n_tone * 4)
    src, mm = H.RingSource(tmp), H.Meter()
    lv = mm.levels(src.read(0.0), 0.0)
    check("end to end: a 1 kHz tone in the ring peaks in the 1 kHz band", int(np.argmax(lv)) == band_of(mm, 1000), f"{lv}")
    fr = H.frame(lv)
    check("end to end: the frame is a valid SysEx body", len(fr) == 21 and all(0 <= b < 128 for b in fr))

with tempfile.TemporaryDirectory() as tmp:
    src = H.RingSource(tmp)
    check("no heads.json = None, no crash", src.read(0.0) is None)
    with open(os.path.join(tmp, "heads.json"), "w") as f:
        f.write("{not json")
    src = H.RingSource(tmp)
    check("a corrupt heads.json = None, no crash", src.read(0.0) is None)
    with open(os.path.join(tmp, "heads.json"), "w") as f:
        json.dump({"tracks": {}}, f)
    src = H.RingSource(tmp)
    check("no tracks = None", src.read(0.0) is None)

# no ring for a while: said ONCE (after 30 s, so a recorder restart stays quiet), and said again when it comes back
with tempfile.TemporaryDirectory() as tmp:
    src = H.RingSource(tmp)
    out_ = io.StringIO()
    with contextlib.redirect_stdout(out_):
        for k in range(0, 61, 5):                     # 60 s with no heads.json, a look every 5 s
            src.read(float(k))
    said = out_.getvalue()
    check("no ring for 60 s: the journal hears it exactly once", said.count("no skipback ring to read") == 1, said)
    out_ = io.StringIO()
    with contextlib.redirect_stdout(out_):
        src2 = H.RingSource(tmp)
        for k in range(0, 26, 5):                     # 25 s: a recorder restart is shorter than the 30 s grace
            src2.read(float(k))
    check("a short outage (< 30 s) stays quiet", out_.getvalue() == "", out_.getvalue())
    wave_ = np.zeros(20000, "<i2")
    make_ring(tmp, {0: dict(name="MAIN", store="ram", channels=2)}, wave_, wave_, 12000 * 4, 20000 * 4)
    out_ = io.StringIO()
    with contextlib.redirect_stdout(out_):
        src.read(70.0)
        src.read(75.0)
    check("the ring comes back: said once, and the next outage would be said again",
          out_.getvalue().count("skipback ring back") == 1 and src._said_none is False, out_.getvalue())


# ---------------------------------------------------------------- the Fire's port (a fake mido)
class FakeMsg:
    def __init__(self, kind, data=None):
        self.kind, self.data = kind, tuple(data or ())
        self.type = kind


class FakeOut:
    def __init__(self, fail=False):
        self.sent, self.fail, self.closed = [], fail, False

    def send(self, msg):
        if self.fail:
            raise OSError("port gone")
        self.sent.append(msg)

    def close(self):
        self.closed = True


class FakeIn:
    def __init__(self):
        self.queue, self.closed = [], False

    def poll(self):
        return self.queue.pop(0) if self.queue else None

    def close(self):
        self.closed = True


class FakeMido:
    def __init__(self, outs=(), ins=(), out=None, inp=None):
        self.outs, self.ins, self.out, self.inp = list(outs), list(ins), out, inp
        self.opened_out = self.opened_in = 0
        self.fail_open = False
        self.Message = FakeMsg

    def get_output_names(self):
        return self.outs

    def get_input_names(self):
        return self.ins

    def open_output(self, name):
        if self.fail_open:
            raise OSError("cannot open")
        self.opened_out += 1
        return self.out

    def open_input(self, name):
        self.opened_in += 1
        return self.inp


NAME = "ORC FW seq:ORC FW seq MIDI 1 36:0"
HELLO = H.HDR + [0x00]
REPLY = FakeMsg("sysex", H.HDR + [0x00, 0x01])
FRAME = H.frame([5] * 16)


def link(f, data, t):
    """What the main loop does for one frame: service the link, then send."""
    return f.service(t) and f.send(data, t)


fm = FakeMido()
fire = H.Fire(fm)
check("no Fire plugged in: send returns False, no crash", link(fire, FRAME, 0.0) is False)
check("it does not hammer the port list (one look per 2 s)", link(fire, FRAME, 0.5) is False and fm.opened_out == 0)

out, inp = FakeOut(), FakeIn()
fm.outs, fm.ins, fm.out, fm.inp = ["Something Else:1", NAME], ["Something Else:1", NAME], out, inp
check("the Fire is found by name; its output and input are both opened", link(fire, FRAME, 3.0) is True and fm.opened_out == 1 and fm.opened_in == 1)
check("the first thing sent is a hello, then the frame", out.sent[0].data == tuple(HELLO) and out.sent[-1].data == tuple(FRAME), [m.data[:6] for m in out.sent])
check("what it sends is SysEx", all(m.kind == "sysex" for m in out.sent))
link(fire, FRAME, 4.0)
check("no second hello before 3 s are up", sum(1 for m in out.sent if m.data == tuple(HELLO)) == 1)
link(fire, FRAME, 6.2)
check("a hello again after 3 s", sum(1 for m in out.sent if m.data == tuple(HELLO)) == 2)

# alive: replies keep the link
check("no reply yet but under 8 s: the link is kept", link(fire, FRAME, 10.5) is True and fm.opened_out == 1)
inp.queue.append(REPLY)
check("a reply arrives: still connected", link(fire, FRAME, 10.6) is True and fm.opened_out == 1)
check("7.4 s after that reply the link is still kept", link(fire, FRAME, 18.0) is True and fm.opened_out == 1)

# dead: an unrelated SysEx is not a reply
inp.queue.append(FakeMsg("sysex", [0x7D, 0x4F, 0x52, 0x43, 0x05, 0x01]))
inp.queue.append(FakeMsg("note_on", [60, 100]))
check("other messages do not count as a reply: after 8 s of none the link is thrown away",
      link(fire, FRAME, 18.8) is False and fire.out is None and out.closed and inp.closed)
out2, inp2 = FakeOut(), FakeIn()
fm.out, fm.inp = out2, inp2
check("...and it waits ~2 s before trying again", link(fire, FRAME, 19.5) is False and fm.opened_out == 1)
check("...then opens a fresh link (the Fire's new port) and sends", link(fire, FRAME, 21.0) is True and fm.opened_out == 2 and fm.opened_in == 2 and out2.sent)

# a stale handle: the Fire reboots, its replies stop, the link is re-made on its own (the bug this exists for)
inp2.queue.append(REPLY)
link(fire, FRAME, 22.0)
out3, inp3 = FakeOut(), FakeIn()
fm.out, fm.inp = out3, inp3
for t in (26.0, 29.0, 30.5):
    link(fire, FRAME, t)
check("the Fire reboots and stops replying: 8 s after its last reply the old link is dropped", fire.out is None and out2.closed)
link(fire, FRAME, 33.0)
check("...and the new link (the Fire's new port) is opened and used", fm.opened_out == 3 and len(out3.sent) >= 2)

# a port that raises on send
fm2 = FakeMido([NAME], [NAME], FakeOut(), FakeIn())
fire2 = H.Fire(fm2)
link(fire2, FRAME, 0.0)
fm2.out.fail = True
bad_out = fm2.out
check("a send that raises: returns False, the link is dropped", link(fire2, FRAME, 0.5) is False and fire2.out is None and bad_out.closed)

# an older firmware that cannot answer a hello: never declared dead (nothing to wait for)
fm3 = FakeMido([NAME], [], FakeOut(), None)
fire3 = H.Fire(fm3)
link(fire3, FRAME, 0.0)
check("with no input port to listen on, the link is never thrown away", link(fire3, FRAME, 100.0) is True and fm3.opened_out == 1)

# open errors
fm4 = FakeMido([NAME], [NAME], FakeOut(), FakeIn())
fm4.fail_open = True
fire4 = H.Fire(fm4)
check("a port that will not open: False, no crash", link(fire4, FRAME, 0.0) is False and fire4.out is None)

# ---------------------------------------------------------------- the crash record (decoded from bytes made by the C code)
import contextlib
import io

C_VECTOR = bytes.fromhex("7d4f52430602010307020945460440004d5702400000000008060008000000000000000400200000024b466801004d782c5101007d0300022342030002")  # crash_test hex (v2)
C_VECTOR_V1 = bytes.fromhex("7d4f52430601010307020945460440004d5702400000000008060008000000000000000400200000024b46680100")  # the same record, v1
c = H.decode_crash(C_VECTOR)
check("the C firmware's crash reply decodes: cause BusFault, stage lights, counts 2 / 9",
      c is not None and c["valid"] and c["cause"] == 3 and c["cause_name"] == "BusFault" and c["stage"] == 7 and c["stage_name"] == "lights"
      and c["count"] == 2 and c["total"] == 9, c)
check("...and its 32-bit fields come back exactly (PC, LR, xPSR, CFSR, HFSR, address, uptime)",
      c and (c["pc"], c["lr"], c["psr"], c["cfsr"], c["hfsr"], c["addr"], c["up_ms"]) ==
      (0x08012345, 0x0800ABCD, 0x61000000, 0x00000400, 0x40000000, 0x20001000, 3810123), c)
check("...and the build id, SP at the fault and the stack's lowest point (v2)",
      c and c["build"] == 0x1A2B3C4D and c["sp"] == 0x2000FE80 and c["stack_min"] == 0x2000E123, c)
c1 = H.decode_crash(C_VECTOR_V1)
check("an older firmware's v1 reply still decodes, build unknown (None)",
      c1 is not None and c1["pc"] == 0x08012345 and c1["up_ms"] == 3810123 and c1["build"] is None and c1["sp"] is None, c1)
check("a reply of the wrong length, header or version is not a crash record",
      H.decode_crash(C_VECTOR[:-1]) is None and H.decode_crash(bytes([0]) + C_VECTOR[1:]) is None
      and H.decode_crash(C_VECTOR[:5] + bytes([1]) + C_VECTOR[6:]) is None       # v1 marker on a v2-length reply
      and H.decode_crash(C_VECTOR_V1[:5] + bytes([2]) + C_VECTOR_V1[6:]) is None  # v2 marker on a v1-length reply
      and H.decode_crash(C_VECTOR[:5] + bytes([3]) + C_VECTOR[6:]) is None and H.decode_crash(b"") is None)
# the stack snapshot (command 08, v0.17c), decoded from bytes made by the C code (crash_test stackhex)
S_VECTOR = bytes.fromhex("7d4f5243080101687a0300020000000002040000000208000000020c000000021000000002390803400018000000021c000000022000000002333502400028000000022c000000023000000002340000000238000000023c000000024000000002440000000248000000024c000000025000000002540000000258000000025c000000026000000002640000000268000000026c000000027000000002740000000278000000027c000000020001000002040100000208010000020c010000021001000002140100000218010000021c010000022001000002240100000228010000022c010000023001000002340100000238010000023c010000024001000002440100000248010000024c010000025001000002540100000258010000025c010000026001000002640100000268010000026c010000027001000002740100000278010000027c01000002")
st = H.decode_stack(S_VECTOR)
check("the C firmware's stack reply decodes: base, 64 words, the two code words where C put them",
      st is not None and st["valid"] and st["base"] == 0x2000FD68 and len(st["words"]) == 64
      and st["words"][5] == 0x0800C439 and st["words"][9] == 0x08009AB3 and st["words"][10] == 0x20000028, st)
check("a stack reply of the wrong length / command / version is not one",
      H.decode_stack(S_VECTOR[:-1]) is None and H.decode_stack(S_VECTOR[:4] + bytes([6]) + S_VECTOR[5:]) is None
      and H.decode_stack(S_VECTOR[:5] + bytes([2]) + S_VECTOR[6:]) is None)
check("return addresses are recognised: odd and inside the app's flash; RAM words, even words and Akai's sectors are not",
      H.looks_like_code(0x0800C439) and not H.looks_like_code(0x0800C438) and not H.looks_like_code(0x2000FD4F)
      and not H.looks_like_code(0x08000101) and not H.looks_like_code(0x08020001))
check("xPSR says what was running: main loop / USB interrupt / LED-chain interrupt / unknown IRQ",
      H.running_in(0x61000000) == "the main loop" and "USB" in H.running_in(0x01000053) and "TIM4" in H.running_in(0x0100002E)
      and H.running_in(0x01000020) == "exception 32 (IRQ 16)")
check("a fault description names what was running", "running: USB interrupt" in H.describe_crash(dict(c, psr=0x01000053)))
check("an MPU instruction-fetch fault (CFSR bit 0) is named as a jump into RAM",
      "jump into RAM" in H.describe_crash(dict(c, cfsr=1, cause=2)))
check("unpack32 reads 7 bits a byte, low bits first", H.unpack32([0x7F, 0, 0, 0, 0]) == 0x7F and H.unpack32([0, 1, 0, 0, 0]) == 0x80
      and H.unpack32([0x7F] * 5) == 0xFFFFFFFF)
none_rec = bytearray(C_VECTOR); none_rec[6] = 0
check("a record marked not-valid says so", H.describe_crash(H.decode_crash(bytes(none_rec))) == "no crash on record")
d1 = H.describe_crash(dict(c, cfsr=1 << 9))
check("the description names the cause, stage and fault kind, and gives PC / LR",
      "BusFault" in d1 and "lights" in d1 and "precise bus error" in d1 and "0x08012345" in d1 and "0x0800abcd" in d1, d1)
wd = H.describe_crash(dict(c, cause=5, cause_name="watchdog (a hang)"))
check("a watchdog hang names the stage but has no PC (nobody knows where)", "watchdog" in wd and "lights" in wd and "PC 0x" not in wd, wd)

# the Fire object: asks for the record once per connection, and tells the journal ONCE per crash
buf = io.StringIO()
fmc = FakeMido([NAME], [NAME], FakeOut(), FakeIn())
saved_reports = []
firec = H.Fire(fmc, on_crash=saved_reports.append)
with contextlib.redirect_stdout(buf):
    link(firec, FRAME, 0.0)
asks = [m for m in fmc.out.sent if m.data == tuple(H.HDR + [0x06])]
check("on connecting it asks the Fire for its last crash (command 06), once", len(asks) == 1, [m.data[:6] for m in fmc.out.sent])
link(firec, FRAME, 1.0)
check("...and not again on the next frame", len([m for m in fmc.out.sent if m.data == tuple(H.HDR + [0x06])]) == 1)
crash_msg = FakeMsg("sysex", list(C_VECTOR))
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    fmc.inp.queue.append(crash_msg)
    link(firec, FRAME, 2.0)
    fmc.inp.queue.append(crash_msg)
    link(firec, FRAME, 2.5)
check("a crash report is logged once, not on every repeat", buf.getvalue().count("the Fire reports a crash") == 1, buf.getvalue())
check("...and the log line carries the description", "BusFault" in buf.getvalue() and "lights" in buf.getvalue())
other = bytearray(C_VECTOR); other[10] = 10                      # total = 10: a NEW crash
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    fmc.inp.queue.append(FakeMsg("sysex", list(other)))
    link(firec, FRAME, 3.0)
check("a different crash (another total) is logged again", buf.getvalue().count("the Fire reports a crash") == 1)
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    fmc.inp.queue.append(FakeMsg("sysex", list(bytes(none_rec))))
    link(firec, FRAME, 3.5)
check("a 'no crash on record' answer is not logged as a crash", "reports a crash" not in buf.getvalue())
check("the full report is saved once per NEW crash (2 crashes, a repeat and a 'none': 2 saves)",
      len(saved_reports) == 2 and saved_reports[1]["total"] == 10, [r["total"] for r in saved_reports])
_fx = H.Fire(FakeMido([NAME], [NAME], FakeOut(), FakeIn()), on_crash=lambda c: 1 / 0)
_fx.inp = FakeIn(); _fx.inp.queue.append(FakeMsg("sysex", list(C_VECTOR)))
with contextlib.redirect_stdout(io.StringIO()) as _b:
    _fx._replies(9.0)
check("a report saver that fails is logged, never kills the service", "could not save the crash report" in _b.getvalue())
check("it still counts the crash reply as the Fire being alive? no: only hello replies keep the link", firec.last_reply <= 0.001, firec.last_reply)
fmc2 = FakeMido([NAME], [NAME], FakeOut(), FakeIn())
firec2 = H.Fire(fmc2)
link(firec2, FRAME, 0.0)
firec2._drop()
fmc2.out = FakeOut()
link(firec2, FRAME, 5.0)
check("after a reconnect it asks again (the Fire may have crashed in between)", any(m.data == tuple(H.HDR + [0x06]) for m in fmc2.out.sent))

H.idle_priority()
check("idle_priority() runs without raising", True)

# ---------------------------------------------------------------- 4.7: send() only sends; service() keeps the link
H._warned.clear()
fs = FakeMido([NAME], [NAME], FakeOut(), FakeIn())
fires = H.Fire(fs)
check("send() alone never opens a port (opening is service()'s job)", fires.send(FRAME, 0.0) is False and fs.opened_out == 0)
check("service() finds and opens the Fire and says hello", fires.service(0.0) is True and fs.opened_out == 1
      and fs.out.sent and fs.out.sent[0].data == tuple(HELLO))
n0 = len(fs.out.sent)
check("send() sends exactly the one message it is given, even when a hello is overdue (no hello inside send)",
      fires.send(FRAME, 50.0) is True and len(fs.out.sent) == n0 + 1 and fs.out.sent[-1].data == tuple(FRAME))
for k in range(40):
    fs.inp.queue.append(FakeMsg("note_on", [60, 100]))
fs.inp.queue.append(REPLY)
fires.service(1.0)
check("service() alone drains the Fire's input queue (no frame needed)", fs.inp.queue == [] and fires.last_reply == 1.0,
      (len(fs.inp.queue), fires.last_reply))


class AutoOut(FakeOut):
    """A live Fire: every hello sent to it is answered on its input port."""
    def __init__(self, inp, alive=True):
        super().__init__()
        self.inp, self.alive = inp, alive

    def send(self, msg):
        super().send(msg)
        if self.alive and tuple(msg.data) == tuple(HELLO):
            self.inp.queue.append(REPLY)


class FakeSrc:
    """A skipback ring that has news except between pause_from and pause_to (the recorder paused / restarting)."""
    def __init__(self, pause_from=1e9, pause_to=1e9):
        self.a, self.b = pause_from, pause_to
        self.w = tone(1000, 0.1)

    def read(self, now):
        return None if self.a <= now < self.b else self.w


def drive(fire_, src_, t_from, t_to, step=0.02, st=None):
    st = st if st is not None else {"started": False, "sent": 0}
    buf_ = io.StringIO()
    t = t_from
    with contextlib.redirect_stdout(buf_):
        while t < t_to:
            H.tick(src_, src_._m, fire_, st, t)
            t = round(t + step, 6)
    return buf_.getvalue(), st


# the pause test: 2 s of audio, 10 s with the ring returning None, 2 s of audio again; a Fire that answers hellos
inp_p = FakeIn()
fp = FakeMido([NAME], [NAME], None, inp_p)
fp.out = AutoOut(inp_p)
firep = H.Fire(fp)
srcp = FakeSrc(2.0, 12.0)
srcp._m = H.Meter()
log1, stp = drive(firep, srcp, 0.0, 2.0)
frames_before = stp["sent"]
hellos_before = sum(1 for m in fp.out.sent if m.data == tuple(HELLO))
log2, stp = drive(firep, srcp, 2.0, 12.0, st=stp)
hellos_pause = sum(1 for m in fp.out.sent if m.data == tuple(HELLO)) - hellos_before
log3, stp = drive(firep, srcp, 12.0, 14.0, st=stp)
check("pause: the ring returning None for 10 s does NOT tear down a healthy link",
      "stopped answering" not in log2 + log3 and fp.opened_out == 1 and firep.out is not None, (fp.opened_out, log2 + log3))
check("pause: hellos keep going while there is no audio (>= 3 in 10 s), and the replies are drained",
      hellos_pause >= 3 and inp_p.queue == [], (hellos_pause, len(inp_p.queue)))
check("pause: no frames go out while the ring is paused, and they resume after it",
      frames_before > 0 and stp["sent"] > frames_before
      and sum(1 for m in fp.out.sent if m.data[:5] == tuple(H.HDR + [0x01])) == stp["sent"], (frames_before, stp["sent"]))

# ...and the dead-check also runs without audio: a Fire that stops answering is dropped even during a pause
inp_q = FakeIn()
fq = FakeMido([NAME], [NAME], None, inp_q)
fq.out = AutoOut(inp_q)
fireq = H.Fire(fq)
srcq = FakeSrc(1.0, 30.0)
srcq._m = H.Meter()
drive(fireq, srcq, 0.0, 1.0)
fq.out.alive = False                                       # the Fire rebooted: its old handle swallows everything
logq, _ = drive(fireq, srcq, 1.0, 11.0)
check("pause: a Fire that stops answering is still noticed with no audio flowing (dropped after ~8 s, port re-opened)",
      logq.count("stopped answering") == 1 and fq.out.closed and fq.opened_out == 2, logq)

# the Fire vanishes for ~60 s (a flash) and comes back on a new port: the service rides it out by itself
H._warned.clear()
inp_f = FakeIn()


class CountMido(FakeMido):
    def __init__(self, *a):
        super().__init__(*a)
        self.lists = 0

    def get_output_names(self):
        self.lists += 1
        return super().get_output_names()


ff = CountMido([NAME], [NAME], None, inp_f)
ff.out = AutoOut(inp_f)
firef = H.Fire(ff)
srcf = FakeSrc()
srcf._m = H.Meter()
drive(firef, srcf, 0.0, 5.0)
old_out = ff.out
old_out.alive = False                                      # the Fire goes into the bootloader: no more replies
ff.outs, ff.ins = [], []                                   # ...and its port disappears
ff.lists = 0
logf, stf = drive(firef, srcf, 5.0, 65.0)
lists_gone = ff.lists
inp_f2 = FakeIn()
ff.outs, ff.ins, ff.inp = [NAME], [NAME], inp_f2
ff.out = AutoOut(inp_f2)
logf2, _ = drive(firef, srcf, 65.0, 75.0, st=stf)
check("a 60 s flash: the link is dropped once, the port list is read at most once per 2 s while it is gone",
      logf.count("stopped answering") == 1 and lists_gone <= 31, (logf.count("stopped answering"), lists_gone))
check("...and when the Fire is back it is re-opened, says hello and gets frames, with no further reconnects",
      ff.opened_out == 2 and "Fire found" in logf2 and "stopped answering" not in logf2
      and ff.out.sent[0].data == tuple(HELLO) and any(m.data[:5] == tuple(H.HDR + [0x01]) for m in ff.out.sent), logf2)
check("...and the journal stays quiet through it (no line per loop)", len((logf + logf2).splitlines()) <= 4, logf + logf2)

# 4.7 (3): a port listing that raises must not crash the service, and is logged once
H._warned.clear()


class BadNames(FakeMido):
    raise_out = True

    def get_output_names(self):
        if self.raise_out:
            raise OSError("ALSA: Resource temporarily unavailable")
        return super().get_output_names()


fb_ = BadNames([NAME], [NAME], FakeOut(), FakeIn())
fireb = H.Fire(fb_)
buf = io.StringIO()
crashed = None
with contextlib.redirect_stdout(buf):
    try:
        r = [fireb.service(t) for t in (0.0, 2.5, 5.0, 7.5)]
    except Exception as e:
        crashed, r = e, None
check("get_output_names() raising: service() returns False, no exception", crashed is None and r == [False] * 4, crashed)
check("...and it is logged once, not every look", buf.getvalue().count("cannot list MIDI ports") == 1, buf.getvalue())
fb_.raise_out = False
check("...and once the listing works the Fire is opened", fireb.service(10.0) is True and fb_.opened_out == 1)
fireb._drop()
fb_.raise_out = True
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    fireb.service(13.0)
check("...and a NEW outage after a good connection is logged again (once per outage, not once per process)",
      buf.getvalue().count("cannot list MIDI ports") == 1, buf.getvalue())


class BadInNames(FakeMido):
    def get_input_names(self):
        raise OSError("gone")


fbi = BadInNames([NAME], [NAME], FakeOut(), FakeIn())
firebi = H.Fire(fbi)
with contextlib.redirect_stdout(io.StringIO()):
    try:
        ok_ = firebi.service(0.0) is False and firebi.out is None
    except Exception:
        ok_ = False
check("get_input_names() raising: no exception, the half-open link is closed again", ok_)

# 4.7 (3): malformed tracks in heads.json are skipped, never a crash
H._warned.clear()


def write_heads(root, tracks):
    with open(os.path.join(root, "heads.json"), "w") as f:
        json.dump({"tracks": tracks}, f)


def try_read(src_, now):
    try:
        return src_.read(now), None
    except Exception as e:
        return None, e


with tempfile.TemporaryDirectory() as tmp:
    frames = 20000
    ramp = (np.arange(frames) % 30000).astype("<i2")
    make_ring(tmp, {2: dict(name="IN3", store="ram", channels=1)}, ramp, ramp, 12000 * 2, frames * 2)
    good = dict(name="IN3", store="ram", channels=1, dir="t2", buffer_bytes=frames * 2)
    for label, bad_t in (("channels 'x'", dict(channels="x")), ("channels 0", dict(channels=0)),
                         ("buffer_bytes None", dict(buffer_bytes=None)), ("buffer_bytes 0", dict(buffer_bytes=0)),
                         ("dir not a string", dict(dir=5))):
        best = dict(name="SMP MAIN", store="ram", channels=2, dir="t2", buffer_bytes=frames * 2)  # ranks first if usable
        best.update(bad_t)
        write_heads(tmp, {"1": best, "2": good})
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            src = H.RingSource(tmp)
            got, err = try_read(src, 0.0)
        check(f"heads.json with a bad track ({label}): no crash, the good track is used",
              err is None and got is not None and src.ch == 1 and src.size == frames * 2, (err, src.ch, src.size))
    write_heads(tmp, {"1": dict(name="SMP MAIN", store="ram", channels=0, dir="t2", buffer_bytes=frames * 2)})
    H._warned.clear()
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        src = H.RingSource(tmp)
        r1 = try_read(src, 0.0)
        r2 = try_read(src, 6.0)
        r3 = try_read(src, 12.0)
    check("only bad tracks: read() = None, no crash (channels 0 used to divide by zero)",
          all(r[0] is None and r[1] is None for r in (r1, r2, r3)), (r1[1], r2[1], r3[1]))
    check("...and the bad track is named in the journal once, not every 5 s re-pick",
          buf.getvalue().count("skipped") == 1, buf.getvalue())
    for label, tracks_ in (("tracks is a list", [good]), ("a track entry is a string", {"1": "t2", "2": good})):
        with open(os.path.join(tmp, "heads.json"), "w") as f:
            json.dump({"tracks": tracks_}, f)
        with contextlib.redirect_stdout(io.StringIO()):
            src = H.RingSource(tmp)
            got, err = try_read(src, 0.0)
        want_ok = (got is None) if isinstance(tracks_, list) else (got is not None)
        check(f"heads.json where {label}: no crash", err is None and want_ok, err)

# 4.7 (4): the averaging matrix gives the same numbers as the per-band loop it replaced
import math
import time as _time

mW = H.Meter()
rngW = np.random.default_rng(47)


def old_band_rms(meter, p):
    return np.array([math.sqrt(float(p[ix].mean())) for ix in meter.idx])


worst = 0.0
for k in range(200):
    p = rngW.exponential(10.0 ** rngW.uniform(-9, 0), len(mW.W[0])) * (rngW.random(len(mW.W[0])) < 0.9)
    o, n_ = old_band_rms(mW, p), mW.band_rms(p)
    worst = max(worst, float(np.max(np.abs(n_ - o) / np.maximum(np.abs(o), 1e-300))))
check("W @ p equals the old per-band loop on 200 random spectra (relative error < 1e-9)", worst < 1e-9, worst)
check("W has one row per band, each row's weights sum to 1 over exactly that band's bins",
      mW.W.shape == (16, 1025) and all(abs(mW.W[i].sum() - 1) < 1e-12 and set(np.nonzero(mW.W[i])[0]) == set(mW.idx[i].tolist())
                                        for i in range(16)))
ov = [i for i in range(15) if set(mW.idx[i].tolist()) & set(mW.idx[i + 1].tolist())]
check("the low bands really overlap (why reduceat can't be used, and W can)", len(ov) > 0, ov)
# in production p is float32 (numpy 2 keeps float32 through the FFT): the old loop averaged in float32, W @ p in
# float64, so compare what the service really uses, the band dB values, on real audio windows
worst_db = 0.0
for k in range(50):
    w_ = (rngW.standard_normal(H.N) * 10.0 ** rngW.uniform(-3, -0.5)).astype("f4") + tone(rngW.uniform(40, 14000), 0.1)
    mA, mB = H.Meter(), H.Meter()
    new_db = mA.bands_db(w_)
    mag = np.abs(np.fft.rfft(w_ * mB.win)) / (H.N / 4.0)
    pB = np.mean([mag * mag], axis=0)
    outB = np.empty(H.NBANDS, "f4")
    for i, ix in enumerate(mB.idx):
        outB[i] = math.sqrt(float(pB[ix].mean()))
    old_db = 20.0 * np.log10(outB + 1e-9) + mB.tilt
    worst_db = max(worst_db, float(np.max(np.abs(new_db - old_db))))
check("on real (float32) audio windows the band dB values match the old loop to < 0.0001 dB", worst_db < 1e-4, worst_db)
# timing, informational only (a CPU-time check would be flaky on a busy Pi)
pp = rngW.random(1025).astype("f4")
t0 = _time.perf_counter(); [old_band_rms(mW, pp) for _ in range(300)]; t_old = (_time.perf_counter() - t0) / 300
t0 = _time.perf_counter(); [mW.band_rms(pp) for _ in range(300)]; t_new = (_time.perf_counter() - t0) / 300
print(f"  info band levels: old loop {t_old * 1e6:.0f} us, W @ p {t_new * 1e6:.0f} us per window")

# ---------------------------------------------------------------- the safety rules, read from the source
src_text = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "host_vu.py"), encoding="utf-8").read()
check("it never writes to the skipback folder (reads only)", 'open(os.path.join(self.dir, "ring.raw"), "rb")' in src_text
      and '"wb"' not in src_text and "'wb'" not in src_text and '"w")' not in src_text)
check("it asks for idle CPU priority first thing in main()", "idle_priority()" in src_text.split("def main(")[1].split("while True")[0])
check("no JACK client (reads memory only)", "jack" not in src_text.lower().replace("no jack client", ""))

total = passed + failed
print(f"\n{passed} passed, {failed} failed" + ("" if total == EXPECTED else f"   (EXPECTED {EXPECTED}, ran {total})"))
sys.exit(0 if failed == 0 and total == EXPECTED else 1)
