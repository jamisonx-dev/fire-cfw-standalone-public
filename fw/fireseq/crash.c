#include "crash.h"

crash_decision_t crash_decide(uint32_t prev_count, uint32_t uptime_ms)
{
    crash_decision_t d;
    if (prev_count > 100) prev_count = 100;                        /* (a garbage register value can't wrap or run away) */
    d.count = uptime_ms < CRASH_EARLY_MS ? prev_count + 1 : 1;     /* a crash after a long healthy run starts a new streak */
    d.dfu = d.count >= CRASH_LOOP_COUNT;
    return d;
}

const char *crash_cause_name(uint32_t cause)
{
    switch (cause) {
    case CRASH_HARD: return "HardFault";
    case CRASH_MEM: return "MemManage";
    case CRASH_BUS: return "BusFault";
    case CRASH_USAGE: return "UsageFault";
    case CRASH_WDOG: return "watchdog";
    case CRASH_STACK: return "stack smash";
    case CRASH_WATCH: return "watchpoint";
    case CRASH_SLIP: return "TIM4 stack slip";
    default: return "none";
    }
}

const char *crash_cause_short(uint32_t cause)
{
    switch (cause) {
    case CRASH_STACK: return "stack";
    case CRASH_WATCH: return "watchpt";
    case CRASH_SLIP: return "TIM4 slip";
    case CRASH_HARD: case CRASH_MEM: case CRASH_BUS: case CRASH_USAGE: case CRASH_WDOG: return crash_cause_name(cause);
    default: return "none";
    }
}

const char *crash_stage_short(uint32_t stage)
{
    static const char *const N[] = {"?", "USB", "sequencer", "queues", "buttons", "gates", "saving", "lights", "knobs",
                                    "LEDs", "screen", "tail", "start-up"};
    return stage < sizeof N / sizeof N[0] ? N[stage] : "?";
}

static char *about_str(char *o, const char *s, const char *end)
{
    while (*s && o < end) *o++ = *s++;
    return o;
}

static char *about_uint(char *o, uint32_t v, const char *end)
{
    char t[10];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10);
    while (n && o < end) *o++ = t[--n];
    return o;
}

static char *about_hex(char *o, uint32_t v, const char *end)
{
    for (int sh = 28; sh >= 0 && o < end; sh -= 4) *o++ = "0123456789abcdef"[(v >> sh) & 0xFu];
    return o;
}

void crash_about_value(int row, const crash_info_t *ci, uint32_t misreads, const char *version, uint32_t build,
                       char out[14])
{
    char *o = out;
    const char *end = out + 13;
    int c = ci && ci->valid;
    switch (row) {
    case ABOUT_VERSION: o = about_str(o, version ? version : "?", end); break;
    case ABOUT_BUILD: o = about_hex(o, build, end); break;
    case ABOUT_CAUSE: o = about_str(o, c ? crash_cause_short(ci->cause) : "none", end); break;
    case ABOUT_STAGE: o = about_str(o, c ? crash_stage_short(ci->stage) : "-", end); break;
    case ABOUT_TOTAL: o = about_uint(o, c ? ci->total : 0u, end); break;
    case ABOUT_UPTIME:                              /* how long it had run when it crashed */
        if (!c) o = about_str(o, "-", end);
        else if (ci->up_ms < 600000u) { o = about_uint(o, ci->up_ms / 1000u, end); o = about_str(o, " s", end); }
        else { o = about_uint(o, ci->up_ms / 60000u, end); o = about_str(o, " min", end); }
        break;
    case ABOUT_PC: o = c ? about_hex(o, ci->pc, end) : about_str(o, "-", end); break;
    case ABOUT_LR: o = c ? about_hex(o, ci->lr, end) : about_str(o, "-", end); break;
    case ABOUT_MISREADS: o = about_uint(o, misreads, end); break;
    default: break;
    }
    *o = 0;
}

void crash_pack32(uint32_t v, uint8_t out[5])
{
    for (int i = 0; i < 5; i++) { out[i] = (uint8_t)(v & 0x7F); v >>= 7; }
}

uint32_t crash_unpack32(const uint8_t in[5])
{
    uint32_t v = 0;
    for (int i = 4; i >= 0; i--) v = (v << 7) | (uint32_t)(in[i] & 0x7F);
    return v;
}

int crash_stack_reply(int valid, uint32_t base, const uint32_t *words, uint8_t out[CRASH_STACK_REPLY_LEN])
{
    int n = 0;
    out[n++] = 0x7D; out[n++] = 0x4F; out[n++] = 0x52; out[n++] = 0x43;     /* 7D 'O' 'R' 'C' */
    out[n++] = 0x08;
    out[n++] = 1;                                                           /* reply version */
    out[n++] = valid ? 1 : 0;
    crash_pack32(valid ? base : 0, &out[n]); n += 5;
    for (int i = 0; i < CRASH_STACK_WORDS; i++) { crash_pack32(valid ? words[i] : 0, &out[n]); n += 5; }
    return n;
}

int crash_reply(const crash_info_t *ci, uint8_t out[CRASH_REPLY_LEN])
{
    int n = 0;
    out[n++] = 0x7D; out[n++] = 0x4F; out[n++] = 0x52; out[n++] = 0x43;     /* 7D 'O' 'R' 'C' */
    out[n++] = 0x06;
    out[n++] = 3;                                                           /* reply version (2 = + build id, 3 = + TIM4 note) */
    out[n++] = ci->valid ? 1 : 0;
    out[n++] = (uint8_t)(ci->cause & 0x7F);
    out[n++] = (uint8_t)(ci->stage & 0x7F);
    out[n++] = (uint8_t)(ci->count > 127 ? 127 : ci->count);
    out[n++] = (uint8_t)(ci->total > 127 ? 127 : ci->total);
    const uint32_t f[16] = {ci->pc, ci->lr, ci->psr, ci->cfsr, ci->hfsr, ci->addr, ci->up_ms, ci->build, ci->sp, ci->stack_min,
                            ci->t4_count, ci->t4_ms, ci->t4_kind, ci->t4_seen, ci->t4_want, ci->t4_exc};
    for (int i = 0; i < 16; i++) { crash_pack32(f[i], &out[n]); n += 5; }
    return n;
}

int crash_t4_verdict(uint32_t seen, uint32_t want, uint32_t slot_now, uint32_t want_now, uint32_t exc)
{
    if (seen == want && slot_now == exc && want_now == exc) return T4V_OK;
    if (slot_now != exc) return T4V_CHANGED;        /* the only case where returning is unsafe */
    if (want_now != exc) return T4V_STALE;
    return T4V_MISREAD;
}
