/* Crash recorder: what to remember about a fault / watchdog reset, whether the Fire should restart itself or fall
 * back to DFU (update mode), and how the record travels over the host protocol. Pure C, no hardware: crash_test.c
 * checks it on the host; main.c keeps the record in the RTC backup registers (they survive a reset).
 *
 * Why: on 2026-10-13 21:00 the Fire fell into DFU 63 minutes after a flash and left NO trace of why (every fault
 * handler just rebooted to DFU). Now a fault / watchdog reset is RECORDED (cause, PC, LR, status registers, which
 * stage of the main loop was running) and the Fire restarts itself; only a crash LOOP (3 in a row, each within 30 s
 * of booting) drops to DFU, so a really bad image can still be replaced without a debugger. */
#ifndef CRASH_H
#define CRASH_H
#include <stdint.h>

enum { CRASH_NONE = 0, CRASH_HARD = 1, CRASH_MEM = 2, CRASH_BUS = 3, CRASH_USAGE = 4, CRASH_WDOG = 5, CRASH_STACK = 6, CRASH_WATCH = 7 };   /* 7 = a CPU write hit the watched canary (DWT) */   /* 6 = a stack canary was overwritten (-fstack-protector) */

/* The main-loop stage a hang / fault happened in (written to a backup register as the loop runs). */
enum {
    ST_USB = 1,         /* tud_task + midi_task (incl. the host protocol) */
    ST_SEQ = 2,         /* sequencer grid, steps, clock out */
    ST_QUEUES = 3,      /* note-off and ratchet queues, batch flush */
    ST_EVENTS = 4,      /* handle_events: buttons, pads, encoder, knobs */
    ST_GATES = 5,       /* idle lights + save gating */
    ST_SAVER = 6,       /* saver_poll (flash writes) */
    ST_LIGHTS = 7,      /* lights_task */
    ST_KNOBS = 8,       /* knob polling */
    ST_PAINT = 9,       /* paint: LEDs */
    ST_RENDER = 10,     /* render + OLED flush */
    ST_TAIL = 11,       /* DFU request check */
    ST_BOOT = 12,       /* start-up (load_state etc.) */
};

typedef struct {
    uint32_t valid;     /* 1 = a crash is on record */
    uint32_t cause;     /* CRASH_* */
    uint32_t stage;     /* ST_* */
    uint32_t count;     /* consecutive early crashes (each within CRASH_EARLY_MS of booting) */
    uint32_t total;     /* all crashes recorded since the registers were last cleared */
    uint32_t pc, lr, psr, cfsr, hfsr, addr, up_ms;
    uint32_t build;     /* BUILD_ID of the image that crashed (0 = recorded by an older image: unknown) */
    uint32_t sp;        /* the stack pointer at the fault (where its exception frame was) */
    uint32_t stack_min; /* the lowest the stack had ever reached since boot (painted-stack high-water mark) */
} crash_info_t;

#define CRASH_EARLY_MS 30000u       /* a crash this soon after booting counts towards a crash loop */
#define CRASH_LOOP_COUNT 3          /* this many in a row = DFU (update mode) instead of another restart */

typedef struct { uint32_t count; int dfu; } crash_decision_t;
/* prev_count = the consecutive count so far, uptime_ms = how long the app had been running when it crashed. */
crash_decision_t crash_decide(uint32_t prev_count, uint32_t uptime_ms);

const char *crash_cause_name(uint32_t cause);       /* "HardFault", "watchdog", ... */

/* 32-bit value <-> five 7-bit bytes (MIDI data bytes), low bits first. */
void crash_pack32(uint32_t v, uint8_t out[5]);
uint32_t crash_unpack32(const uint8_t in[5]);

/* The host-protocol reply to command 06, WITHOUT the F0 / F7 framing:
 *   7D 4F 52 43 06 <ver=2> <valid> <cause> <stage> <count> <total> then pc lr psr cfsr hfsr addr up_ms build sp stack_min,
 *   each 5 bytes.
 * (Version 1, 46 bytes, had no build: a host should accept both.)
 * Returns the length (always CRASH_REPLY_LEN); every byte is below 0x80. */
#define CRASH_REPLY_LEN 61
int crash_reply(const crash_info_t *ci, uint8_t out[CRASH_REPLY_LEN]);

/* The stack around the last fault (v0.17c): CRASH_STACK_WORDS words copied upward from the fault's exception frame
 * (R0-R3, R12, LR, PC, xPSR, then the interrupted code's own frames: saved registers and return addresses) into RAM
 * that a reset does not clear. Host command 08 reads it, WITHOUT the F0 / F7 framing:
 *   7D 4F 52 43 08 <ver=1> <valid> <base: 5 bytes> then the words, 5 bytes each. Every byte is below 0x80. */
#define CRASH_STACK_WORDS 64
#define CRASH_STACK_REPLY_LEN (4 + 1 + 1 + 1 + 5 + CRASH_STACK_WORDS * 5)
int crash_stack_reply(int valid, uint32_t base, const uint32_t *words, uint8_t out[CRASH_STACK_REPLY_LEN]);

#endif
