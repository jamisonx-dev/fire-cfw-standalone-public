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

enum { CRASH_NONE = 0, CRASH_HARD = 1, CRASH_MEM = 2, CRASH_BUS = 3, CRASH_USAGE = 4, CRASH_WDOG = 5, CRASH_STACK = 6, CRASH_WATCH = 7, CRASH_SLIP = 8 };   /* 8 = TIM4's own stack check: its SP moved / its saved return changed while it ran */   /* 7 = a CPU write hit the watched canary (DWT) */   /* 6 = a stack canary was overwritten (-fstack-protector) */

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
    /* v3 (2026-11-09): TIM4's last return-slot check mismatch (the "TIM4 check note", fire_hw.c t4_note; LIVE, kept
       across resets, all 0 = none since the note was last cleared). kind = T4V_*; seen / want = what the check read
       (the slot / the yardstick t4_exc), exc = the EXC_RETURN the hardware handed TIM4 (the truth). */
    uint32_t t4_count, t4_ms, t4_kind, t4_seen, t4_want, t4_exc;
} crash_info_t;

/* TIM4's return-slot check (v0.31g): when the slot read != the yardstick read, TIM4 re-reads both after a DSB and
   compares them with the EXC_RETURN it was handed (exc, kept in a register). The verdict:
     T4V_MISREAD = the slot AND the yardstick both hold exc on a second look: one of the check's reads returned a wrong
                   value (memory was fine). Safe to return: noted, not a crash.
     T4V_STALE   = the slot holds exc but the yardstick in RAM doesn't: the entry store to t4_exc was lost / overwritten.
                   Safe to return: noted, the yardstick repaired, not a crash.
     T4V_CHANGED = the slot itself no longer holds exc: returning would pop a bad EXC_RETURN. Crash cause 8, kind 5.
   Why: the 2026-11-09 v0.31f record was a kind-5 slip whose snapshot showed the slot holding a correct EXC_RETURN,
   i.e. the CHECK saw a mismatch that memory did not have, and the check itself reset a healthy Fire. */
enum { T4V_OK = 0, T4V_MISREAD = 1, T4V_STALE = 2, T4V_CHANGED = 3, T4V_ENTRY = 4 };

/* v0.33b. The two 2026-11-11 crashes: TIM4's naked entry `mov r0, sp` (0x4668) ran as `mov r0, r12` (0x4660, one bit),
   so tim4_body was handed r12 (0xFFFFFFFF) as the stack address the interrupted frame starts at. The stack itself was
   fine; the frame check read through that nonsense address and reset a healthy Fire. A real entry address is word
   aligned and a little ABOVE tim4_body's own stack pointer (its frame is ~140 bytes). Anything else can only be a
   wrong r0: T4V_ENTRY = noted (seen = what was handed, want = tim4_body's SP), the address-based checks are skipped
   for that one pass, TIM4 does its work and returns. The note's count is the misread counter. */
#define T4_ENTRY_SPAN 512u
static inline int crash_t4_entry_ok(uint32_t sp_in, uint32_t body_sp)
{
    return !(sp_in & 3u) && sp_in > body_sp && sp_in - body_sp <= T4_ENTRY_SPAN;
}
int crash_t4_verdict(uint32_t seen, uint32_t want, uint32_t slot_now, uint32_t want_now, uint32_t exc);

#define CRASH_EARLY_MS 30000u       /* a crash this soon after booting counts towards a crash loop */
#define CRASH_LOOP_COUNT 3          /* this many in a row = DFU (update mode) instead of another restart */

typedef struct { uint32_t count; int dfu; } crash_decision_t;
/* prev_count = the consecutive count so far, uptime_ms = how long the app had been running when it crashed. */
crash_decision_t crash_decide(uint32_t prev_count, uint32_t uptime_ms);

const char *crash_cause_name(uint32_t cause);       /* "HardFault", "watchdog", ... */

/* v0.35 BROWSER > About (2026-11-12): what a tester photographs and sends in a bug report (no Pi, no tools): the
   version, the build, and the crash on record. One row per call: row = ABOUT_*, out gets the value (<= 13 chars + 0,
   it must fit beside its label on the 128 px screen: menu_render.py checks). Pure, no stdio (newlib's printf would
   not fit in RAM). misreads = the TIM4 misread counter (t4_note.count, 0 when the note is not live). */
enum { ABOUT_VERSION, ABOUT_BUILD, ABOUT_CAUSE, ABOUT_STAGE, ABOUT_TOTAL, ABOUT_UPTIME, ABOUT_PC, ABOUT_LR, ABOUT_MISREADS,
       ABOUT_ROWS };
const char *crash_cause_short(uint32_t cause);      /* <= 10 chars: "HardFault", "watchdog", "TIM4 slip", "none" */
const char *crash_stage_short(uint32_t stage);      /* <= 9 chars: "screen", "buttons", "saving", ..., "?" */
void crash_about_value(int row, const crash_info_t *ci, uint32_t misreads, const char *version, uint32_t build,
                       char out[14]);

/* 32-bit value <-> five 7-bit bytes (MIDI data bytes), low bits first. */
void crash_pack32(uint32_t v, uint8_t out[5]);
uint32_t crash_unpack32(const uint8_t in[5]);

/* The host-protocol reply to command 06, WITHOUT the F0 / F7 framing:
 *   7D 4F 52 43 06 <ver=2> <valid> <cause> <stage> <count> <total> then pc lr psr cfsr hfsr addr up_ms build sp stack_min,
 *   each 5 bytes.
 *   version 3 (v0.31g) adds t4_count t4_ms t4_kind t4_seen t4_want t4_exc (the TIM4 check note), 5 bytes each.
 * (Version 1, 46 bytes, had no build; version 2, 61 bytes, no note: a host should accept all three.)
 * Returns the length (always CRASH_REPLY_LEN); every byte is below 0x80. */
#define CRASH_REPLY_LEN 91
int crash_reply(const crash_info_t *ci, uint8_t out[CRASH_REPLY_LEN]);

/* The stack around the last fault (v0.17c): CRASH_STACK_WORDS words copied upward from the fault's exception frame
 * (R0-R3, R12, LR, PC, xPSR, then the interrupted code's own frames: saved registers and return addresses) into RAM
 * that a reset does not clear. Host command 08 reads it, WITHOUT the F0 / F7 framing:
 *   7D 4F 52 43 08 <ver=1> <valid> <base: 5 bytes> then the words, 5 bytes each. Every byte is below 0x80. */
#define CRASH_STACK_WORDS 64
#define CRASH_STACK_REPLY_LEN (4 + 1 + 1 + 1 + 5 + CRASH_STACK_WORDS * 5)
int crash_stack_reply(int valid, uint32_t base, const uint32_t *words, uint8_t out[CRASH_STACK_REPLY_LEN]);

#endif
