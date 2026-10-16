/* Host test for crash.c.   gcc -O1 -Wall -Wextra -I. crash_test.c crash.c -o crash_test && ./crash_test */
#include <stdio.h>
#include <string.h>
#include "crash.h"

static int fails, checks;
#define EXPECTED 34
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "stackhex")) {      /* a known stack snapshot reply (08), as hex, for host_vu_test.py */
        static uint8_t r[CRASH_STACK_REPLY_LEN];
        uint32_t w[CRASH_STACK_WORDS];
        for (int i = 0; i < CRASH_STACK_WORDS; i++) w[i] = i == 5 ? 0x0800C439u : i == 9 ? 0x08009AB3u : 0x20000000u + (uint32_t)i * 4u;
        crash_stack_reply(1, 0x2000FD68u, w, r);
        for (int i = 0; i < CRASH_STACK_REPLY_LEN; i++) printf("%02x", r[i]);
        putchar(10);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "hex")) {           /* the reply for a known record, as hex: host_vu_test.py decodes the same bytes */
        crash_info_t k = {1, CRASH_BUS, ST_LIGHTS, 2, 9, 0x08012345u, 0x0800ABCDu, 0x61000000u, 0x00000400u, 0x40000000u, 0x20001000u, 3810123u, 0x1A2B3C4Du, 0x2000FE80u, 0x2000E123u};
        uint8_t r[CRASH_REPLY_LEN];
        crash_reply(&k, r);
        for (int i = 0; i < CRASH_REPLY_LEN; i++) printf("%02x", r[i]);
        putchar(10);
        return 0;
    }
    /* the restart / DFU decision */
    crash_decision_t d = crash_decide(0, 5000);
    CHECK(d.count == 1 && !d.dfu, "a first crash soon after boot: restart (count 1)");
    d = crash_decide(1, 6000);
    CHECK(d.count == 2 && !d.dfu, "a second one in a row: still restart");
    d = crash_decide(2, 2000);
    CHECK(d.count == 3 && d.dfu, "the third in a row: DFU (update mode), so a bad image can be replaced");
    d = crash_decide(2, CRASH_EARLY_MS);
    CHECK(d.count == 1 && !d.dfu, "a crash after a long healthy run starts a new streak (exactly 30 s counts as healthy)");
    d = crash_decide(2, CRASH_EARLY_MS - 1);
    CHECK(d.count == 3 && d.dfu, "29.999 s still counts as early");
    d = crash_decide(0xFFFFFFFFu, 100);
    CHECK(d.count == 101 && d.dfu, "a garbage register value cannot wrap the counter (clamped to 100, then +1)");
    d = crash_decide(0, 0);
    CHECK(d.count == 1 && !d.dfu, "uptime 0 (a crash at once): restart, not DFU, on the first try");
    uint32_t c = 0; int dfu_at = -1;
    for (int i = 1; i <= 5; i++) { d = crash_decide(c, 1000); c = d.count; if (d.dfu && dfu_at < 0) dfu_at = i; }
    CHECK(dfu_at == 3, "a crash loop reaches DFU on exactly the third crash");

    /* names */
    CHECK(!strcmp(crash_cause_name(CRASH_HARD), "HardFault") && !strcmp(crash_cause_name(CRASH_WDOG), "watchdog"), "cause names");
    CHECK(!strcmp(crash_cause_name(CRASH_MEM), "MemManage") && !strcmp(crash_cause_name(CRASH_BUS), "BusFault") && !strcmp(crash_cause_name(CRASH_USAGE), "UsageFault")
          && !strcmp(crash_cause_name(CRASH_STACK), "stack smash")
          && !strcmp(crash_cause_name(CRASH_WATCH), "watchpoint"), "all seven causes have names");
    CHECK(!strcmp(crash_cause_name(0), "none") && !strcmp(crash_cause_name(99), "none"), "unknown causes are 'none'");

    /* packing */
    static const uint32_t V[] = {0u, 1u, 0x7Fu, 0x80u, 0x3FFFu, 0x4000u, 0x08008ABCu, 0x20010000u, 0xDEADBEEFu, 0xFFFFFFFFu, 0x80000000u};
    int ok = 1, hi = 0;
    for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
        uint8_t b[5];
        crash_pack32(V[i], b);
        if (crash_unpack32(b) != V[i]) ok = 0;
        for (int k = 0; k < 5; k++) if (b[k] & 0x80) hi = 1;
    }
    CHECK(ok, "pack32 / unpack32 round-trip for edge values (0, 0x7F, 0x80, 0x3FFF, 0x4000, flash and RAM addresses, 0xFFFFFFFF)");
    CHECK(!hi, "packed bytes are always MIDI data bytes (< 0x80)");
    uint32_t seed = 12345; ok = 1;
    for (int i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        uint8_t b[5]; crash_pack32(seed, b);
        if (crash_unpack32(b) != seed) ok = 0;
    }
    CHECK(ok, "20000 random values round-trip");
    uint8_t one[5];
    crash_pack32(0x7Fu, one);
    CHECK(one[0] == 0x7F && one[1] == 0 && one[4] == 0, "low bits come first");
    crash_pack32(0x80u, one);
    CHECK(one[0] == 0 && one[1] == 1, "bit 7 spills into the second byte");

    /* the reply */
    crash_info_t ci = {1, CRASH_BUS, ST_LIGHTS, 2, 9, 0x08012345u, 0x0800ABCDu, 0x61000000u, 0x00000400u, 0x40000000u, 0x20001000u, 3810123u, 0x1A2B3C4Du, 0x2000FE80u, 0x2000E123u};
    uint8_t r[CRASH_REPLY_LEN];
    int n = crash_reply(&ci, r);
    CHECK(n == CRASH_REPLY_LEN, "the reply is CRASH_REPLY_LEN bytes");
    CHECK(r[0] == 0x7D && r[1] == 0x4F && r[2] == 0x52 && r[3] == 0x43 && r[4] == 0x06, "it starts 7D 'O' 'R' 'C' 06");
    int low = 1; for (int i = 0; i < n; i++) if (r[i] & 0x80) low = 0;
    CHECK(low, "every byte of the reply is < 0x80 (valid SysEx data)");
    CHECK(r[5] == 2 && r[6] == 1 && r[7] == CRASH_BUS && r[8] == ST_LIGHTS && r[9] == 2 && r[10] == 9, "version, valid, cause, stage, count, total");
    CHECK(crash_unpack32(&r[11]) == 0x08012345u && crash_unpack32(&r[16]) == 0x0800ABCDu, "PC and LR come back exactly");
    CHECK(crash_unpack32(&r[21]) == 0x61000000u && crash_unpack32(&r[26]) == 0x00000400u, "xPSR and CFSR come back exactly");
    CHECK(crash_unpack32(&r[31]) == 0x40000000u && crash_unpack32(&r[36]) == 0x20001000u && crash_unpack32(&r[41]) == 3810123u, "HFSR, the fault address and the uptime come back exactly");
    CHECK(crash_unpack32(&r[46]) == 0x1A2B3C4Du && crash_unpack32(&r[51]) == 0x2000FE80u && crash_unpack32(&r[56]) == 0x2000E123u,
          "the build id, the stack pointer at the fault and the stack's lowest point come back exactly");
    crash_info_t none = {0};
    crash_reply(&none, r);
    CHECK(r[6] == 0 && r[7] == 0, "no crash on record: valid = 0, cause = 0");
    crash_info_t big = ci; big.count = 500; big.total = 9999; big.cause = 0x1234; big.stage = 0x4321;
    crash_reply(&big, r);
    low = 1; for (int i = 0; i < CRASH_REPLY_LEN; i++) if (r[i] & 0x80) low = 0;
    CHECK(low && r[9] == 127 && r[10] == 127, "oversized counters are clamped and never produce a byte >= 0x80");
    CHECK(sizeof(uint32_t) == 4, "(sanity: 32-bit words)");

    /* the stack snapshot reply (08) */
    {
        static uint8_t sr[CRASH_STACK_REPLY_LEN];
        uint32_t w[CRASH_STACK_WORDS];
        for (int i = 0; i < CRASH_STACK_WORDS; i++) w[i] = 0x08000001u + (uint32_t)i * 0x01010101u;
        int sn = crash_stack_reply(1, 0x2000FD40u, w, sr);
        int all = sn == CRASH_STACK_REPLY_LEN && sr[4] == 0x08 && sr[5] == 1 && sr[6] == 1 && crash_unpack32(&sr[7]) == 0x2000FD40u;
        for (int i = 0; i < CRASH_STACK_WORDS; i++) if (crash_unpack32(&sr[12 + 5 * i]) != w[i]) all = 0;
        for (int i = 0; i < sn; i++) if (sr[i] & 0x80) all = 0;
        CHECK(all, "stack reply: header, base and all 64 words come back exactly, every byte < 0x80");
        crash_stack_reply(0, 0x2000FD40u, w, sr);
        int zero = sr[6] == 0;
        for (int i = 7; i < CRASH_STACK_REPLY_LEN; i++) if (sr[i]) zero = 0;
        CHECK(zero, "no snapshot: valid = 0 and nothing else is sent (no stale words)");
    }

    /* the stage numbers are small and distinct (they travel as one 7-bit byte) */
    int stages[] = {ST_USB, ST_SEQ, ST_QUEUES, ST_EVENTS, ST_GATES, ST_SAVER, ST_LIGHTS, ST_KNOBS, ST_PAINT, ST_RENDER, ST_TAIL, ST_BOOT};
    ok = 1;
    for (unsigned i = 0; i < sizeof stages / sizeof stages[0]; i++) {
        if (stages[i] <= 0 || stages[i] > 127) ok = 0;
        for (unsigned j = i + 1; j < sizeof stages / sizeof stages[0]; j++) if (stages[i] == stages[j]) ok = 0;
    }
    CHECK(ok, "stage numbers are distinct and fit a data byte");
    CHECK(CRASH_REPLY_LEN == 4 + 1 + 6 + 10 * 5, "the reply length is the sum of its parts");
    CHECK(CRASH_LOOP_COUNT == 3 && CRASH_EARLY_MS == 30000u, "the policy constants are what the docs say (3 crashes, 30 s)");
    CHECK(crash_decide(1, 1).count == 2, "decide is pure: the same input twice gives the same answer");
    CHECK(crash_decide(1, 1).count == crash_decide(1, 1).count, "(and again)");

    printf("crash_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
