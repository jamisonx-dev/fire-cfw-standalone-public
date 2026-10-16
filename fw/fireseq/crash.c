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
    default: return "none";
    }
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
    out[n++] = 2;                                                           /* reply version (2 = with the build id) */
    out[n++] = ci->valid ? 1 : 0;
    out[n++] = (uint8_t)(ci->cause & 0x7F);
    out[n++] = (uint8_t)(ci->stage & 0x7F);
    out[n++] = (uint8_t)(ci->count > 127 ? 127 : ci->count);
    out[n++] = (uint8_t)(ci->total > 127 ? 127 : ci->total);
    const uint32_t f[10] = {ci->pc, ci->lr, ci->psr, ci->cfsr, ci->hfsr, ci->addr, ci->up_ms, ci->build, ci->sp, ci->stack_min};
    for (int i = 0; i < 10; i++) { crash_pack32(f[i], &out[n]); n += 5; }
    return n;
}
