"""Mutation check for uni_test.c + compact_test.c: undo each rule of uni.c / compact.c (v0.31) in a temp copy; every
one must turn its test red.
    python3 uni_mutate.py .        (compact_test also uses ~/fire_fw/full_chip_before_v030_20261026.bin if it is there)"""
import os, shutil, subprocess, sys, tempfile
src = sys.argv[1]
d = tempfile.mkdtemp(prefix=".fwmut.", dir=os.path.expanduser("~"))
for f in ("uni.c", "uni.h", "uni_test.c", "compact.c", "compact.h", "compact_test.c", "store2.c", "store2.h", "steps.c", "steps.h"):
    shutil.copy(os.path.join(src, f), d)
IMG = os.path.expanduser("~/fire_fw/full_chip_before_v030_20261026.bin")
TEST = {
    "uni.c": "gcc -O1 -DUNI_TEST -fsanitize=bounds -fsanitize-undefined-trap-on-error -I. uni_test.c uni.c -o t 2>&1 && ./t",
    "compact.c": "gcc -O1 -fsanitize=bounds -fsanitize-undefined-trap-on-error -I. compact_test.c compact.c store2.c steps.c uni.c -o t 2>&1 && ./t " + IMG,
}
M = [
 ("uni.c", "return len >= UNI_HDR && len <= UNI_MAX_LEN && (len - UNI_HDR) % UNI_ENT == 0;", "return len >= UNI_HDR && len <= UNI_MAX_LEN;"),
 ("uni.c", "return (e[1] >> 1) <= UNI_COL_GREY && type >= UT_NOTE && type < UT_N;", "return type < UT_N;"),
 ("uni.c", "if (seen[k >> 3] >> (k & 7) & 1) return UR_INVALID;", ""),
 ("uni.c", "for (int i = 1; i <= UNI_NAME; i++) if (r[i] < 0x20 || r[i] > 0x7E) return UR_INVALID;", ""),
 ("uni.c", "return e[1] >= UK_CC && e[1] < UK_N && e[2] <= 15 && e[5] == 0;", "return e[1] < UK_N && e[2] <= 15;"),
 ("uni.c", "    return (uint16_t)(s & 0x3FFF);\n}\n\nint uni_find", "    return (uint16_t)s;\n}\n\nint uni_find"),
 ("uni.c", "if (r[o] < UNI_PADS + UNI_KNOBS) ix[uni_key(r + o)] = (uint8_t)i;", "if (r[o] >= UNI_PADS ? r[o] < UNI_PADS + UNI_KNOBS : !(r[o + 1] & 1)) ix[uni_key(r + o)] = (uint8_t)i;"),
 ("uni.c", "uint8_t ch = p.ch & 15, vel = p.on ? p.on : 1;", "uint8_t ch = p.ch & 15, vel = p.on;"),
 ("uni.c", "    case UT_HOLD: m[0] = (uint8_t)(0xB0 | ch); m[1] = p.num; m[2] = p.on; *n = 3; return UREL_CC;", "    case UT_HOLD: m[0] = (uint8_t)(0xB0 | ch); m[1] = p.num; m[2] = p.on; *n = 3; return UREL_NONE;"),
 ("uni.c", "m[2] = *tog ? p.on : p.off; *n = 3;", "m[2] = p.on; *n = 3;"),
 ("uni.c", "    case UT_PRESS: m[0] = (uint8_t)(0xB0 | ch); m[1] = p.num; m[2] = p.on; *n = 3; return UREL_NONE;", "    case UT_PRESS: m[0] = (uint8_t)(0xB0 | ch); m[1] = p.num; m[2] = p.on; *n = 3; return UREL_CC;"),
 ("uni.c", "    if (v == *val) return 0;", ""),
 ("uni.c", "uint32_t b = v <= 64 ? (uint32_t)v * 128u : 8192u + (uint32_t)(v - 64) * 8191u / 63u;", "uint32_t b = (uint32_t)v * 128u;"),
 ("uni.c", "    if (mode == m_fader) return has_daw ? m_daw : before;", "    if (mode == m_fader) return m_daw;"),   # v0.38
 ("uni.c", "    if (mode == m_daw) return before;", "    if (mode == m_daw) return m_fader;"),
 ("uni.c", "    return mode == m_note ? m_uni : m_note;", "    return m_uni;"),
 ("uni.c", "    return mode == m_note ? m_uni : m_note;", "    return mode == m_note ? m_uni : mode;"),
 ("uni.c", "else if (knob == 1 && layer == UL_BASE) p->col", "else if (knob == 1) p->col"),
 # latched notes: the off is what was SENT
 ("uni.c", "        m[0] = (uint8_t)(0x80 | L[i].ch); m[1] = L[i].note; m[2] = 0;\n        L[i].key1 = 0;", "        m[0] = (uint8_t)(0x80 | (p.ch & 15)); m[1] = p.num; m[2] = 0;\n        L[i].key1 = 0;"),
 ("uni.c", "        m[0] = (uint8_t)(0x80 | L[i].ch); m[1] = L[i].note; m[2] = 0;\n        L[i].key1 = 0;\n        return 3;", "        m[0] = (uint8_t)(0x80 | L[i].ch); m[1] = L[i].note; m[2] = 0;\n        return 3;"),
 ("uni.c", "    i = latch_at(L, 0);", "    i = 0;"),
 ("uni.c", "(pad >= 0 && (L[i].key1 - 1) >> 1 != pad)", "(pad >= 0 && 0)"),
 ("uni.c", "    case UT_LATCH: return UREL_NONE;", "    case UT_LATCH: m[0] = (uint8_t)(0x90 | ch); m[1] = p.num; m[2] = vel; *n = 3; return UREL_NONE;"),
 # the cursor: right, and O(1) per word
 ("uni.c", "            if (s && s->del) continue;", ""),
 ("uni.c", "&& !s->del && s->key < UNI_KEYS && !bit(c->has, s->key)\n", "&& !s->del && s->key < UNI_KEYS\n"),
 ("uni.c", "        if (want < c->idx || (c->idx >= 0 && !c->e)) {", "        if (1) {"),
 ("uni.c", "        if (bit(c.has, s->key)) { if (s->del) cnt--; }", "        if (bit(c.has, s->key)) { }"),
 ("uni.c", "        if (dup_before(ps, i, page, mask)) continue;", ""),
 ("uni.c", "            && !dup_before(c->ps, c->si - 1, c->page, c->mask)) {", "            ) {"),
 ("uni.c", "    if (used_others + len > UNI_BUDGET) return UR_FULL;", ""),
 ("uni.c", "    if (n != UNI_CHUNK && off + n != len) return UR_BAD;", ""),
 ("uni.c", "    if (off != done) return UR_ORDER;", ""),
 # the clean-up
 ("compact.c", "        if (!compact_page_ok(src[p], len[p])) {", "        if (!src[p] || !uni_len_ok(len[p])) {"),
 ("compact.c", "    return r && uni_len_ok(len) && r[0] == UNI_VER && uni_check(r, len) == UR_OK;", "    return r && uni_len_ok(len);"),
 ("compact.c", "        ps[p].c = c; ps[p].off = used;", "        ps[p].c = c; ps[p].off = 0;"),
 ("compact.c", "        if (len[p]) ok = store2_append_src(CP_KIND_UNI + p, len[p], page_reader, &ps[p]);", "        (void)p;"),
 ("compact.c", "    if (ok && c->crash) ok = store2_append(CP_KIND_CRASH, c->crash, CP_CRASH_LEN);", "    (void)0;"),
 ("compact.c", "        if (c->pat_keep(p)) ok = store2_append_src(", "        if (1) ok = store2_append_src("),
 ("compact.c", "    if (used > c->sa_len + c->sb_len) return CP_REFUSED;", "    if (used > 2 * (c->sa_len + c->sb_len)) return CP_REFUSED;"),
]
red = 0
for fn, a, b in M:
    p = os.path.join(d, fn); s = open(p, encoding="utf-8", newline="").read()
    sn = s.replace("\r\n", "\n")
    if sn.count(a) != 1:
        print("NOT APPLIED", fn, a[:60].replace("\n", " ")); continue
    open(p, "w", encoding="utf-8", newline="").write(sn.replace(a, b, 1))
    r = subprocess.run(TEST[fn], shell=True, cwd=d, capture_output=True, text=True)
    ok = r.returncode != 0; red += ok
    print("red  " if ok else "GREEN", fn, a[:60].replace("\n", " "))
    open(p, "w", encoding="utf-8", newline="").write(s)
print(f"{red}/{len(M)} red")
shutil.rmtree(d)
sys.exit(0 if red == len(M) else 1)
