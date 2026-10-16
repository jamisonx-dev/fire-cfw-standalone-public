/* "ORC FW seq" v0.14: the Fire as a standalone step sequencer + live pads, ORC-style controls.
 *
 * 24 tracks x up to 128 steps, each track with its OWN length (polyrhythms), 8 patterns, all saved.
 *   T1-T16 drum tracks (one note each); M1-M8 melodic tracks (every step has its own note).
 *   Pads              4 visible tracks x 16 visible steps: tap toggles a step
 *   SHIFT + pad       velocity tier on a lit step: accent (127) -> soft (60) -> normal (100)
 *   ALT + pad         that track's length = up to this step (1-128)
 *   hold pad + knob 1 ratchet 1-4x;  hold pad + knob 2 probability 100/75/50/25 %  (tap a lit pad = off on release)
 *   hold pad + knob 3 the step's note (melodic tracks);  hold pad + knob 4 gate 25/50/75/95 % / TIE / SLIDE
 *   KNOB MODE button  step knob page 2 (MIXER lit): knob 1 condition, 2 lock value, 3 chord (melodic)
 *   hold row + knob 3 that track's lock CC;  hold PERFORM = FILL (for FILL conditions)
 *   tap PERFORM       FADERS: up to 32 CC faders, 4 rows at a time, each its own CC / channel / colour
 *   GRID < >          step page (1-16, 17-32 ... 113-128)
 *   SELECT turn       scroll tracks one row        SHIFT + SELECT: tempo (SHIFT+ALT: x10)
 *   SHIFT + PATTERN   scroll tracks a page (4 rows)
 *   PATTERN up/down   pattern 1-32 (queued to the 16-step bar while playing); ALT + it = duplicate into it
 *   hold PATTERN      the pattern picker: tap one of patterns 1-32 (top two rows); knob 1 = its colour
 *   PLAY / STOP       start-stop / stop + rewind;  SHIFT + PLAY = MIDI clock out on/off
 *   PATTERN/SONG      song mode on/off;  SHIFT + it = song editor (top row slots, 2nd row pattern | bars)
 *   Row buttons       tap = mute that track; hold + knob 1 = its channel, hold + knob 2 = its note
 *   Knob 1            swing 50-75 %      Knobs 2-4: relative CC 17-19 on channel 1 (65 = +1, 63 = -1)
 *   STEP / NOTE / DRUM  what the pads do: edit steps / play a keyboard / play the 16 tracks (the sequencer keeps running)
 *   NOTE mode         rows a fourth apart, C lit; GRID < > octave (SHIFT: semitone); SELECT = which melodic
 *                     track it plays (M1-M8, on that track's channel), ALT + SELECT = that channel; REC = record into it
 *   DRUM mode         four 4x4 grids side by side (pad 1 bottom-left), each with its own channel + notes
 *                     (chromatic from a base note, or the MPC's panel pads: CC on ch 14); ALT + tap a group, then knobs 1-3 edit it;
 *                     REC = record hits into the pattern (onto the track with that note + channel, else a free track)
 *   Live pads         fixed velocity 100, SHIFT held = accent 127
 *   MIDI in           follows external clock (F8 / FA / FB / FC); F0 47 7F 43 72 00 00 F7 = reboot to DFU
 * Steps are stored sparsely (steps.c: only lit steps take memory, a shared pool for all patterns).
 * Saved to flash sector 5 (store2.c), 3 s after the last edit; only changed patterns are written.
 * Moving around (pattern choice, mutes, octave ...) doesn't start a save; it rides along with the next one.
 * Safety nets: watchdog (~2 s), watchdog reset -> DFU, faults -> DFU.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "tusb.h"
#include "draw.h"
#include "oled.h"
#include "fire_hw.h"
#include "store2.h"
#include "store.h"                                  /* legacy (v1/v2) format: migration only */
#include "steps.h"
#include "play.h"
#include "lights.h"
#include "screens.h"
#include "crash.h"
#include "ctl.h"                                    /* the control decisions (tested on the host: ctl_test.c) */

/* noinline on the functions main() calls that own buffers or parse input (render, draw_*, host_cmd, midi_*,
   handle_events, paint): main() never returns, so anything inlined into it is never canary-checked
   (-fstack-protector-strong checks at a function's return). Found by the 2026-10-14 crash hunt. */
/* The build id: a hash of the sources (Makefile), so the same sources always give the same image. It goes into a
   crash record, and host_crash.py finds this tag in the ELF to refuse to symbolise against the wrong build. */
#ifndef BUILD_ID
#define BUILD_ID 0u
#define BUILD_ID_STR "00000000"
#endif
__attribute__((used)) static const char BUILD_TAG[] = "ORCBUILD=" BUILD_ID_STR;
#include <math.h>

#define DTRACKS 16                                  /* drum tracks T1-T16: one note per track */
#define MTRACKS 8                                   /* melodic tracks M1-M8: a note per step */
#define TRACKS (DTRACKS + MTRACKS)
#define MAXSTEPS 128
#define PATTERNS 32
#define VIEW_ROWS 4
#define PAGE 16
#define PAGES (MAXSTEPS / PAGE)

volatile uint32_t ms_ticks;
void SysTick_Handler(void) { ms_ticks++; }
uint32_t tusb_time_millis_api(void) { return ms_ticks; }
void OTG_FS_IRQHandler(void) { tusb_int_handler(0, true); }

static void bk_enable(void)                         /* the RTC backup registers: they survive a reset */
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
}

static void __attribute__((noreturn)) reboot_to_dfu(void)
{
    __disable_irq();
    bk_enable();
    RTC->BKP0R = 1u;                                /* the Akai bootloader reads this: 1 = go to DFU (update mode) */
    NVIC_SystemReset();
}

/* Crash recorder (crash.h). Backup registers: 0 = DFU flag, 1 = 0xC0DE0000 | cause (a valid record), 2 = the main-loop
   stage right now, 3-8 = PC, LR, xPSR, CFSR, HFSR, fault address, 9 = uptime ms at the crash, 10 = consecutive early
   crashes, 11 = total, 12 = heartbeat (ms, written every second), 13 = the stage the crash happened in. */
#define BK_MAGIC 0xC0DE0000u
static inline void crash_stage(uint32_t st) { RTC->BKP2R = st; }
static inline bool bk_valid(void) { return (RTC->BKP1R & 0xFFFF0000u) == BK_MAGIC; }

static void crash_record(uint32_t cause, uint32_t pc, uint32_t lr, uint32_t psr, uint32_t cfsr, uint32_t hfsr, uint32_t addr,
                         uint32_t up_ms, uint32_t stage, bool *dfu)
{
    uint32_t prev = bk_valid() ? RTC->BKP10R : 0, total = bk_valid() ? RTC->BKP11R : 0;
    crash_decision_t d = crash_decide(prev, up_ms);
    RTC->BKP3R = pc; RTC->BKP4R = lr; RTC->BKP5R = psr; RTC->BKP6R = cfsr; RTC->BKP7R = hfsr; RTC->BKP8R = addr;
    RTC->BKP9R = up_ms; RTC->BKP10R = d.count; RTC->BKP11R = total + 1; RTC->BKP13R = stage;
    RTC->BKP14R = BUILD_ID;                         /* which image crashed: an address only means something against ITS ELF */
    RTC->BKP1R = BK_MAGIC | cause;
    *dfu = d.dfu != 0;
}

/* Stack high-water mark: at boot the free RAM between the end of .bss and the stack is filled with a pattern; the
   lowest word that no longer holds it is the deepest the stack has reached (RAM survives a reset, so a watchdog
   restart can still read the previous run's mark before painting again). BKP15 = SP at a fault, 16 = the low point. */
extern uint32_t _enoinit, _estack;                  /* the stack's free space runs from _enoinit (after .bss + .noinit) */
#define STACK_PAINT 0xA5C3A5C3u
static void stack_paint(void)
{
    uint32_t *p = &_enoinit, *lim = (uint32_t *)(__get_MSP() - 256);
    while (p < lim) *p++ = STACK_PAINT;
}
static uint32_t stack_low(void)                     /* address of the deepest stack word ever written (the top = none) */
{
    const uint32_t *p = &_enoinit;
    while (p < &_estack && *p == STACK_PAINT) p++;
    return (uint32_t)p;
}

/* The stack around the last fault, kept across the reset (.noinit: not zeroed, not painted): the exception frame and
   above it the interrupted code's saved registers and return addresses = a call chain (host command 08). */
#define SNAP_MAGIC 0x5AC4F00Du
static struct { uint32_t magic, base, w[CRASH_STACK_WORDS]; } crash_snap __attribute__((section(".noinit")));
static void snap_take(const uint32_t *from)
{
    crash_snap.magic = 0;
    const uint32_t *top = &_estack;
    if (from < (const uint32_t *)0x20000000u || from >= top) return;   /* not a RAM stack address: nothing to keep */
    for (int i = 0; i < CRASH_STACK_WORDS; i++) crash_snap.w[i] = from + i < top ? from[i] : 0;
    crash_snap.base = (uint32_t)from;
    crash_snap.magic = SNAP_MAGIC;
}

/* SRAM is never code here (.data is data only), so the MPU marks it execute-never: a jump into RAM (a smashed return
   address or function pointer, the 2026-10-13 22:09 crash) faults on its FIRST instruction as a MemManage fault with
   PC = that RAM address, while LR still names the call / return that went wrong. Everything else keeps the default
   map (PRIVDEFENA); the firmware runs privileged throughout. */
static void mpu_ram_xn(void)
{
    MPU->CTRL = 0;
    MPU->RNR = 0;
    MPU->RBAR = 0x20000000u;
    MPU->RASR = MPU_RASR_XN_Msk | (3u << MPU_RASR_AP_Pos) | (1u << MPU_RASR_TEX_Pos) | MPU_RASR_C_Msk | MPU_RASR_B_Msk
              | (15u << MPU_RASR_SIZE_Pos) | MPU_RASR_ENABLE_Msk;   /* 64 KB (2^(15+1)), full access, normal WBWA, XN */
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB(); __ISB();
}

/* A fault: record it and restart the app (a crash LOOP, 3 early crashes in a row, still goes to DFU). */
void __attribute__((noreturn, used)) crash_from_fault(const uint32_t *f, uint32_t cause)
{
    __disable_irq();
    bk_enable();
    uint32_t cfsr = SCB->CFSR;
    uint32_t addr = (cfsr & (1u << 15)) ? SCB->BFAR : (cfsr & (1u << 7)) ? SCB->MMFAR : 0;   /* BFARVALID / MMARVALID */
    if (cause == CRASH_WATCH) { addr = DWT->COMP0; DWT->FUNCTION0 = 0; }   /* the watched word that was written */
    bool dfu;
    crash_record(cause, f ? f[6] : 0, f ? f[5] : 0, f ? f[7] : 0, cfsr, SCB->HFSR, addr, ms_ticks, RTC->BKP2R, &dfu);
    RTC->BKP15R = (uint32_t)f; RTC->BKP16R = stack_low();
    snap_take(f);
    RTC->BKP0R = dfu ? 1u : 0u;
    NVIC_SystemReset();
}

/* -fstack-protector-strong: every function with a local array checks a guard word on return. A buffer that overran
   lands here (cause "stack smash") with PC = the function whose buffer it was, instead of returning into garbage
   (the 2026-10-13 22:09 crash: HardFault, invalid state, PC 0x30, LR in the stack = a smashed return address). */
uint32_t __stack_chk_guard = 0xE2B7C94Du;
void __attribute__((noreturn, used)) __stack_chk_fail(void)
{
    uint32_t ra = (uint32_t)__builtin_return_address(0);
    __disable_irq();
    bk_enable();
    bool dfu;
    crash_record(CRASH_STACK, ra, ra, __get_xPSR(), 0, 0, 0, ms_ticks, RTC->BKP2R, &dfu);
    RTC->BKP15R = __get_MSP(); RTC->BKP16R = stack_low();
    snap_take((const uint32_t *)__get_MSP());
    RTC->BKP0R = dfu ? 1u : 0u;
    NVIC_SystemReset();
}

/* The handlers read which stack the fault came from, hand its frame (R0-R3, R12, LR, PC, xPSR) to crash_from_fault. */
#define CRASH_VECTOR(name, cause) \
    void __attribute__((naked)) name(void) \
    { __asm volatile("tst lr, #4\n\tite eq\n\tmrseq r0, msp\n\tmrsne r0, psp\n\tmovs r1, #" #cause "\n\tb crash_from_fault\n\t"); }
CRASH_VECTOR(HardFault_Handler, 1)                  /* CRASH_HARD */
CRASH_VECTOR(MemManage_Handler, 2)                  /* CRASH_MEM */
CRASH_VECTOR(BusFault_Handler, 3)                   /* CRASH_BUS */
CRASH_VECTOR(UsageFault_Handler, 4)                 /* CRASH_USAGE */
CRASH_VECTOR(DebugMon_Handler, 7)                   /* CRASH_WATCH: the DWT canary watchpoint (fire_hw.c TIM4) fired: stacked PC =
                                                       just after the writing instruction, xPSR = the context that wrote */

/* The debug monitor (no debugger needed): DWT watchpoints raise DebugMon. Priority 0, so it preempts TIM4 itself. */
static void dbgmon_init(void)
{
    DWT->FUNCTION0 = 0;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk | CoreDebug_DEMCR_MON_EN_Msk;
    NVIC_SetPriority(DebugMonitor_IRQn, 0);
}

static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->PLLCFGR = 0x07415408u;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SystemCoreClock = 84000000u;
}

static void usb_pins_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER   = (GPIOA->MODER & ~(0xFu << 22)) | (0xAu << 22);
    GPIOA->OSPEEDR |= 0xFu << 22;
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~(0xFFu << 12)) | (0xAAu << 12);
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
}

/* 1 MHz free-running microsecond clock (TIM2, 32-bit) */
static void us_clock_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIM2->PSC = 83;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = 1;
    TIM2->CR1 = TIM_CR1_CEN;
}
static inline uint32_t us_now(void) { return TIM2->CNT; }

/* ---------- tracks, patterns, settings ---------- */

static const uint8_t DEF_NOTE[DTRACKS] = {36, 38, 42, 46, 39, 37, 41, 45, 48, 49, 51, 56, 54, 70, 75, 63};
/* track colours, R,G,B brightness 0-7 (repeats every 8 tracks) */
static const uint8_t TRACK_RGB[8][3] = {
    {7, 0, 0}, {6, 4, 0}, {0, 7, 0}, {0, 3, 7}, {6, 0, 6}, {0, 6, 6}, {6, 6, 0}, {4, 4, 7},
};
enum { LV_OFF, LV_SOFT, LV_NORMAL, LV_ACCENT };
static const uint8_t LEVEL_VEL[4] = {0, 60, 100, 127};
static const uint8_t LEVEL_BRIGHT[4] = {0, 2, 4, 7};

/* Saved global record. Fields are only ever appended; zero = default.
   (Patterns: steps.c. Old full-size pattern records, v0.4-v0.9, are converted at boot: load_state.) */
typedef struct {
    uint16_t bpm;
    uint8_t cur_pat, swing, clock_out, reserved0;
    uint16_t mute_bits;                             /* T1-T16 */
    uint8_t note[DTRACKS];                          /* 0 = default */
    uint8_t chan[DTRACKS];                          /* 1-16, 0 = 10 */
    uint8_t lock_cc[TRACKS];                        /* v0.11 (was reserved, always 0): the CC a track's step locks send, 0 = none */
    /* v0.7: song mode (appended: older saves load these as zero = song off, empty) */
    uint8_t song_on;                                /* 1 = playback follows the song */
    uint8_t song_pat[16];                           /* slot -> pattern 1-32, 0 = empty (the song ends there) */
    uint8_t song_rep[16];                           /* slot -> bars 1-8, 0 = 1 */
    /* v0.8: live keyboard (taken from the reserved bytes: older saves read zero = defaults) */
    uint8_t kb_track;                               /* NOTE mode plays melodic track M(1 + this), 0-7 */
    uint8_t kb_base;                                /* NOTE mode bottom-left note, 0 = 48 (C3) */
    /* v0.8: DRUM mode = four 4x4 pad grids side by side, each with its own channel and notes */
    uint8_t grid_chan[4];                           /* 1-16, 0 = default (10, 1, 2, 3) */
    uint8_t grid_base[4];                           /* chromatic layout: pad 1's note 1-112, 0 = 36 */
    uint8_t grid_layout[4];                         /* 0 = chromatic from grid_base, 1 = classic MPC note map */
    /* v0.9: melodic tracks */
    uint8_t m_chan[MTRACKS];                        /* 1-16, 0 = default (M1 = 1 ... M8 = 8) */
    uint8_t m_mute;                                 /* bit i = M(i+1) muted */
    uint8_t reserved2[7];
    /* v0.13: faders (appended: an older save reads fad_init 0 -> the default 16 are set up) */
    uint8_t fad_init;                               /* 1 = the list below has been set up */
    uint8_t fad_n;                                  /* how many faders, 0-32 */
    uint8_t fad_cc[32], fad_ch[32], fad_col[32], fad_val[32];   /* CC 0-127, channel 1-16, colour 0-15, value 0-127 */
    /* v0.15: lights + menu (taken from the reserved bytes: an older save reads lt_init 0 -> defaults) */
    uint8_t lt_init;                                /* 1 = the lights settings below have been set up */
    uint8_t lt_mode;                                /* 0-12 = that effect, 13 = CYCLE */
    uint8_t lt_speed;                               /* 0-127, 64 = x1 (2^((v-64)/32): x0.25 .. x4) */
    uint8_t lt_idle;                                /* index into LT_IDLE_S: when the pads go to lights on their own */
    uint8_t lt_smooth;                              /* 1 = temporal dithering between pad levels */
    uint8_t autosave_off;                           /* 1 = nothing saves until SAVE NOW (menu) */
    uint8_t lt_bars;                                /* VU bar look: 0 snappy, 1 smooth, 2 whole, 3 heat, 4 heat smooth (0 = what an older save reads) */
    uint8_t lt_flip;                                /* 1 = the heat looks are flipped: cool base, hot tip (0 = what an older save reads) */
    uint8_t lt_idle_v18;                            /* v0.18: 1 = Idle start was switched OFF once (the owner never wanted the screen saver) */
    uint8_t reserved3[5];
    /* v0.14: 32 patterns, each with a colour (appended) */
    uint8_t pat_col[32];                            /* colour 0-15 + 1, 0 = default (pattern number mod 16) */
    /* v0.20 (appended: an older save reads zeros = exactly the old behaviour) */
    uint8_t knob_bank;                              /* free knobs: bank 0-3 (KNOB MODE with nothing held; the 4 lights show it) */
    uint8_t kb_fn[4][4];                            /* [bank][knob]: 0 = default, 1-127 = CC n, KB_SWING, KB_TEMPO */
    uint8_t kb_ch[4][4];                            /* 0 = channel 1, 1-16 */
    uint8_t kb_abs[4][4];                           /* 0 = relative (64 +- steps), 1 = absolute (a value 0-127 the knob moves) */
    uint8_t mark_off;                               /* 1 = no beat / bar marks on the step grid */
    uint8_t mark_col;                               /* index into MARK_RGB (0 = dim blue) */
    uint8_t mark_space;                             /* 0 = every beat, 1 = every bar, 2 = both (the bar one brighter) */
} global_t;

_Static_assert(TRACKS == ST_TRACKS && DTRACKS == ST_MEL_FIRST && PATTERNS == ST_PATTERNS && MAXSTEPS == ST_MAXSTEPS, "steps.h sizes");
_Static_assert(ST_REC_HDR + ST_POOL <= S2_MAX_RECORD, "a pattern must fit one save record");
_Static_assert(sizeof(global_t) == 356, "global_t changed size: only APPEND fields (older saves zero-fill)");
static global_t G;
static global_t G_saved;                            /* what the newest global record holds (skip identical saves) */
static global_t G_snap;                             /* the settings record being written: a copy, so edits never tear it */
static uint32_t saving_size;                        /* pool bytes of the pattern being written (-> steps_saved when done) */
static uint32_t pat_stuck;                          /* bit p: pattern p's saved record didn't fit at boot: never overwrite it */
#define KIND_PAT(p) (17 + (p))                      /* pattern records (steps.c v2, self-describing) */
#define KIND_PAT_V1(p) (9 + (p))                    /* v0.10's sparse records; kinds 1-8 = the old full-size ones */
static uint32_t pat_dirty;                          /* bit p = pattern p needs saving */
static bool global_dirty;

#define BPM     G.bpm
#define CURPAT  G.cur_pat
static inline bool is_mel(int t) { return t >= DTRACKS; }
static inline int tlen(int p, int t) { int l = steps_len[p][t]; return l ? l : 16; }
static inline int level(int p, int t, int s) { return steps_get(p, t, s) & 3; }
static inline uint8_t tnote(int t) { return G.note[t] ? G.note[t] : DEF_NOTE[t]; }   /* drum tracks only */
static inline uint8_t tchan(int t)
{
    if (is_mel(t)) { uint8_t c = G.m_chan[t - DTRACKS]; return (c >= 1 && c <= 16) ? c : (uint8_t)(t - DTRACKS + 1); }
    return (G.chan[t] >= 1 && G.chan[t] <= 16) ? G.chan[t] : 10;
}
static inline bool muted(int t) { return is_mel(t) ? (G.m_mute >> (t - DTRACKS) & 1) : (G.mute_bits >> t & 1); }
static inline void toggle_mute(int t) { if (is_mel(t)) G.m_mute ^= (uint8_t)(1u << (t - DTRACKS)); else G.mute_bits ^= (uint16_t)(1u << t); }
/* the note a step plays: a melodic step's own note, a drum track's note */
static inline uint8_t step_note(int p, int t, int s) { if (!is_mel(t)) return tnote(t); uint8_t n = steps_note(p, t, s); return n ? n : 60; }
static inline int kb_track(void) { return DTRACKS + G.kb_track % MTRACKS; }   /* NOTE mode's melodic track */
static inline int kb_base(void) { return (G.kb_base >= 1 && G.kb_base <= 96) ? G.kb_base : 48; }

/* DRUM groups ("grids" in the code): group g = columns 4g..4g+3; pad 1 bottom-left, pad 16 top-right (MPC numbering). */
static const uint8_t GRID_DEF_CHAN[4] = {10, 1, 2, 3};
/* Layout 1 = the MPC's front-panel pads, the owner's JJOS2XL remote map (bank A; control channel 14): pads 1-12 =
   CC 20-31, pads 13-16 = CC 52-55. A press sends 127, the release 0: it presses the MPC's pad, so it works on any
   MPC screen (NEXT SEQ, 16 levels ...). Plain notes to the MPC are the chromatic layout. */
#define MPC_PANEL_CH 14
static const uint8_t MPC_PAD_CC[16] = {20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 52, 53, 54, 55};
static inline bool grid_mpc(int g) { return G.grid_layout[g] == 1; }
static inline uint8_t grid_chan(int g) { return grid_mpc(g) ? MPC_PANEL_CH : (G.grid_chan[g] >= 1 && G.grid_chan[g] <= 16) ? G.grid_chan[g] : GRID_DEF_CHAN[g]; }
static inline int grid_base(int g) { return (G.grid_base[g] >= 1 && G.grid_base[g] <= 112) ? G.grid_base[g] : 36; }
static inline int grid_padnum(int r, int c) { return (3 - r) * 4 + c % 4; }          /* 0-15 = pad 1-16 */
static inline uint8_t grid_note(int g, int p) { return grid_mpc(g) ? MPC_PAD_CC[p] : (uint8_t)(grid_base(g) + p); }   /* MPC: the CC number */

/* Tracks are named by number only (T1-T16): what a note sounds like depends on where it's sent,
   so the firmware never guesses a drum name. Notes are shown as musical names (60 = C4). */
static const char *track_name(int t)
{
    static char nbuf[6];
    if (is_mel(t)) { nbuf[0] = 'M'; nbuf[1] = (char)('1' + t - DTRACKS); nbuf[2] = 0; return nbuf; }
    nbuf[0] = 'T';
    if (t + 1 >= 10) { nbuf[1] = '1'; nbuf[2] = (char)('0' + (t + 1) % 10); nbuf[3] = 0; }
    else { nbuf[1] = (char)('1' + t); nbuf[2] = 0; }
    return nbuf;
}

static const char *note_name(uint8_t n)
{
    static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char nbuf[6];
    const char *b = NAMES[n % 12];
    int k = 0, oct = n / 12 - 1;
    while (*b) nbuf[k++] = *b++;
    if (oct < 0) { nbuf[k++] = '-'; oct = -oct; }
    nbuf[k++] = (char)('0' + oct);
    nbuf[k] = 0;
    return nbuf;
}

/* step byte: bits 0-1 level, 2-3 probability (0 = 100 %, 1 = 75, 2 = 50, 3 = 25), 4-5 ratchet (0 = 1x .. 3 = 4x),
   6-7 gate (0 = 50 %, 1 = 75, 2 = 95, 3 = 25: zero stays the old half-step gate) */
static inline int step_prob(uint8_t b) { return b >> 2 & 3; }
static inline int step_ratchet(uint8_t b) { return (b >> 4 & 3) + 1; }
static const uint8_t PROB_PCT[4] = {100, 75, 50, 25};
static const uint8_t GATE_PCT[4] = {50, 75, 95, 25};
static const uint8_t GATE_RANK[4] = {1, 2, 3, 0};   /* code -> position short..long */
static const uint8_t GATE_CODE[4] = {3, 0, 1, 2};   /* position -> code */
static inline int step_gate(uint8_t b) { return GATE_PCT[b >> 6 & 3]; }

/* Chord shapes for knob 3 on page 2 (semitones above the step's note). Recording can store others. */
static const struct { const char *name; int8_t a, b; } CHORDS[] = {
    {"--", 0, 0}, {"oct", 12, 0}, {"5th", 7, 0}, {"maj", 4, 7}, {"min", 3, 7}, {"sus2", 2, 7}, {"sus4", 5, 7},
    {"dim", 3, 6}, {"aug", 4, 8}, {"maj7", 4, 11}, {"m7", 3, 10}, {"7", 4, 10}, {"5+oct", 7, 12},
};
#define NCHORDS ((int)(sizeof CHORDS / sizeof CHORDS[0]))
static int chord_index(int8_t a, int8_t b) { for (int i = 0; i < NCHORDS; i++) if (CHORDS[i].a == a && CHORDS[i].b == b) return i; return -1; }

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------- runtime state ---------- */

static int queued_pat = -1;                         /* pattern (or, in song mode, slot) to switch to at the bar */
#define SLOTS 16
static bool song_edit;                              /* song editor on the pads */
static int song_sel;                                /* slot selected in the editor */
static int song_slot, song_bar;                     /* playback position in the song */
static int view_top;                                /* first visible track */
static int page;                                    /* visible step page */
static bool playing;
static uint32_t gstep;                              /* steps played since the start; next step to play */
static bool has_played;                             /* false after a rewind: playheads hidden */
static uint32_t grid_us, step_start_us;
static int pending = -1;                            /* gstep whose (possibly swung) notes are still due */
static int clock_k = 6;
static bool ext_clock, ext_playing;
static uint32_t ext_last_ms, ext_ticks;
static char status_line[22] = "";
static int held_row = -1, held_row_t = -1;          /* row button held, and the track it was on when pressed */
static int held_pad = -1;                           /* the step pad being held (its pad number), and where it was: */
static int hold_view, hold_page, hold_pat;          /* if the view, page or pattern moves, the hold is dropped */
static bool hold_songed;
static int held_t = -1, held_s = -1;               /* step pad being held (step settings) */
static bool held_pad_turned, held_pad_was_lit;
static bool held_row_turned;
static uint32_t save_due;
static bool save_waiting_compact;
static int saving_kind = -1;                        /* record being written, -1 none */
static bool save_run;                               /* a save pass is in progress */
static bool dirty = true, dfu_requested;
static bool save_force;                             /* save even with AUTOSAVE off (SAVE NOW, or the setting itself) */
static bool save_forced_run;                        /* the running save was a forced one (a retry / re-arm stays forced) */
static uint32_t save_lazy_due;                      /* settings changed while stopped: saved when this is due (0 = none) */
static uint8_t saves_global;                        /* settings records written since boot (mod 128; host status 07) */

/* pad light shows + the BROWSER menu (state; the code is below, after the text helpers) */
static bool lt_on;                                  /* a show owns the pads */
static bool lt_manual;                              /* started by hand / by the host: not ended by PLAY, no idle rules */
static bool lt_host;                                /* the host drives it (mode HOST) */
static bool lt_by_host;                             /* the host started what is showing (02 01 / 04 / 05): its 02 00 ends it */
static bool lt_pinned;                              /* an effect / text the host asked for: CYCLE leaves it alone */
static float lt_t;                                  /* effect time */
static uint32_t lt_last_us, lt_input_ms, lt_host_ms, lt_cycle_ms;
static int lt_cur, lt_slice;                        /* effect showing; next column group of the frame being built */
static unsigned lt_frame;
static uint64_t lt_eat;                             /* pads whose press only woke the pads up: their release is ignored too */
static uint8_t lt_stage[LT_ROWS * LT_COLS][3];
static bool menu_on, menu_edit;
static int pop_kind;                                /* a big-number popup after a change: 1 tempo, 2 swing */
static uint32_t pop_ms;
static int menu_sel, menu_grp;                      /* the selected row, and which menu page is open (0 = the top) */
static char menu_toast[22];
static uint32_t menu_toast_ms;
static const uint16_t LT_IDLE_S[6] = {0, 15, 30, 60, 120, 300};   /* lt_idle index -> seconds, 0 = off */

/* live play */
enum { MODE_STEP, MODE_NOTE, MODE_DRUM, MODE_FADER };
static int mode = MODE_STEP;
#define LIVE_VEL 100
#define LIVE_ACCENT 127
#define KB_SPAN 31                                  /* NOTE mode: 16 columns + 3 rows a fourth apart */
static struct { uint8_t note, chan; bool keys, on, sx, cc; } live[64];   /* what each pad's press sent, so its release matches (sx = a surface SysEx, cc = an MPC panel CC, not a note) */
/* SURFACE (v0.19): the ORC draws its own controls. Pads show the colours the host set (0A); a held pad shows brighter.
   The host turns it on (0B 01 <x>); any mode button (STEP / NOTE / DRUM / PERFORM, SHIFT + PATTERN) or 0B 00 turns it
   off. The sequencer keeps running underneath. Not saved.
   v0.21 (the owner: "they recognise each other, they can just talk however"): a surface pad is NOT a MIDI note any more,
   so it takes no channel from the sequencer, the ORC or the gear: press / release = F0 7D 'ORC' 0C <pad 0-63> <vel> F7
   (release vel 0). 0B's second byte (once the channel) is still accepted and ignored. */
static bool surface_on;
static uint8_t surf_ch;                             /* 0-15: what 0B's second byte said (reported by 07; unused since v0.21) */
static bool surf_known;                             /* the host has set up a surface since boot (BROWSER > ORC surface can bring it back) */
static uint8_t surf_rgb[64][3];                     /* levels 0-7, as the host sent them */
static uint8_t notes_held[128];                     /* NOTE mode: pads holding each note (a note can sit on 2 pads) */
static int grid_sel;                                /* DRUM mode: grid being edited (ALT + tap a grid) */
static uint8_t m_cur[MTRACKS] = {60, 60, 60, 60, 60, 60, 60, 60};   /* the note a newly lit melodic step gets */
static uint32_t flash_ms[TRACKS];                   /* DRUM mode: when the sequencer last played each track */
static bool rec_arm;
static int32_t rec_skip[TRACKS];                    /* gstep + 1 whose note this track skips (just recorded early) */
static uint32_t ext_step_at, ext_even_at, ext_step_len;   /* external clock: last step's time, last even step's, step length (us) */
#define MAXFADERS 32
static int fad_top;                                 /* first visible fader */
static int mode_before_faders = MODE_STEP;          /* where a tap on PERFORM goes back to */
static uint32_t perform_down_ms;                    /* a short press of PERFORM = faders, a hold = FILL */
static const uint8_t FAD_RGB[16][3] = {             /* fader colours, R,G,B brightness 0-7 */
    {7, 0, 0}, {7, 2, 0}, {7, 4, 0}, {6, 6, 0}, {3, 7, 0}, {0, 7, 0}, {0, 7, 3}, {0, 6, 5},
    {0, 5, 7}, {0, 3, 7}, {0, 0, 7}, {3, 0, 7}, {5, 0, 7}, {7, 0, 6}, {7, 2, 4}, {6, 6, 6},
};
static const char *const FAD_COL_NAME[16] = {"red", "orange", "amber", "yellow", "lime", "green", "mint", "teal",
    "cyan", "sky", "blue", "indigo", "purple", "magenta", "pink", "white"};
static int knob_page;
static int pat_btn = -1;                            /* PATTERN up/down being held for the picker (its input), -1 none */
static bool picker_used;                            /* a pattern was tapped / recoloured during this hold */
static uint32_t pat_down_ms;                        /* when it was pressed: the picker shows after 0.2 s */
static int picker_sel;                              /* the pattern knob 1 recolours */                               /* step knobs: 0 = ratchet/chance/note/gate, 1 = condition/lock/chord */
static held_notes_t held_notes[TRACKS];             /* notes a TIE / SLIDE step is holding (play.c) */
static int rec_chord_t = -1; static uint32_t rec_chord_g;   /* NOTE-mode recording: notes on the same step form a chord */
static bool ext_odd_due;                            /* external clock: the swung (odd) step of this pair hasn't played */
static uint32_t held_since[TRACKS];                 /* step a TIE / SLIDE hold began (a hold lasts at most one loop) */

/* A save reads the live data, so an edit to the record being written cancels it (it never counts);
   the save runs again 3 s after the last edit. */
static void save_soon(void) { save_due = ms_ticks + 3000; if (!save_due) save_due = 1; }
/* A pattern save reads the live steps, so an edit to that pattern cancels it (it never counts) and
   queues it again. (The settings record is written from a snapshot, so nothing cancels it.) */
static void cancel_save_of(int kind)
{
    if (store2_busy_kind() != kind) return;
    store2_cancel(); saving_kind = -1; save_run = false;
    if (kind >= KIND_PAT(0)) pat_dirty |= (uint32_t)(1u << (kind - KIND_PAT(0)));
    if (save_forced_run) save_force = true;         /* a SAVE NOW that was cut short still saves with AUTOSAVE off */
    if (!save_due) save_soon();
}
static void edited_pattern(int p) { cancel_save_of(KIND_PAT(p)); pat_dirty |= (uint32_t)(1u << p); save_soon(); }
static void edited_global(void) { global_dirty = true; save_soon(); }
/* Moving around (pattern choice, mutes, octave, song on/off ...) and live-play gestures (tempo, swing,
   clock out, recorded hits): remembered with the next save, but doesn't start one by itself while
   playing. STOP starts one if anything is waiting (a calm moment). While STOPPED nothing may come
   (a tempo set before pressing PLAY, a light show picked), so a change then is saved
   CTL_LAZY_SAVE_MS after the LAST such change. */
static void touched_lazy(void) { if (!playing && !ext_playing) save_lazy_due = ctl_lazy_arm(ms_ticks); }
static void touched_global(void) { global_dirty = true; touched_lazy(); }
/* Same for a pattern: a save of it that is mid-write reads live data, so it is cancelled (it never
   counts); the pattern then waits for the next save instead of re-arming the timer. */
static void touched_pattern(int p)
{
    if (store2_busy_kind() == KIND_PAT(p)) { store2_cancel(); saving_kind = -1; save_run = false; }
    pat_dirty |= (uint32_t)(1u << p);
    touched_lazy();
}
/* Anything waiting: save it now-ish (STOP, and STOP while lights run, which shows the next show instead). */
static void commit_pending(void)
{
    save_lazy_due = 0;
    if ((global_dirty || (pat_dirty & ~pat_stuck)) && !save_due && !save_run) save_soon();
}
static void pool_full(void) { memcpy(status_line, "full", 5); dirty = true; }   /* the step pool is out of room */

/* Pattern colours: the fader palette; an empty pattern shows uncoloured (dim grey). */
static int pat_col_idx(int p) { return G.pat_col[p] ? (G.pat_col[p] - 1) & 15 : p % 16; }
static bool pat_empty(int p) { return steps_pattern_size(p) == 0; }
static void pat_rgb(int p, int k, int *R, int *Gn, int *B)   /* brightness k 1-7; empty = grey */
{
    if (pat_empty(p)) { *R = *Gn = *B = k > 3 ? 3 : 1; return; }
    const uint8_t *rgb = FAD_RGB[pat_col_idx(p)];
    *R = (rgb[0] * k + 6) / 7; *Gn = (rgb[1] * k + 6) / 7; *B = (rgb[2] * k + 6) / 7;
}
static void pat_recolour(int p, int d)
{
    if (p < 0 || p >= PATTERNS) return;
    int k = pat_col_idx(p) + d;
    G.pat_col[p] = (uint8_t)((k < 0 ? 0 : k > 15 ? 15 : k) + 1);
    edited_global();
    dirty = true;
}
static bool picker_on(void) { return pat_btn >= 0 && fire_held(pat_btn) && ms_ticks - pat_down_ms >= 200; }

#define NOFFS 128
#define NRATQ 128
static struct { uint8_t note, chan; uint32_t at_us; bool live; } offs[NOFFS];
static struct { uint8_t note, chan, vel; uint32_t at_us, gate_us; bool live; } ratq[NRATQ];   /* later ratchet hits */

static void add_off(uint8_t note, uint8_t ch, uint32_t at)
{
    for (int i = 0; i < NOFFS; i++)
        if (!offs[i].live) { offs[i].note = note; offs[i].chan = ch; offs[i].at_us = at; offs[i].live = true; return; }
}

static void midi_send3(uint8_t a, uint8_t b, uint8_t c)
{
    if (!tud_midi_mounted()) return;
    uint8_t m[3] = {a, b, c};
    tud_midi_stream_write(0, m, 3);
}
static void midi_send1(uint8_t a) { if (tud_midi_mounted()) tud_midi_stream_write(0, &a, 1); }
/* v0.21: a surface pad press (vel 1-127) / release (0), as SysEx */
static void surf_send(int pad, uint8_t vel)
{
    if (!tud_midi_mounted()) return;
    uint8_t m[9] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x0C, (uint8_t)(pad & 0x3F), (uint8_t)(vel & 0x7F), 0xF7};
    tud_midi_stream_write(0, m, sizeof m);
}

static uint32_t step_us(void) { return 60000000u / (uint32_t)(BPM * 4); }
static uint32_t swing_us(void) { return (uint32_t)((G.swing - 50) * 2) * step_us() / 100; }
static inline int track_pos(int t, uint32_t g) { return (int)(g % (uint32_t)tlen(CURPAT, t)); }
/* Step length and swing delay as they really are: the host's when following external clock. */
static int ext_late(void) { return (G.swing - 50) * 6 / 50; }   /* swung step: this many clock ticks late */
static uint32_t cur_step_us(void) { return (ext_playing && ext_step_len) ? ext_step_len : step_us(); }
static uint32_t cur_swing_us(void) { return (ext_playing && ext_step_len) ? ext_step_len * (uint32_t)ext_late() / 6 : swing_us(); }

/* ---------- song ---------- */

static int song_len(void) { int n = 0; while (n < SLOTS && G.song_pat[n]) n++; return n; }
static int slot_reps(int i) { return G.song_rep[i] ? G.song_rep[i] : 1; }
static bool song_active(void) { return G.song_on && song_len() > 0; }

static void song_goto(int slot)
{
    int n = song_len();
    if (!n) return;
    song_slot = ((slot % n) + n) % n;
    song_bar = 0;
    CURPAT = (uint8_t)(G.song_pat[song_slot] - 1);
    touched_global();
}

/* Called on every 16-step bar boundary while playing (not on the very first step). */
static void song_bar_end(void)
{
    if (queued_pat >= 0) { song_goto(queued_pat); queued_pat = -1; return; }   /* PATTERN up/down in song mode = jump */
    if (++song_bar >= slot_reps(song_slot)) song_goto(song_slot + 1);
}

/* A step's notes are collected and handed to USB in ONE write, so they leave in one packet.
   (One write per note let the first go out alone and the rest a USB frame later, 2-3 ms.) */
static uint8_t batch[768];                          /* up to 256 messages: 24 tracks x (lock + 3 notes + ends) */
static uint32_t batch_n;
static void batch_add(uint8_t a, uint8_t b, uint8_t c) { if (batch_n + 3 <= sizeof batch) { batch[batch_n++] = a; batch[batch_n++] = b; batch[batch_n++] = c; } }
static void batch_flush(void) { if (batch_n && tud_midi_mounted()) tud_midi_stream_write(0, batch, batch_n); batch_n = 0; }

static void seq_note_on(uint8_t ch, uint8_t note, uint8_t vel)
{
    for (int i = 0; i < NOFFS; i++)
        if (offs[i].live && offs[i].note == note && offs[i].chan == ch) { batch_add((uint8_t)(0x80 | ch), note, 0); offs[i].live = false; }
    batch_add((uint8_t)(0x90 | ch), note, vel);
}

static void out_cc(uint8_t ch, uint8_t cc, uint8_t v) { batch_add((uint8_t)(0xB0 | ch), cc, v); }
static void out_off(uint8_t ch, uint8_t n) { batch_add((uint8_t)(0x80 | ch), n, 0); }
static const play_out_t OUT = {out_cc, seq_note_on, out_off};

static void play_step(uint32_t g, uint32_t now)
{
    int was_pat = CURPAT;
    if (g % PAGE == 0 && g > 0 && song_active()) song_bar_end();
    else if (g % PAGE == 0 && queued_pat >= 0) { CURPAT = (uint8_t)queued_pat; queued_pat = -1; touched_global(); }
    if (CURPAT != was_pat)                          /* a new pattern: nothing carries over from a TIE / SLIDE */
        for (int t = 0; t < TRACKS; t++) if (held_notes[t].n) play_release(&held_notes[t], &OUT);
    /* how long this step really lasts until the next one sounds (a swung step is shorter) */
    uint32_t span = cur_step_us() - ((g & 1) ? cur_swing_us() : 0);
    for (int t = 0; t < TRACKS; t++) {
        if (muted(t)) { if (held_notes[t].n) play_release(&held_notes[t], &OUT); continue; }
        if (held_notes[t].n && g - held_since[t] > (uint32_t)tlen(CURPAT, t))
            play_release(&held_notes[t], &OUT);     /* a hold that found no next note within a loop ends */
        step_t v;
        if (!steps_read(CURPAT, t, track_pos(t, g), &v)) continue;
        int lv = v.b & 3;
        if (rec_skip[t] == (int32_t)g + 1) { rec_skip[t] = 0; continue; }   /* hit live a moment ago */
        if (!cond_ok(cond_of(v.cond), g / (uint32_t)tlen(CURPAT, t), fire_held(IN_PERFORM))) continue;   /* condition said no */
        if (step_prob(v.b) && rnd() % 100 >= PROB_PCT[step_prob(v.b)]) continue;   /* chance said no */
        uint8_t ch = (uint8_t)(tchan(t) - 1), notes[3];
        int nn = play_step_notes(&v, is_mel(t), is_mel(t) ? 0 : tnote(t), ch, G.lock_cc[t], LEVEL_VEL[lv], &held_notes[t], &OUT, notes);
        flash_ms[t] = ms_ticks ? ms_ticks : 1;
        lights_hit(t, lv);                          /* the TRACKS light show jumps with the hit */
        if (!nn) { held_since[t] = g; continue; }   /* TIE / SLIDE: held until this track's next note */
        int nr = step_ratchet(v.b);
        uint32_t sub = span / (uint32_t)nr, gate = sub * (uint32_t)step_gate(v.b) / 100;
        for (int i = 0; i < nn; i++) {
            add_off(notes[i], ch, now + gate);
            for (int k = 1; k < nr; k++)            /* ratchets: the remaining hits, evenly spaced in the step */
                for (int q = 0; q < NRATQ; q++)
                    if (!ratq[q].live) {
                        ratq[q].note = notes[i]; ratq[q].chan = ch; ratq[q].vel = LEVEL_VEL[lv];
                        ratq[q].at_us = now + (uint32_t)k * sub; ratq[q].gate_us = gate; ratq[q].live = true;
                        break;
                    }
        }
    }
    batch_flush();
    has_played = true;
    dirty = true;
}

static void all_notes_off(void)
{
    for (int i = 0; i < NOFFS; i++)
        if (offs[i].live) { batch_add((uint8_t)(0x80 | offs[i].chan), offs[i].note, 0); offs[i].live = false; }
    for (int i = 0; i < NRATQ; i++) ratq[i].live = false;
    for (int t = 0; t < TRACKS; t++) if (held_notes[t].n) play_release(&held_notes[t], &OUT);
    for (int t = 0; t < TRACKS; t++) rec_skip[t] = 0;
    batch_flush();
}

static void stop(bool rewind)
{
    if (playing && G.clock_out) midi_send1(0xFC);
    playing = false;
    ext_playing = false;
    all_notes_off();
    pending = -1;
    rec_chord_t = -1;
    if (rewind) { gstep = 0; has_played = false; }
    commit_pending();                               /* live-play changes waiting */
    dirty = true;
}

static void start(uint32_t now)
{
    if (G.clock_out) midi_send1(gstep == 0 ? 0xFA : 0xFB);
    if (gstep == 0 && song_active()) song_goto(0);  /* after a rewind the song starts from the top */
    playing = true;
    grid_us = now;
    pending = -1;
    rec_chord_t = -1;
    clock_k = 6;
    dirty = true;
}

/* ---------- LEDs ---------- */

static void paint_song_editor(void)
{
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int R = 0, Gn = 0, B = 0;
            if (r == 0) {                           /* slots: coloured by their pattern, selected = white */
                int pt = G.song_pat[c];
                if (c == song_sel) R = Gn = B = 7;
                else if (pt) pat_rgb(pt - 1, 5, &R, &Gn, &B);
                else if (c == song_len()) R = Gn = B = 1;       /* where the next slot would go */
                if (song_active() && (playing || ext_playing) && c == song_slot && c != song_sel && ((ms_ticks / 200) & 1)) R = Gn = B = 7;
            } else if (r == 1 || r == 2) {          /* patterns 1-16 / 17-32 for the selected slot */
                int p = (r - 1) * 16 + c;
                pat_rgb(p, G.song_pat[song_sel] == p + 1 ? 7 : 2, &R, &Gn, &B);
            } else if (c < 8) {                     /* bottom row: bars 1-8 */
                int reps = G.song_pat[song_sel] ? slot_reps(song_sel) : 0;
                Gn = (c < reps) ? (c == reps - 1 ? 7 : 3) : 1;
            }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

/* NOTE mode: rows a fourth apart, bottom row lowest. */
static int kb_note(int r, int c) { return kb_base() + (VIEW_ROWS - 1 - r) * 5 + c; }

static void paint_keys(void)
{
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int n = kb_note(r, c), pc = n % 12, R = 0, Gn = 0, B = 0;
            if (notes_held[n]) Gn = 7;
            else if (pc == 0) { Gn = 3; B = 7; }    /* C */
            else if (pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10) R = Gn = B = 1;   /* white keys */
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

/* DRUM mode: four 4x4 grids, one colour each; pad 1 (bottom-left) a little brighter. */
static const uint8_t GRID_RGB[4][3] = {{7, 0, 0}, {0, 7, 0}, {0, 3, 7}, {6, 0, 6}};

static void paint_drums(void)
{
    bool editing = fire_held(IN_ALT);
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int g = c / 4, p = grid_padnum(r, c), R, Gn, B;
            uint8_t note = grid_note(g, p), ch = grid_chan(g);
            const uint8_t *rgb = GRID_RGB[g];
            int k = p == 0 ? 3 : 2;
            if (editing && g != grid_sel) k = 1;     /* ALT held: the grid being edited stands out */
            for (int t = 0; t < DTRACKS; t++)       /* the sequencer just played this pad's note */
                if (!grid_mpc(g) && flash_ms[t] && ms_ticks - flash_ms[t] < 90 && tnote(t) == note && tchan(t) == ch) { k = 7; break; }
            R = (rgb[0] * k + 6) / 7; Gn = (rgb[1] * k + 6) / 7; B = (rgb[2] * k + 6) / 7;
            if (live[r * 16 + c].on) R = Gn = B = 7;
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

/* FADERS: each row a fader in its colour, lit up to its level, dim beyond. The row after the last
   fader (if there's room) shows a dim mark: its row button adds a fader. */
/* beat / bar mark colours: which channels light (at level 1, or 2 for "both"'s bar mark) */
#define MARK_N 7
static const uint8_t MARK_RGB[MARK_N][3] = {{0, 0, 1}, {1, 1, 1}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}};
static const char *const NM_MARKCOL[MARK_N] = {"blue", "white", "red", "green", "yellow", "purple", "cyan"};

static void paint_surface(void)                     /* the host's colours; a held pad brighter (or dim white if it has none) */
{
    for (int pad = 0; pad < 64; pad++) {
        int R = surf_rgb[pad][0], Gn = surf_rgb[pad][1], B = surf_rgb[pad][2];
        if (live[pad].on) {
            int mx = R > Gn ? (R > B ? R : B) : (Gn > B ? Gn : B);
            if (!mx) R = Gn = B = 3;
            else { R = R * 7 / mx; Gn = Gn * 7 / mx; B = B * 7 / mx; }
        }
        int r = pad / 16, c = pad % 16;
        led_set(led_pad(r, c, 0), R); led_set(led_pad(r, c, 1), Gn); led_set(led_pad(r, c, 2), B);
    }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

static void paint_faders(void)
{
    for (int r = 0; r < VIEW_ROWS; r++) {
        int f = fad_top + r;
        for (int c = 0; c < PAGE; c++) {
            int R = 0, Gn = 0, B = 0;
            if (f < G.fad_n) {
                const uint8_t *rgb = FAD_RGB[G.fad_col[f] & 15];
                int lit = (G.fad_val[f] * PAGE + 63) / 127;
                if (c < lit) { R = rgb[0]; Gn = rgb[1]; B = rgb[2]; }
                else { R = rgb[0] ? 1 : 0; Gn = rgb[1] ? 1 : 0; B = rgb[2] ? 1 : 0; }
            } else if (f == G.fad_n && c == 0) R = Gn = B = 1;   /* "+": add a fader here */
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
        led_set(LED_ROW_G[r], held_row == r ? 7 : (f < G.fad_n ? 3 : f == G.fad_n ? 1 : 0));
        led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0);
    }
}

/* Hold PATTERN: patterns 1-16 on the top row, 17-32 on the second. Bright = the current one (or, in
   song mode, the one picked for recolouring), blinking = queued, grey = empty. */
static void paint_picker(void)
{
    int cur = song_active() ? picker_sel : CURPAT;
    bool blink = (ms_ticks / 150) & 1;
    for (int r = 0; r < VIEW_ROWS; r++)
        for (int c = 0; c < PAGE; c++) {
            int R = 0, Gn = 0, B = 0;
            if (r < 2) {
                int p = r * 16 + c;
                int k = p == cur ? 7 : (!song_active() && p == queued_pat) ? (blink ? 7 : 2) : 2;
                pat_rgb(p, k, &R, &Gn, &B);
            }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
    for (int r = 0; r < VIEW_ROWS; r++) { led_set(LED_ROW_G[r], 0); led_set(LED_ROWSIDE_R[r], 0); led_set(LED_ROWSIDE_G[r], 0); }
}

static void __attribute__((noinline)) paint(void)
{
    bool run = playing || ext_playing;
    if (surface_on) paint_surface();
    else if (picker_on()) paint_picker();
    else if (mode == MODE_FADER) paint_faders();
    else if (mode == MODE_NOTE) paint_keys();
    else if (mode == MODE_DRUM) paint_drums();
    else if (song_edit) paint_song_editor(); else {
    uint32_t last = gstep ? gstep - 1 : 0;          /* the step that most recently played */
    for (int r = 0; r < VIEW_ROWS; r++) {
        int t = view_top + r;
        int len = tlen(CURPAT, t), head = track_pos(t, last);
        const uint8_t *rgb = TRACK_RGB[t % 8];
        for (int c = 0; c < PAGE; c++) {
            int s = page * PAGE + c, R = 0, Gn = 0, B = 0;
            if (s < len) {
                int lv = level(CURPAT, t, s);
                bool is_head = has_played && run && s == head;
                if (lv != LV_OFF) {
                    int k = LEVEL_BRIGHT[lv];
                    step_t sv; steps_read(CURPAT, t, s, &sv);
                    uint8_t sb = sv.b;
                    bool special = step_prob(sb) || step_ratchet(sb) > 1 || (sb >> 6) || sv.cond || sv.lock || sv.ch2 || sv.ch3;
                    if (special && ((ms_ticks / 300) & 1)) k = k > 2 ? k - 2 : 1;   /* gentle pulse: the step has settings */
                    R = (rgb[0] * k + 6) / 7; Gn = (rgb[1] * k + 6) / 7; B = (rgb[2] * k + 6) / 7;
                    if (muted(t)) { R = R ? 1 : 0; Gn = Gn ? 1 : 0; B = B ? 1 : 0; }
                    if (is_head) R = Gn = B = 7;
                } else if (is_head) {
                    R = Gn = B = 2;
                } else if (!G.mark_off && (G.mark_space == 1 ? s % 16 == 0 : s % 4 == 0)) {
                    /* beat / bar marks (BROWSER > Display): level 1 = the LED driver's lowest; dim blue by default (white =
                       3 channels at level 1 was too bright, the owner 2026-10-14). "both": the bar mark at level 2. */
                    const uint8_t *mc = MARK_RGB[G.mark_col % MARK_N];
                    int ml = (G.mark_space == 2 && s % 16 == 0) ? 2 : 1;
                    R = mc[0] * ml; Gn = mc[1] * ml; B = mc[2] * ml;
                }
            }
            led_set(led_pad(r, c, 0), R);
            led_set(led_pad(r, c, 1), Gn);
            led_set(led_pad(r, c, 2), B);
        }
        led_set(LED_ROW_G[r], held_row == r ? 7 : (muted(t) ? 0 : 3));
        led_set(LED_ROWSIDE_R[r], muted(t) ? 7 : 0);
        led_set(LED_ROWSIDE_G[r], 0);
    }
    }
    led_set(LED_PATSONG_G, G.song_on ? 7 : 0);
    led_set(LED_PATSONG_Y, song_edit ? (((ms_ticks / 300) & 1) ? 7 : 2) : 0);
    led_set(LED_PLAY_G, run ? 7 : 0);
    led_set(LED_PLAY_Y, run ? 0 : 2);
    led_set(LED_STOP_Y, run ? 2 : 0);
    led_set(LED_SHIFT_Y, fire_held(IN_SHIFT) ? 7 : 1);
    led_set(LED_ALT_Y, fire_held(IN_ALT) ? 7 : 1);
    led_set(LED_STEP_Y, mode == MODE_STEP ? 7 : 1);
    led_set(LED_NOTE_Y, mode == MODE_NOTE ? 7 : 1);
    led_set(LED_DRUM_Y, mode == MODE_DRUM ? 7 : 1);
    led_set(LED_REC_Y, rec_arm ? 7 : 0);
    if (held_t >= 0 && held_row < 0) {             /* a step held: which page its knobs are on (CHANNEL = 1, MIXER = 2) */
        led_set(LED_CHANNEL, knob_page == 0 ? 5 : 1); led_set(LED_MIXER, knob_page == 1 ? 5 : 1);
        led_set(LED_USER1, 0); led_set(LED_USER2, 0);
    } else {                                        /* otherwise: the free-knob bank, 1-4 */
        int kb = G.knob_bank & 3;
        led_set(LED_CHANNEL, kb == 0 ? 5 : 1); led_set(LED_MIXER, kb == 1 ? 5 : 1);
        led_set(LED_USER1, kb == 2 ? 5 : 1); led_set(LED_USER2, kb == 3 ? 5 : 1);
    }
    led_set(LED_PERFORM_Y, mode == MODE_FADER ? 7 : 1);   /* faders mode */
    led_set(LED_PERFORM_R, fire_held(IN_PERFORM) ? 7 : 0);  /* FILL while held */
    if (mode == MODE_NOTE) {                        /* GRID = octave */
        led_set(LED_GRID_L, kb_base() > 1 ? 2 : 0);
        led_set(LED_GRID_R, kb_base() < 96 ? 2 : 0);
    } else {
        led_set(LED_GRID_L, mode == MODE_STEP && page > 0 ? 2 : 0);
        led_set(LED_GRID_R, mode == MODE_STEP && page < PAGES - 1 ? 2 : 0);
    }
    bool blink = (ms_ticks / 150) & 1;
    led_set(LED_PAT_UP, queued_pat >= 0 && queued_pat > CURPAT ? (blink ? 7 : 0) : 1);
    led_set(LED_PAT_DN, queued_pat >= 0 && queued_pat < CURPAT ? (blink ? 7 : 0) : 1);
    led_set(LED_BROWSER, menu_on ? 7 : lt_on ? 3 : 1);   /* the menu button: lit while the menu is open */
}

/* ---------- screen ---------- */

static void fmt_int(char *o, int v) { char t[12]; int n = 0; if (v < 0) { *o++ = '-'; v = -v; } do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *o++ = t[--n]; *o = 0; }
static char *cat(char *o, const char *str) { while (*str) *o++ = *str++; *o = 0; return o; }
static char *cat_int(char *o, int v) { fmt_int(o, v); return o + strlen(o); }

/* ---------- light shows + the BROWSER menu ----------
   Lights: the pads show an effect instead of the steps. They start by hand (menu: Lights now), when
   the host asks, or on their own after the idle time with the sequencer stopped. The first touch of
   any pad or button only ends them (it is not also an edit). While they run: SELECT = next / previous
   show, knob 1 = speed, STOP (stopped) = next show, BROWSER = the menu.
   A frame is drawn 2 columns at a time, only when the next step is not about to be due (the rule a
   flash write follows), and committed to the LEDs when all 16 columns are done. */

static void saver_start(void);                      /* (save / load section, below) */
#define LT_SLICES 8                                 /* a frame is drawn in pieces of this many columns each (16 / 8 = 2) */
#define LT_SLICE_COLS (LT_COLS / LT_SLICES)

static float lt_speed_f(void) { return lights_speed_factor(G.lt_speed); }

/* The Show setting (G.lt_mode): 0-12 = that effect (12 = TRACKS), 13 = CYCLE (it stays 13: saved settings already say so),
   14 = PI VU bars, 15 = the scrolling TEXT. The menu and SELECT walk them in SHOW_ORDER (CYCLE last): ctl.c. */

static void lights_defaults(void)
{
    G.lt_init = 1; G.lt_mode = 1; G.lt_speed = 64; G.lt_idle = 0; G.lt_smooth = 1;   /* SPARKLE, x1, idle start OFF (v0.18), smooth */
    G.lt_idle_v18 = 1;
}

/* Make the picked show the running one. idle: an idle start never waits for a host that is not there. */
static void lights_apply_show(bool idle)
{
    int v = G.lt_mode;
    if (v == 11) v = 1;                             /* (the removed fake VU: never run) */
    if (idle && v == SHOW_HOST) v = 1;
    lt_host = v == SHOW_HOST;
    if (v == SHOW_HOST) { lt_cur = LT_M_HOST; lt_host_ms = ms_ticks; }          /* the pads return after 3 s with no levels */
    else if (v == SHOW_TEXT) { lt_cur = LT_M_TEXT; lights_set_text(lights_text(), (int)strlen(lights_text())); }   /* from the right edge */
    else if (v == SHOW_CYCLE) { if (lt_cur >= LT_FX) lt_cur = 7; }       /* RIPPLE: not a twin of the show next to CYCLE in the list */
    else lt_cur = v;
    lt_cycle_ms = ms_ticks;
    lt_pinned = lt_by_host = false;                 /* the Show setting runs: no host effect is pinned over it */
}

static void lights_on(bool manual, bool host)
{
    lt_manual = manual;
    lights_apply_show(!manual);
    if (host) { lt_host = true; lt_cur = LT_M_HOST; lt_host_ms = ms_ticks; }    /* the host asked for its bars */
    lights_clear_bars();                            /* bars start empty (hits counted while the pads were dark are old news) */
    lt_last_us = us_now(); lt_slice = 0;
    lt_on = true; led_pad_lock = 1;
    dirty = true;
}

static void lights_off(void)
{
    if (!lt_on) return;
    lt_on = false; lt_host = false; lt_by_host = lt_pinned = false; led_pad_lock = 0;
    lt_input_ms = ms_ticks;                         /* the idle time counts from now (a host "off" must not re-light them at once) */
    dirty = true;                                   /* the next paint puts the steps back */
}

static void lights_next(int d)                      /* SELECT / STOP: the next / previous show in the Show list */
{
    G.lt_mode = (uint8_t)ctl_show_step(G.lt_mode, d); touched_global();
    lights_apply_show(false);
    dirty = true;
}

static void lights_speed(int v)                     /* knob 1 while lights run */
{
    int s = (int)G.lt_speed + v * 3;
    G.lt_speed = (uint8_t)(s < 0 ? 0 : s > 127 ? 127 : s);
    touched_global();
    dirty = true;
}

/* The running show was picked as PI VU (not an effect the host pinned, not a show the host started). */
static bool show_is_host(void) { return G.lt_mode == SHOW_HOST && !lt_pinned && !lt_by_host; }

static void lights_task(uint32_t now, bool quiet)
{
    if (!lt_on) return;
    switch (ctl_host_timeout(lt_on, lt_host, show_is_host(), ms_ticks, lt_host_ms)) {   /* the host went quiet */
    case HT_FALLBACK: lt_host = false; lt_cur = 1; lt_cycle_ms = ms_ticks; dirty = true; break;   /* SPARKLE until levels come back */
    case HT_OFF: lights_off(); return;
    default: break;
    }
    if (!quiet) return;
    if (lt_slice == 0) {
        if (!ctl_frame_due(now, lt_last_us)) return;    /* ~30 frames a second */
        float dt = (float)(now - lt_last_us) * 1e-6f;
        if (dt > 0.2f) dt = 0.2f;
        lt_last_us = now;
        lt_t += dt * ((float)BPM / 60.0f * 3.0f) * lights_scale(lt_cur) * lt_speed_f();   /* tempo-locked, like the ORC */
        if (lt_t > 40000.0f) lt_t = fmodf(lt_t, 6283.185f);
        lights_step(dt);
        if (ctl_cycle_due(G.lt_mode, lt_cur, lt_pinned, ms_ticks, lt_cycle_ms)) {   /* CYCLE (the 12 time-driven shows) */
            do lt_cur = (lt_cur + 1) % LT_FX; while (lt_cur == 11);   /* (11 = the removed fake VU) */
            lt_cycle_ms = ms_ticks; dirty = true;
        }
    }
    lights_cols(lt_cur, lt_t, lt_slice * LT_SLICE_COLS, (lt_slice + 1) * LT_SLICE_COLS, lt_stage);
    if (++lt_slice == LT_SLICES) {                  /* the frame is complete: show it */
        lt_slice = 0; lt_frame++;
        for (int r = 0; r < LT_ROWS; r++)
            for (int c = 0; c < LT_COLS; c++)
                for (int j = 0; j < 3; j++)
                    led_level[led_pad(r, c, j)] = (uint8_t)lights_level(lt_stage[r * LT_COLS + c][j], r, c, lt_frame, G.lt_smooth);
    }
}

/* The menu. Items edit G fields directly; SELECT scrolls, pushing it opens / confirms, BROWSER backs out. */
/* Free-knob banks (v0.20): with nothing held, KNOB MODE steps banks 1-4 and the knobs do what the bank says. Bank 1 out
   of the box = the old behaviour (knob 1 swing, knobs 2-4 relative CC 17 / 18 / 19 on ch 1); banks 2-4 = CC 20-31. */
#define KB_SWING 128
#define KB_TEMPO 129
static uint8_t kb_val[4][4] = {{64, 64, 64, 64}, {64, 64, 64, 64}, {64, 64, 64, 64}, {64, 64, 64, 64}};   /* absolute knobs' values */
static int kb_fn(int b, int k)
{
    int f = G.kb_fn[b & 3][k & 3];
    if (f >= 1 && f <= KB_TEMPO) return f;
    if (b == 0) return k == 0 ? KB_SWING : 16 + k;
    return 16 + 4 * b + k;
}
static int kb_chan(int b, int k) { int c = G.kb_ch[b & 3][k & 3]; return c >= 1 && c <= 16 ? c : 1; }

/* MIDI clock out on / off: SHIFT + PLAY and BROWSER > Playing (a running clock gets a STOP / CONTINUE so gear follows) */
static void clock_out_toggle(void)
{
    if (playing && G.clock_out) midi_send1(0xFC);
    G.clock_out = !G.clock_out;
    if (playing && G.clock_out) midi_send1(0xFB);
    cat(status_line, G.clock_out ? "clk out" : "clk off");
    dirty = true; touched_global();
}

enum { MK_ENUM, MK_EFFECT, MK_SPEED, MK_ACT, MK_SUB,
       MK_INFO,                                     /* a read-only line (the shortcut pages): the name is the whole text */
       MK_TEMPO, MK_SWING, MK_CLOCK,                /* Playing: the same values SHIFT + SELECT, knob 1, SHIFT + PLAY change */
       MK_KBANK, MK_KSEL, MK_KFN, MK_KCH, MK_KMODE };   /* Knobs: the bank, which knob, what it sends, channel, mode */
/* grp = the menu page the item is on (0 = the top); a MK_SUB item opens page n. */
typedef struct { const char *name; uint8_t grp, kind; uint8_t *v; uint8_t n; const char *const *names; void (*act)(void); } mitem_t;

static void menu_save_now(void);
static void menu_lights_now(void);
static void menu_surface_now(void);
static int menu_knob;                               /* Knobs page: the knob being set up (0-3) */
static const char *const NM_AUTOSAVE[2] = {"on", "off"};   /* autosave_off = 0 means ON */
static const char *const NM_OFFON[2] = {"off", "on"};
static const char *const NM_BARS[5] = {"snappy", "smooth", "whole", "heat", "heat smooth"};
static const char *const NM_IDLE[6] = {"off", "15s", "30s", "1m", "2m", "5m"};
static const char *const NM_MARKS[2] = {"on", "off"};   /* mark_off 0 = marks on */
static const char *const NM_SPACING[3] = {"every beat", "every bar", "beat+bar"};
static const char *const NM_KMODE[2] = {"relative", "absolute"};
static const mitem_t MENU[] = {
    /* the top page */
    {"Autosave",    0, MK_ENUM,   &G.autosave_off, 2, NM_AUTOSAVE, 0},
    {"Save now",    0, MK_ACT,    0, 0, 0, menu_save_now},
    {"ORC mode", 0, MK_ACT,    0, 0, 0, menu_surface_now},
    {"Playing",     0, MK_SUB,    0, 2, 0, 0},
    {"Knobs",       0, MK_SUB,    0, 3, 0, 0},
    {"Display",     0, MK_SUB,    0, 4, 0, 0},
    {"Lights",      0, MK_SUB,    0, 1, 0, 0},
    {"Shortcuts",   0, MK_SUB,    0, 5, 0, 0},
    /* page 1: lights */
    {"Start lights", 1, MK_ACT,   0, 0, 0, menu_lights_now},
    {"Show",        1, MK_EFFECT, &G.lt_mode, SHOW_VALUES, 0, 0},
    {"Speed",       1, MK_SPEED,  &G.lt_speed, 128, 0, 0},
    {"Idle start",  1, MK_ENUM,   &G.lt_idle, 6, NM_IDLE, 0},
    {"Smooth",      1, MK_ENUM,   &G.lt_smooth, 2, NM_OFFON, 0},
    {"Bar look",    1, MK_ENUM,   &G.lt_bars, 5, NM_BARS, 0},
    /* page 2: playing (the shortcuts still work) */
    {"Tempo",       2, MK_TEMPO,  0, 0, 0, 0},
    {"Swing",       2, MK_SWING,  0, 0, 0, 0},
    {"Clock out",   2, MK_CLOCK,  0, 0, 0, 0},
    /* page 3: knobs (the free knobs, nothing held; KNOB MODE steps the bank) */
    {"Bank",        3, MK_KBANK,  0, 0, 0, 0},
    {"Knob",        3, MK_KSEL,   0, 0, 0, 0},
    {"Sends",       3, MK_KFN,    0, 0, 0, 0},
    {"Channel",     3, MK_KCH,    0, 0, 0, 0},
    {"Mode",        3, MK_KMODE,  0, 0, 0, 0},
    /* page 4: display */
    {"Beat marks",  4, MK_ENUM,   &G.mark_off, 2, NM_MARKS, 0},
    {"Colour",      4, MK_ENUM,   &G.mark_col, MARK_N, NM_MARKCOL, 0},
    {"Spacing",     4, MK_ENUM,   &G.mark_space, 3, NM_SPACING, 0},
    /* page 5: shortcuts (each a page of read-only lines, <= 19 characters: the screen's width beside the scroll bar) */
    {"Steps",       5, MK_SUB,    0, 6, 0, 0},
    {"Hold a step", 5, MK_SUB,    0, 7, 0, 0},
    {"Hold a row",  5, MK_SUB,    0, 8, 0, 0},
    {"Patterns",    5, MK_SUB,    0, 9, 0, 0},
    {"Song editor", 5, MK_SUB,    0, 10, 0, 0},
    {"Playing",     5, MK_SUB,    0, 11, 0, 0},
    {"Note mode",   5, MK_SUB,    0, 12, 0, 0},
    {"Drum mode",   5, MK_SUB,    0, 13, 0, 0},
    {"Faders",      5, MK_SUB,    0, 14, 0, 0},
    {"Knobs",       5, MK_SUB,    0, 15, 0, 0},
    {"Press: step on/off", 6, MK_INFO, 0, 0, 0, 0}, {"SH+pad: accent", 6, MK_INFO, 0, 0, 0, 0},
    {"ALT+pad: length", 6, MK_INFO, 0, 0, 0, 0}, {"GRID: step page", 6, MK_INFO, 0, 0, 0, 0},
    {"SELECT: scroll rows", 6, MK_INFO, 0, 0, 0, 0}, {"SH+PAT: 4 rows", 6, MK_INFO, 0, 0, 0, 0},
    {"Row button: mute", 6, MK_INFO, 0, 0, 0, 0}, {"Tap PERFORM: faders", 6, MK_INFO, 0, 0, 0, 0},
    {"Hold PERFORM: fills", 6, MK_INFO, 0, 0, 0, 0},
    {"KNOB MODE: 1 <-> 2", 7, MK_INFO, 0, 0, 0, 0}, {"1A: ratchet", 7, MK_INFO, 0, 0, 0, 0},
    {"1B: chance", 7, MK_INFO, 0, 0, 0, 0}, {"1C: note", 7, MK_INFO, 0, 0, 0, 0},
    {"1D: gate 25-95%", 7, MK_INFO, 0, 0, 0, 0}, {"  then tie, slide", 7, MK_INFO, 0, 0, 0, 0},
    {"2A: condition", 7, MK_INFO, 0, 0, 0, 0}, {"  fill=hold PERFORM", 7, MK_INFO, 0, 0, 0, 0}, {"2B: lock value", 7, MK_INFO, 0, 0, 0, 0},
    {"2C: chord", 7, MK_INFO, 0, 0, 0, 0},
    {"1A: track channel", 8, MK_INFO, 0, 0, 0, 0}, {"1B: track note", 8, MK_INFO, 0, 0, 0, 0},
    {"1C: lock CC", 8, MK_INFO, 0, 0, 0, 0}, {"M row 1B: new note", 8, MK_INFO, 0, 0, 0, 0},
    {"PAT: next/previous", 9, MK_INFO, 0, 0, 0, 0}, {"Hold PAT: picker", 9, MK_INFO, 0, 0, 0, 0},
    {"PAT+1A: color", 9, MK_INFO, 0, 0, 0, 0}, {"ALT+PAT: copy", 9, MK_INFO, 0, 0, 0, 0},
    {"PAT/SONG: song mode", 9, MK_INFO, 0, 0, 0, 0}, {"SH+PAT/SONG: editor", 9, MK_INFO, 0, 0, 0, 0},
    {"Top row: slot", 10, MK_INFO, 0, 0, 0, 0}, {"Rows 2-3: pattern", 10, MK_INFO, 0, 0, 0, 0},
    {"Bottom row: bars", 10, MK_INFO, 0, 0, 0, 0}, {"1A: slot color", 10, MK_INFO, 0, 0, 0, 0},
    {"SH+slot: clear", 10, MK_INFO, 0, 0, 0, 0},
    {"PLAY: start/stop", 11, MK_INFO, 0, 0, 0, 0}, {"STOP: stop, rewind", 11, MK_INFO, 0, 0, 0, 0},
    {"SH+SELECT: tempo", 11, MK_INFO, 0, 0, 0, 0}, {"SH+ALT+SELECT: x10", 11, MK_INFO, 0, 0, 0, 0},
    {"SH+PLAY: clock out", 11, MK_INFO, 0, 0, 0, 0}, {"REC: record live", 11, MK_INFO, 0, 0, 0, 0},
    {"GRID: octave", 12, MK_INFO, 0, 0, 0, 0}, {"SH+GRID: semitone", 12, MK_INFO, 0, 0, 0, 0},
    {"SELECT: track M1-8", 12, MK_INFO, 0, 0, 0, 0}, {"ALT+SELECT: channel", 12, MK_INFO, 0, 0, 0, 0},
    {"SH+pad: accent", 12, MK_INFO, 0, 0, 0, 0}, {"REC: record notes", 12, MK_INFO, 0, 0, 0, 0},
    {"ALT+tap: pick group", 13, MK_INFO, 0, 0, 0, 0}, {"ALT+1A: channel", 13, MK_INFO, 0, 0, 0, 0},
    {"ALT+1B: first note", 13, MK_INFO, 0, 0, 0, 0}, {"ALT+1C: MPC pads", 13, MK_INFO, 0, 0, 0, 0},
    {"SH+pad: accent", 13, MK_INFO, 0, 0, 0, 0}, {"REC: record hits", 13, MK_INFO, 0, 0, 0, 0},
    {"Tap PERFORM: open", 14, MK_INFO, 0, 0, 0, 0}, {"Tap column: level", 14, MK_INFO, 0, 0, 0, 0},
    {"1A-1D: fine tune", 14, MK_INFO, 0, 0, 0, 0}, {"SELECT: scroll", 14, MK_INFO, 0, 0, 0, 0},
    {"Empty row btn: add", 14, MK_INFO, 0, 0, 0, 0}, {"SH+row: remove", 14, MK_INFO, 0, 0, 0, 0},
    {"Row+1A-C: CC/ch/col", 14, MK_INFO, 0, 0, 0, 0},
    {"KNOB MODE: bank 1-4", 15, MK_INFO, 0, 0, 0, 0}, {"  (nothing held)", 15, MK_INFO, 0, 0, 0, 0},
    {"Lit light: bank", 15, MK_INFO, 0, 0, 0, 0}, {"BROWSER>Knobs: edit", 15, MK_INFO, 0, 0, 0, 0},
};
#define MENU_ALL ((int)(sizeof MENU / sizeof MENU[0]))
#define MENU_PAGES 16
#define MENU_PAGE_MAX 16                            /* the most items one page may hold (draw_menu's arrays) */
static const char *const MENU_TITLE[MENU_PAGES] = {"Menu", "Lights", "Playing", "Knobs", "Display", "Shortcuts",
    "Steps", "Hold step", "Hold row", "Patterns", "Song edit", "Playing", "Note mode", "Drum mode", "Faders", "Knobs"};   /* <= 10 characters: room before "BRW=back" */
static const uint8_t MENU_PARENT[MENU_PAGES] = {0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};   /* where BROWSER goes back to */

static int menu_count(void)                         /* items on the open page */
{
    int n = 0;
    for (int k = 0; k < MENU_ALL; k++) if (MENU[k].grp == menu_grp) n++;
    return n;
}
static const mitem_t *menu_it(int i)                /* the i-th item of the open page */
{
    for (int k = 0; k < MENU_ALL; k++) if (MENU[k].grp == menu_grp && i-- == 0) return &MENU[k];
    return &MENU[0];
}

static void menu_toast_set(const char *s) { cat(menu_toast, s); menu_toast_ms = ms_ticks ? ms_ticks : 1; dirty = true; }

static void menu_save_now(void)
{
    bool any = global_dirty || (pat_dirty & ~pat_stuck);
    if (save_run || store2_busy()) menu_toast_set("busy, retry");
    else if (!any) menu_toast_set("nothing new");
    else { save_force = true; saver_start(); menu_toast_set("saving..."); }
}

static void menu_lights_now(void) { menu_on = menu_edit = false; menu_grp = 0; menu_sel = 0; lights_on(true, false); }
/* Back to the ORC's surface after a mode button left it (the owner: "how do I bring this up on the Fire?"). The colours
   and channel the ORC sent are still here; without them there is nothing to show. */
static void menu_surface_now(void)
{
    if (!surf_known) { menu_toast_set("no ORC surface yet"); return; }
    menu_on = menu_edit = false; menu_grp = 0; menu_sel = 0;
    surface_on = true; lights_off(); song_edit = false; held_t = held_s = -1; held_pad = -1;
    dirty = true;
}
static void menu_value(const mitem_t *m, char *o)
{
    *o = 0;
    switch (m->kind) {
    case MK_SUB: cat(o, ">"); break;
    case MK_ENUM: cat(o, m->names[*m->v < m->n ? *m->v : 0]); break;
    case MK_EFFECT: cat(o, ctl_show_name(*m->v)); break;
    case MK_SPEED: {
        int h = (int)(lt_speed_f() * 100.0f + 0.5f);
        o = cat(o, "x"); o = cat_int(o, h / 100); o = cat(o, "."); if (h % 100 < 10) o = cat(o, "0"); cat_int(o, h % 100);
        break;
    }
    case MK_TEMPO: o = cat_int(o, BPM); cat(o, " bpm"); break;
    case MK_SWING: o = cat_int(o, G.swing); cat(o, "%"); break;
    case MK_CLOCK: cat(o, G.clock_out ? "on" : "off"); break;
    case MK_KBANK: cat_int(o, (G.knob_bank & 3) + 1); break;
    case MK_KSEL: { char kn[3] = {(char)('1' + (G.knob_bank & 3)), (char)('A' + (menu_knob & 3)), 0}; cat(o, kn); break; }   /* bank + knob: 3C */
    case MK_KFN: {
        int f = kb_fn(G.knob_bank, menu_knob);
        if (f == KB_SWING) cat(o, "swing"); else if (f == KB_TEMPO) cat(o, "tempo"); else { o = cat(o, "CC "); cat_int(o, f); }
        break;
    }
    case MK_KCH: o = cat(o, "ch "); cat_int(o, kb_chan(G.knob_bank, menu_knob)); break;
    case MK_KMODE: cat(o, NM_KMODE[G.kb_abs[G.knob_bank & 3][menu_knob] ? 1 : 0]); break;
    default: break;
    }
}

static void menu_turn(int d)
{
    if (!menu_edit) {
        menu_sel += d > 0 ? 1 : -1;
        if (menu_sel < 0) menu_sel = 0;
        if (menu_sel > menu_count() - 1) menu_sel = menu_count() - 1;
        return;
    }
    const mitem_t *m = menu_it(menu_sel);
    int b = G.knob_bank & 3, big = fire_held(IN_SHIFT) ? 10 : 1;   /* SHIFT = steps of 10 (tempo, CC numbers) */
    switch (m->kind) {
    case MK_TEMPO: { int t = (int)BPM + (d > 0 ? big : -big); BPM = (uint16_t)(t < 40 ? 40 : t > 300 ? 300 : t); touched_global(); return; }
    case MK_SWING: { int s = G.swing + (d > 0 ? 1 : -1); G.swing = (uint8_t)(s < 50 ? 50 : s > 75 ? 75 : s); touched_global(); return; }
    case MK_CLOCK: if ((d > 0) != (G.clock_out != 0)) clock_out_toggle(); return;
    case MK_KBANK: { int nb = b + (d > 0 ? 1 : -1); G.knob_bank = (uint8_t)(nb < 0 ? 0 : nb > 3 ? 3 : nb); touched_global(); return; }
    case MK_KSEL: { int k = menu_knob + (d > 0 ? 1 : -1); menu_knob = k < 0 ? 0 : k > 3 ? 3 : k; return; }
    case MK_KFN: {                                  /* the list: swing, tempo, CC 1 .. CC 127 */
        int f = kb_fn(b, menu_knob), i = f == KB_SWING ? 0 : f == KB_TEMPO ? 1 : f + 1;
        i += d > 0 ? (i >= 2 ? big : 1) : (i > 2 ? -big : -1);
        i = i < 0 ? 0 : i > 128 ? 128 : i;
        G.kb_fn[b][menu_knob] = (uint8_t)(i == 0 ? KB_SWING : i == 1 ? KB_TEMPO : i - 1);
        edited_global(); return;
    }
    case MK_KCH: { int c = kb_chan(b, menu_knob) + (d > 0 ? 1 : -1); G.kb_ch[b][menu_knob] = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c); edited_global(); return; }
    case MK_KMODE: G.kb_abs[b][menu_knob] = d > 0 ? 1 : 0; edited_global(); return;
    default: break;
    }
    if (m->kind == MK_EFFECT) {                     /* the Show list, in its own order (a running show switches at once) */
        *m->v = (uint8_t)ctl_show_clamp(*m->v, d); edited_global();
        if (lt_on) lights_apply_show(false);
        return;
    }
    int step = m->kind == MK_SPEED ? (fire_held(IN_SHIFT) ? 1 : 4) : 1;
    int v = (int)*m->v + (d > 0 ? step : -step);
    if (v < 0) v = 0;
    if (v > (int)m->n - 1) v = (int)m->n - 1;
    *m->v = (uint8_t)v;
    lights_set_bar_style(G.lt_bars);                /* (cheap: only the bar look is affected, and only if it changed) */
    if (m->v == &G.autosave_off) { save_force = true; edited_global(); }   /* the setting itself always saves */
    else edited_global();
}

static void menu_push(void)                         /* SELECT pushed */
{
    const mitem_t *m = menu_it(menu_sel);
    if (m->kind == MK_ACT) m->act();
    else if (m->kind == MK_SUB) { menu_grp = m->n; menu_sel = 0; menu_edit = false; }
    else if (m->kind == MK_INFO) return;            /* a shortcut line: nothing to change */
    else menu_edit = !menu_edit;
}

static void menu_back(void)                         /* BROWSER: finish an edit, else up a page, else close */
{
    if (menu_edit) menu_edit = false;
    else if (menu_grp) {
        int from = menu_grp;
        menu_grp = MENU_PARENT[from < MENU_PAGES ? from : 0]; menu_sel = 0;
        for (int i = 0; i < menu_count(); i++) { const mitem_t *m = menu_it(i); if (m->kind == MK_SUB && m->n == from) menu_sel = i; }
    } else menu_on = false;
}

/* A press while a menu or lights are showing. true = it was used here (the normal handler must not see it). */
static bool overlay_event(int idx, int pad)
{
    if (menu_on) {
        if (idx == IN_BROWSER) menu_back();
        else if (idx == IN_SELECT) menu_push();
        else if (idx == IN_PLAY || idx == IN_STOP || idx == IN_SHIFT || idx == IN_ALT) return false;   /* transport + modifiers still work */
        dirty = true;
        return true;
    }
    if (idx == IN_BROWSER) { menu_on = true; menu_edit = false; menu_grp = 0; menu_sel = 0; dirty = true; return true; }
    if (!lt_on) return false;
    if (idx == IN_SHIFT || idx == IN_ALT) return false;
    if (idx == IN_PLAY) { if (!lt_manual) lights_off(); return false; }
    if (idx == IN_STOP && !playing && !ext_playing) { lights_next(1); commit_pending(); return true; }   /* STOP still saves what waits */
    if (idx == IN_STOP) return false;
    if (idx != IN_SELECT) {                         /* anything else: the pads were only asleep */
        lights_off();
        if (pad >= 0) lt_eat |= (uint64_t)1 << pad;
    }
    return true;
}

/* unsaved changes: a save pending / running, or (with AUTOSAVE off) anything waiting for SAVE NOW */
static bool unsaved(void)
{
    return save_due || save_lazy_due || save_run || save_waiting_compact || (G.autosave_off && (global_dirty || (pat_dirty & ~pat_stuck)));
}
static void draw_save_star(void) { if (unsaved()) draw_text(122, 0, "*", 1); }

static void __attribute__((noinline)) draw_menu(void)
{
    scr_menu_row_t rows[MENU_PAGE_MAX];
    char vals[MENU_PAGE_MAX][14];
    int n = menu_count();
    if (n > MENU_PAGE_MAX) n = MENU_PAGE_MAX;       /* (wiring_test keeps every page within it) */
    for (int i = 0; i < n; i++) {
        const mitem_t *m = menu_it(i);
        menu_value(m, vals[i]);
        rows[i].name = m->name; rows[i].value = vals[i];
    }
    const char *note = (menu_toast_ms && ms_ticks - menu_toast_ms < 2000) ? menu_toast : menu_edit ? "SEL=set" : "BRW=back";
    scr_menu(MENU_TITLE[menu_grp], note, rows, n, menu_sel, menu_edit, unsaved());
}

static void __attribute__((noinline)) draw_lights_screen(void)
{
    char buf[24], *o;
    draw_text(0, 0, "Lights", 1);
    if (lt_host) draw_text(44, 0, "Host", 1);
    else if (G.lt_mode == SHOW_CYCLE && lt_cur < LT_FX) draw_text(44, 0, "Cycle", 1);
    draw_text(0, 16, lt_cur == LT_M_HOST ? "Pi VU" : LT_NAME[lt_cur], 2);
    int h = (int)(lt_speed_f() * 100.0f + 0.5f);
    o = cat(buf, "x"); o = cat_int(o, h / 100); o = cat(o, "."); if (h % 100 < 10) o = cat(o, "0"); o = cat_int(o, h % 100);
    o = cat(o, "  "); o = cat_int(o, BPM); cat(o, "bpm");
    if (lt_cur == LT_M_TEXT) draw_text_n(0, 36, lights_text(), 21, 1);   /* the message itself */
    else draw_text(0, 36, buf, 1);
    draw_text(0, 46, "SEL=show  1A=speed", 1);
    draw_text(0, 55, "BRW=menu  tap=exit", 1);
    draw_save_star();
}

static void __attribute__((noinline)) render(void)
{
    char buf[24], *o;
    draw_clear();
    if (menu_on) { draw_menu(); return; }
    if (lt_on) { draw_lights_screen(); return; }
    if (surface_on) {                               /* the ORC surface: say whose pads they are and how to leave */
        draw_text(0, 0, "ORC mode", 1);
        draw_text(0, 20, "pads talk to the ORC", 1);
        draw_text(0, 30, "no MIDI channel used", 1);
        draw_text(0, 46, "STEP/NOTE/DRUM/PERF", 1);
        draw_text(0, 55, "= back to the seq", 1);
        draw_save_star();
        return;
    }
    if (pop_kind && ms_ticks - pop_ms < 1200) {      /* tempo / swing: the number, big, for a moment after a change */
        if (pop_kind == 1) scr_popup("Tempo", BPM, "bpm", BPM, 40, 300, unsaved());
        else scr_popup("Swing", G.swing, "%", G.swing, 50, 75, unsaved());
        return;
    }
    pop_kind = 0;
    char left[24], ctx[16], stat[24];
    if (song_active()) {                            /* S<slot>/<len> P<pattern> <bar>/<bars> */
        o = cat(left, "S"); o = cat_int(o, song_slot + 1); o = cat(o, "/"); o = cat_int(o, song_len());
        o = cat(o, " P"); o = cat_int(o, CURPAT + 1); o = cat(o, " "); o = cat_int(o, song_bar + 1);
        o = cat(o, "/"); o = cat_int(o, slot_reps(song_slot));
        if (queued_pat >= 0) { o = cat(o, ">S"); cat_int(o, queued_pat + 1); }
    } else {
        o = cat(left, "PAT "); o = cat_int(o, CURPAT + 1);
        if (queued_pat >= 0) { o = cat(o, ">"); o = cat_int(o, queued_pat + 1); }
        if (G.song_on) cat(o, " nosong");
    }
    bool step_held = held_t >= 0 && held_row < 0 && (steps_get(CURPAT, held_t, held_s) & 3);
    ctx[0] = stat[0] = 0;
    cat(stat, status_line);
    if (picker_on()) cat(ctx, "Patterns");
    else if (mode == MODE_FADER) {
        if (held_row >= 0 && held_row_t < G.fad_n) { cat(ctx, "Fader"); stat[0] = 0; }
        else {
            int rows = G.fad_n < MAXFADERS ? G.fad_n + 1 : MAXFADERS;
            cat(ctx, "Faders");
            o = cat(stat, "F"); o = cat_int(o, fad_top + 1); o = cat(o, "-"); o = cat_int(o, fad_top + VIEW_ROWS < rows ? fad_top + VIEW_ROWS : rows);
            o = cat(o, "/"); cat_int(o, G.fad_n);
        }
    }
    else if (mode == MODE_NOTE) cat(ctx, "NOTE");
    else if (mode == MODE_DRUM) cat(ctx, "DRUM");
    else if (song_edit) cat(ctx, "Song");
    else if (step_held) {
        o = cat(ctx, track_name(held_t)); o = cat(o, " step "); cat_int(o, held_s + 1);
        cat(stat, knob_page ? "pg2" : "pg1");
    }
    else if (held_row >= 0) { cat(ctx, "Track "); cat(ctx, track_name(held_row_t)); stat[0] = 0; }
    else { o = cat(ctx, "P"); o = cat_int(o, page + 1); o = cat(o, "/"); cat_int(o, PAGES); }
    {
        scr_head_t hd = {left, (int)BPM, G.swing, ext_clock ? 2 : G.clock_out ? 1 : 0, playing || ext_playing, unsaved(), ctx, stat};
        scr_header(&hd);
    }
    if (picker_on()) {                              /* the pattern picker */
        int p = song_active() ? picker_sel : CURPAT;
        draw_text(0, 28, "Patterns: tap one", 1);
        o = cat(buf, "P"); o = cat_int(o, p + 1); o = cat(o, pat_empty(p) ? " (empty)" : "");
        draw_text(0, 37, buf, 1);
        o = cat(buf, "1A color "); cat(o, FAD_COL_NAME[pat_col_idx(p)]);
        draw_text(0, 46, buf, 1);
        if (song_active()) draw_text(0, 55, "song on: tap = pick", 1);
    } else if (mode == MODE_FADER && held_row >= 0 && held_row_t < G.fad_n) {   /* setting up a fader */
        int f = held_row_t;
        o = cat(buf, "Fader "); cat_int(o, f + 1);
        draw_text(0, 28, buf, 1);
        o = cat(buf, "1A CC "); cat_int(o, G.fad_cc[f]);
        draw_text(0, 37, buf, 1);
        o = cat(buf, "1B ch "); cat_int(o, G.fad_ch[f]);
        draw_text(0, 46, buf, 1);
        o = cat(buf, "1C "); cat(o, FAD_COL_NAME[G.fad_col[f] & 15]);
        draw_text(0, 55, buf, 1);
        draw_text(50, 28, "SH+row=del", 1);
    } else if (mode == MODE_FADER) {                /* the 4 visible faders, as bars */
        scr_fader_t fl[4];
        for (int r = 0; r < VIEW_ROWS; r++) {
            int f = fad_top + r;
            fl[r].used = f < G.fad_n ? 1 : f == G.fad_n ? 0 : -1;
            fl[r].num = f + 1; fl[r].cc = f < G.fad_n ? G.fad_cc[f] : 0; fl[r].val = f < G.fad_n ? G.fad_val[f] : 0;
        }
        scr_faders(fl);
    } else if (mode == MODE_NOTE) {                        /* keyboard */
        draw_text(0, 28, "NOTE", 2);
        o = cat(buf, track_name(kb_track())); o = cat(o, " ch"); cat_int(o, tchan(kb_track()));
        draw_text(56, 28, buf, 1);
        if (rec_arm) draw_text(104, 28, "REC", 1);
        o = cat(buf, note_name((uint8_t)kb_base())); o = cat(o, "-"); cat(o, note_name((uint8_t)(kb_base() + KB_SPAN)));
        draw_text(56, 37, buf, 1);
        draw_text(0, 46, "GRID oct  SH=accent", 1);
        draw_text(0, 55, "SEL trk  ALT+SEL ch", 1);
    } else if (mode == MODE_DRUM && fire_held(IN_ALT)) {   /* editing a grid */
        int g = grid_sel;
        o = cat(buf, "Group "); cat_int(o, g + 1);
        draw_text(0, 28, buf, 1);
        draw_text(48, 28, "tap a group", 1);
        o = cat(buf, "1A ch "); cat_int(o, grid_chan(g));
        draw_text(0, 37, buf, 1);
        if (G.grid_layout[g] == 1) cat(buf, "1B pad 1 (MPC pads)");
        else { o = cat(buf, "1B pad 1 "); o = cat(o, note_name((uint8_t)grid_base(g))); o = cat(o, " ("); o = cat_int(o, grid_base(g)); cat(o, ")"); }
        draw_text(0, 46, buf, 1);
        draw_text(0, 55, G.grid_layout[g] == 1 ? "1C map: MPC pads" : "1C map: notes", 1);
    } else if (mode == MODE_DRUM) {                 /* the four grids */
        for (int g = 0; g < 4; g++) {
            o = cat(buf, "G"); o = cat_int(o, g + 1); o = cat(o, " ch"); cat_int(o, grid_chan(g));
            draw_text(0, 28 + g * 9, buf, 1);
            if (G.grid_layout[g] == 1) cat(buf, "MPC pads");
            else { o = cat(buf, note_name((uint8_t)grid_base(g))); o = cat(o, "-"); cat(o, note_name((uint8_t)(grid_base(g) + 15))); }
            draw_text(42, 28 + g * 9, buf, 1);
        }
        draw_text(98, 28, rec_arm ? "REC" : "", 1);
        draw_text(98, 37, "ALT", 1);
        draw_text(98, 46, "=edit", 1);
    } else if (song_edit) {                         /* song editor help */
        o = cat(buf, "Song edit  slot "); cat_int(o, song_sel + 1);
        draw_text(0, 28, buf, 1);
        if (G.song_pat[song_sel]) { o = cat(buf, "pattern "); o = cat_int(o, G.song_pat[song_sel]); o = cat(o, "  bars "); cat_int(o, slot_reps(song_sel)); }
        else cat(buf, "(empty)");
        draw_text(0, 37, buf, 1);
        draw_text(0, 46, "1:slot 2-3:pat 4:bars", 1);
        draw_text(0, 55, "SH+slot=clr  1A=col", 1);
    } else if (step_held) {                         /* step settings (the header names the step and the page) */
        step_t sv; steps_read(CURPAT, held_t, held_s, &sv);
        uint8_t b = sv.b;
        if (knob_page == 0) {
            int hm = hold_of(sv.cond);
            scr_step_t ss = {step_ratchet(b), PROB_PCT[step_prob(b)], is_mel(held_t), "", hm == H_TIE ? 1 : hm == H_SLIDE ? 2 : 0, step_gate(b)};
            char nb[16];
            if (is_mel(held_t)) {
                uint8_t n = step_note(CURPAT, held_t, held_s);
                o = cat(nb, note_name(n)); o = cat(o, " ("); o = cat_int(o, n); cat(o, ")");
                ss.note = nb;
            }
            scr_step_bars(&ss);
        } else {
            o = cat(buf, "2A cond "); cat(o, COND_NAME[cond_of(sv.cond)]);
            draw_text(0, SCR_BODY_Y, buf, 1);
            if (!G.lock_cc[held_t]) cat(buf, "2B lock: set CC 1st");
            else if (!sv.lock) { o = cat(buf, "2B CC"); o = cat_int(o, G.lock_cc[held_t]); cat(o, " --"); }
            else { o = cat(buf, "2B CC"); o = cat_int(o, G.lock_cc[held_t]); o = cat(o, " = "); cat_int(o, sv.lock - 1); }
            draw_text(0, SCR_BODY_Y + SCR_ROW_H, buf, 1);
            if (is_mel(held_t)) {
                int ci = chord_index(sv.ch2, sv.ch3);
                o = cat(buf, "2C chord ");
                if (ci >= 0) cat(o, CHORDS[ci].name);
                else { if (sv.ch2 >= 0) o = cat(o, "+"); o = cat_int(o, sv.ch2); o = cat(o, " "); if (sv.ch3 >= 0) o = cat(o, "+"); cat_int(o, sv.ch3); }
                draw_text(0, SCR_BODY_Y + 2 * SCR_ROW_H, buf, 1);
            }
        }
    } else if (held_row >= 0) {
        int t = held_row_t;
        o = cat(buf, "TRACK "); cat(o, track_name(t));
        draw_text(0, 28, buf, 1);
        uint8_t n = is_mel(t) ? m_cur[t - DTRACKS] : tnote(t);
        o = cat(buf, "1A ch "); o = cat_int(o, tchan(t)); o = cat(o, "  len "); cat_int(o, tlen(CURPAT, t));
        draw_text(0, 37, buf, 1);
        o = cat(buf, is_mel(t) ? "1B new " : "1B note "); o = cat(o, note_name(n)); o = cat(o, " ("); o = cat_int(o, n); cat(o, ")");
        draw_text(0, 46, buf, 1);
        if (G.lock_cc[t]) { o = cat(buf, "1C lock CC "); cat_int(o, G.lock_cc[t]); } else cat(buf, "1C lock CC: none");
        draw_text(0, 55, buf, 1);
    } else {                                        /* the step view: dots per step, a playhead per track */
        char nm[4][6];
        const char *names[4];
        uint8_t lv[4][16], mu[4];
        int8_t hd[4];
        bool run = playing || ext_playing;
        uint32_t last = gstep ? gstep - 1 : 0;
        for (int r = 0; r < VIEW_ROWS; r++) {
            int t = view_top + r, len = tlen(CURPAT, t);
            strcpy(nm[r], track_name(t)); names[r] = nm[r];
            mu[r] = muted(t);
            for (int c = 0; c < PAGE; c++) { int st = page * PAGE + c; lv[r][c] = st < len ? (uint8_t)level(CURPAT, t, st) : 255; }
            int hs = track_pos(t, last) - page * PAGE;
            hd[r] = (has_played && run && hs >= 0 && hs < PAGE) ? (int8_t)hs : -1;
        }
        scr_tracks(names, lv, mu, hd);
    }
}

/* ---------- faders ---------- */

static int fad_max_top(void) { int rows = G.fad_n < MAXFADERS ? G.fad_n + 1 : MAXFADERS; return rows > VIEW_ROWS ? rows - VIEW_ROWS : 0; }
static void scroll_faders(int d)
{
    fad_top += d;
    if (fad_top > fad_max_top()) fad_top = fad_max_top();
    if (fad_top < 0) fad_top = 0;
    dirty = true;
}
static void fader_send(int f) { midi_send3((uint8_t)(0xB0 | (G.fad_ch[f] - 1)), G.fad_cc[f], G.fad_val[f]); }
static void fader_set(int f, int v)                 /* a new level: send it, remember it (rides along with the next save) */
{
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (G.fad_val[f] == v) return;
    G.fad_val[f] = (uint8_t)v;
    fader_send(f);
    touched_global();
    dirty = true;
}
static void fader_add(void)                         /* a new fader at the end: next channel, CC 7, next colour */
{
    if (G.fad_n >= MAXFADERS) return;
    int f = G.fad_n, prev = f ? f - 1 : -1;
    G.fad_cc[f] = 7;
    G.fad_ch[f] = (uint8_t)(prev >= 0 ? G.fad_ch[prev] % 16 + 1 : 1);
    G.fad_col[f] = (uint8_t)(prev >= 0 ? (G.fad_col[prev] + 1) % 16 : 0);
    G.fad_val[f] = 100;
    G.fad_n++;
    edited_global();
}
static void fader_remove(int f)                     /* the faders below move up */
{
    if (f < 0 || f >= G.fad_n) return;
    for (int i = f; i < G.fad_n - 1; i++) { G.fad_cc[i] = G.fad_cc[i + 1]; G.fad_ch[i] = G.fad_ch[i + 1]; G.fad_col[i] = G.fad_col[i + 1]; G.fad_val[i] = G.fad_val[i + 1]; }
    G.fad_n--;
    scroll_faders(0);
    edited_global();
}

/* ---------- inputs ---------- */

static void scroll_tracks(int d)
{
    if (mode == MODE_FADER) { scroll_faders(d); return; }
    view_top += d;
    if (view_top < 0) view_top = 0;
    if (view_top > TRACKS - VIEW_ROWS) view_top = TRACKS - VIEW_ROWS;
    dirty = true;
}

/* DRUM mode + REC while playing: write the hit into the current pattern at the nearest step.
   A hit in the second half of a step lands on the next one. A step that hasn't played yet then
   skips its note once (the live note already sounded).
   The hit goes to the track playing that note on that channel; if there is none, the first track
   with no steps in any pattern takes over that note and channel. */
static bool track_unused(int t) { return steps_track_empty(t); }   /* no steps in any pattern */

static void record_step(int t, uint8_t note, int lv, uint32_t now);

static void record_hit(uint8_t note, uint8_t chan1, int lv, uint32_t now)
{
    if (!(playing || ext_playing) || !gstep) return;
    int t = -1;
    for (int i = 0; i < DTRACKS && t < 0; i++) if (tnote(i) == note && tchan(i) == chan1) t = i;
    if (t < 0) {
        for (int i = 0; i < DTRACKS && t < 0; i++) if (track_unused(i)) t = i;
        if (t < 0) { cat(status_line, "no track"); return; }
        G.note[t] = note; G.chan[t] = chan1;
        G.mute_bits &= (uint16_t)~(1u << t);
        edited_global();
        char *o = cat(status_line, "rec>"); cat(o, track_name(t));
    }
    record_step(t, note, lv, now);
}

/* Write a live note into track t at the nearest step (melodic tracks also keep its note). */
/* The pattern that will be playing on step g (g at most one step ahead: a bar start may switch). */
static int pattern_for_step(uint32_t g)
{
    if (g < gstep || g % PAGE || g == 0) return CURPAT;
    if (song_active()) {
        int slot = queued_pat >= 0 ? queued_pat : (song_bar + 1 >= slot_reps(song_slot) ? (song_slot + 1) % song_len() : song_slot);
        return G.song_pat[slot] - 1;
    }
    return queued_pat >= 0 ? queued_pat : CURPAT;
}

static void record_step(int t, uint8_t note, int lv, uint32_t now)
{
    if (!(playing || ext_playing) || !gstep) return;
    /* The nearest step by when it SOUNDS (swing moves odd steps later): look at the step that last
       started, the one before and the next one. */
    uint32_t r = gstep - 1, S = cur_step_us(), W = cur_swing_us(), tr;
    if (playing) tr = step_start_us;                /* grid time of step r */
    else { if (!ext_step_len) { S = 0; } tr = ext_step_at - ((r & 1) ? W : 0); }
    uint32_t g = S ? nearest_sounding_step(r, tr, S, W, now) : r;
    if (g >= gstep || (playing && pending >= 0 && (uint32_t)pending == g))
        rec_skip[t] = (int32_t)g + 1;               /* that step hasn't played yet: don't play it twice */
    int P = pattern_for_step(g);                    /* a hit just before a bar that switches pattern goes into the NEW one */
    int s = (int)(g % (uint32_t)tlen(P, t));
    uint8_t old = steps_get(P, t, s);
    bool ok = true;
    if (is_mel(t)) {
        step_t v; steps_read(P, t, s, &v);
        int iv = (int)note - (int)v.note;
        if (rec_chord_t == t && rec_chord_g == g && (v.b & 3) && iv && iv >= -48 && iv <= 48) {
            /* another note on the same step while recording: add it to the chord (up to 3 notes) */
            if (iv != v.ch2 && iv != v.ch3) {
                if (!v.ch2) v.ch2 = (int8_t)iv; else if (!v.ch3) v.ch3 = (int8_t)iv;
                ok = steps_write(P, t, s, &v);
            }
        } else {                                    /* the first note: replaces the step's note and chord */
            v.b = (uint8_t)((v.b & ~3) | lv); v.note = note; v.ch2 = v.ch3 = 0;
            ok = steps_write(P, t, s, &v);
        }
        rec_chord_t = t; rec_chord_g = g;
    }
    else if ((old & 3) < lv) ok = steps_set_byte(P, t, s, (uint8_t)((old & ~3) | lv));   /* drums: never lowers an accent */
    if (!ok) { pool_full(); return; }
    touched_pattern(P);                             /* a live hit: saved with the next save / at STOP */
}

static void set_mode(int m)
{
    if (surface_on) { surface_on = false; dirty = true; }   /* any mode button leaves the ORC surface */
    if (m == mode) return;
    mode = m;
    held_t = held_s = -1; held_pad = -1;            /* a held step pad no longer means anything */
    if (m != MODE_STEP) song_edit = false;
    dirty = true;
}

/* PATTERN up / down (copy = ALT: duplicate the current pattern into the next / previous one). */
static void pattern_step(int dir, bool copy)
{
    if (song_active() && !copy) {                   /* song mode: PATTERN = next / previous slot */
        int base = queued_pat >= 0 ? queued_pat : song_slot;
        int to = (base + (dir > 0 ? 1 : song_len() - 1)) % song_len();
        if (playing || ext_playing) queued_pat = to; else song_goto(to);
        dirty = true;
        return;
    }
    int base = (queued_pat >= 0 && !song_active()) ? queued_pat : CURPAT;   /* (song mode queues slots) */
    int to = (base + (dir > 0 ? 1 : PATTERNS - 1)) % PATTERNS;
    if (copy) {
        cancel_save_of(KIND_PAT(to));
        if (!steps_copy_pattern(CURPAT, to)) { pool_full(); return; }
        G.pat_col[to] = (uint8_t)(pat_col_idx(CURPAT) + 1);   /* the copy takes the colour too */
        edited_pattern(to); edited_global();
        if (song_active()) {                        /* song mode: copy only; the song decides what plays */
            char *o = cat(status_line, "copy>P"); cat_int(o, to + 1);
            dirty = true; return;
        }
    }
    if (playing || ext_playing) queued_pat = to;
    else { CURPAT = (uint8_t)to; queued_pat = -1; }
    dirty = true; touched_global();
}

/* A held step pad belongs to the step it was pressed on: if the view, page or pattern moves under
   it (scrolling, GRID, a bar-boundary pattern switch, the song), the hold is dropped, so its release
   or a knob can't edit some other step. */
static void check_hold(void)
{
    if (held_t >= 0 && (view_top != hold_view || page != hold_page || CURPAT != hold_pat || song_edit != hold_songed)) {
        held_t = held_s = -1; held_pad = -1; dirty = true;
    }
}

static void __attribute__((noinline)) handle_events(uint32_t now)
{
    check_hold();
    int e;
    while ((e = fire_event()) >= 0) {
        int idx = e & 0x7F;
        bool down = e & 0x80;
        int pad = pad_of_input(idx);
        lt_input_ms = ms_ticks;
        if (pad >= 0 && !down && (lt_eat >> pad & 1)) { lt_eat &= ~((uint64_t)1 << pad); continue; }   /* the release of a press that only woke the pads */
        if (down && overlay_event(idx, pad)) continue;   /* the menu, or lights showing: BROWSER, and the wake-up touch */
        if (pad >= 0 && !down && live[pad].on) {    /* a live pad let go: end exactly the note it started, whatever the mode is now */
            live[pad].on = false;
            if (live[pad].sx) {                     /* a surface pad: its release is a surface message too */
                live[pad].sx = false;
                surf_send(pad, 0);
                dirty = true;
                continue;
            }
            if (live[pad].cc) {                     /* an MPC panel pad: let the MPC's pad go */
                live[pad].cc = false;
                midi_send3((uint8_t)(0xB0 | live[pad].chan), live[pad].note, 0);
                dirty = true;
                continue;
            }
            if (live[pad].keys && notes_held[live[pad].note]) notes_held[live[pad].note]--;
            bool other = false;                     /* another pad still holding the same note + channel? */
            for (int q = 0; q < 64; q++) if (live[q].on && live[q].note == live[pad].note && live[q].chan == live[pad].chan) other = true;
            if (!other) midi_send3((uint8_t)(0x80 | live[pad].chan), live[pad].note, 0);
            dirty = true;
            continue;
        }
        if (pad >= 0 && surface_on) {               /* the ORC surface: a surface message, no MIDI channel (v0.21) */
            if (!down) continue;                    /* (the release is handled above, by live[]) */
            uint8_t vel = fire_held(IN_SHIFT) ? LIVE_ACCENT : LIVE_VEL;
            surf_send(pad, vel);
            live[pad].note = (uint8_t)pad; live[pad].chan = 0; live[pad].keys = false; live[pad].sx = true; live[pad].cc = false; live[pad].on = true;
            dirty = true;
            continue;
        }
        if (pad >= 0 && picker_on()) {              /* the pattern picker: tap a pattern (top two rows) */
            if (!down) continue;
            if (pad >= 32) continue;
            int p = pad;                            /* row 0 = 1-16, row 1 = 17-32 */
            picker_sel = p; picker_used = true;
            if (!song_active()) {
                if (playing || ext_playing) queued_pat = p == CURPAT ? -1 : p;
                else { CURPAT = (uint8_t)p; queued_pat = -1; }
                touched_global();
            }
            dirty = true;
            continue;
        }
        if (pad >= 0 && mode == MODE_FADER) {       /* tap a column: that fader's level (0 left .. 127 right) */
            if (!down) continue;
            int f = fad_top + pad / 16;
            if (f < G.fad_n) fader_set(f, ((pad % 16) * 127 + 7) / 15);
            continue;
        }
        if (pad >= 0 && mode != MODE_STEP) {
            if (!down) continue;
            int r = pad / 16, c = pad % 16;
            if (mode == MODE_DRUM && fire_held(IN_ALT)) { grid_sel = c / 4; dirty = true; continue; }   /* ALT + tap: pick a group to edit */
            uint8_t vel = fire_held(IN_SHIFT) ? LIVE_ACCENT : LIVE_VEL, note, ch;
            if (mode == MODE_NOTE) {
                note = (uint8_t)kb_note(r, c); ch = (uint8_t)(tchan(kb_track()) - 1);
                notes_held[note]++;
                m_cur[kb_track() - DTRACKS] = note;     /* steps lit by hand on this track get this note */
                if (rec_arm) record_step(kb_track(), note, vel == LIVE_ACCENT ? LV_ACCENT : LV_NORMAL, now);
            } else {
                int g = c / 4;
                note = grid_note(g, grid_padnum(r, c)); ch = (uint8_t)(grid_chan(g) - 1);
                if (grid_mpc(g)) {                  /* the MPC's own pad, pressed: a control, never recorded as a note */
                    midi_send3((uint8_t)(0xB0 | ch), note, 127);
                    live[pad].note = note; live[pad].chan = ch; live[pad].keys = false; live[pad].sx = false; live[pad].cc = true; live[pad].on = true;
                    dirty = true;
                    continue;
                }
                if (rec_arm) record_hit(note, (uint8_t)(ch + 1), vel == LIVE_ACCENT ? LV_ACCENT : LV_NORMAL, now);
            }
            midi_send3((uint8_t)(0x90 | ch), note, vel);
            live[pad].note = note; live[pad].chan = ch; live[pad].keys = mode == MODE_NOTE; live[pad].sx = false; live[pad].cc = false; live[pad].on = true;
            dirty = true;
            continue;
        }
        if (pad >= 0 && song_edit) {                /* song editor */
            if (!down) continue;
            int r = pad / 16, c = pad % 16;
            if (r == 0) {
                if (fire_held(IN_SHIFT)) {          /* SHIFT + slot: clear it (later slots move up) */
                    for (int i = c; i < SLOTS - 1; i++) { G.song_pat[i] = G.song_pat[i + 1]; G.song_rep[i] = G.song_rep[i + 1]; }
                    G.song_pat[SLOTS - 1] = 0; G.song_rep[SLOTS - 1] = 0;
                    if (song_slot >= song_len()) song_slot = 0;
                    edited_global();
                }
                song_sel = c > song_len() ? song_len() : c;   /* no gaps: at most one past the end */
                if (song_sel >= SLOTS) song_sel = SLOTS - 1;
            } else if (r == 1 || r == 2) {          /* pattern 1-32 for the selected slot */
                G.song_pat[song_sel] = (uint8_t)((r - 1) * 16 + c + 1);
                edited_global();
            } else if (c < 8 && G.song_pat[song_sel]) {   /* bottom row: bars for the selected slot */
                G.song_rep[song_sel] = (uint8_t)(c + 1);
                edited_global();
            }
            dirty = true;
            continue;
        }
        if (pad >= 0) {
            int r = pad / 16, c = pad % 16, t = view_top + r, s = page * PAGE + c;
            uint8_t st = steps_get(CURPAT, t, s);
            if (!down) {                            /* release: a lit step tapped (not edited) turns off */
                check_hold();
                if (pad == held_pad && held_t >= 0) {
                    if (held_pad_was_lit && !held_pad_turned) { steps_set(CURPAT, held_t, held_s, 0, 0); edited_pattern(CURPAT); }
                    held_t = held_s = -1; held_pad = -1;
                    dirty = true;
                }
                continue;
            }
            if (fire_held(IN_ALT)) {                /* ALT + pad: track length */
                steps_len[CURPAT][t] = (uint8_t)(s + 1);
                char *o = cat(status_line, "len "); cat_int(o, s + 1);
            } else if (s >= tlen(CURPAT, t)) {
                continue;                           /* past the track's end */
            } else if (fire_held(IN_SHIFT)) {       /* SHIFT + pad: velocity tier, lit steps only */
                int lv = st & 3;
                if (lv == LV_OFF) continue;
                steps_set_byte(CURPAT, t, s, (uint8_t)((st & ~3) | (lv == LV_ACCENT ? LV_SOFT : lv == LV_SOFT ? LV_NORMAL : LV_ACCENT)));
            } else {                                /* plain: unlit -> on now; lit -> off on release (unless edited) */
                held_t = t; held_s = s; held_pad = pad; held_pad_turned = false;
                hold_view = view_top; hold_page = page; hold_pat = CURPAT; hold_songed = song_edit;
                held_pad_was_lit = (st & 3) != LV_OFF;
                if (!held_pad_was_lit) {
                    if (!steps_set(CURPAT, t, s, LV_NORMAL, is_mel(t) ? m_cur[t - DTRACKS] : 0)) {
                        held_t = held_s = -1; held_pad = -1; pool_full(); continue;
                    }
                }
                else { dirty = true; continue; }
            }
            dirty = true; edited_pattern(CURPAT);
            continue;
        }
        int row = -1;
        for (int r = 0; r < VIEW_ROWS; r++) if (idx == IN_ROWBTN[r]) row = r;
        if (row >= 0 && mode == MODE_FADER) {       /* hold = set it up (knobs), tap = add (empty row), SHIFT + tap = remove */
            if (down) { held_row = row; held_row_t = fad_top + row; held_row_turned = false; }
            else if (held_row == row) {
                int f = held_row_t;
                if (!held_row_turned) {
                    if (fire_held(IN_SHIFT)) fader_remove(f);
                    else if (f == G.fad_n) { fader_add(); scroll_faders(1); }   /* keep the next "+" in view */
                    else if (f < G.fad_n) fader_send(f);   /* tap: send its level again */
                }
                held_row = -1;
            }
            dirty = true;
            continue;
        }
        if (row >= 0) {                             /* hold = track settings, tap = mute */
            if (down) { held_row = row; held_row_t = view_top + row; held_row_turned = false; }
            else if (held_row == row) {
                if (!held_row_turned) { toggle_mute(held_row_t); touched_global(); }
                held_row = -1;
            }
            dirty = true;
            continue;
        }
        if (!down && idx == pat_btn) {              /* PATTERN let go: if the picker wasn't used, it was a plain up / down */
            pat_btn = -1;
            if (!picker_used) pattern_step(idx == IN_PAT_UP ? 1 : -1, false);
            dirty = true;
            continue;
        }
        if (!down && idx == IN_PERFORM && ms_ticks - perform_down_ms < 300) {   /* a tap: faders on / off */
            if (mode == MODE_FADER) set_mode(mode_before_faders);
            else { mode_before_faders = mode; set_mode(MODE_FADER); }
        }
        if (!down) { if (idx == IN_SHIFT || idx == IN_ALT || idx == IN_PERFORM) dirty = true; continue; }
        switch (idx) {
        case IN_PLAY:
            if (fire_held(IN_SHIFT)) {
                clock_out_toggle();
                break;
            }
            if (ext_clock) break;
            if (playing) stop(false); else start(now);
            break;
        case IN_STOP: stop(true); break;
        case IN_PATSONG:
            if (fire_held(IN_SHIFT)) { set_mode(MODE_STEP); song_edit = !song_edit; }   /* SHIFT + PATTERN/SONG: song editor */
            else {
                G.song_on = !G.song_on;
                queued_pat = -1;                    /* a queued slot / pattern means something else now */
                if (G.song_on && song_len()) {
                    if (playing || ext_playing) queued_pat = 0;  /* join the song at the next bar */
                    else song_goto(0);
                }
                cat(status_line, G.song_on ? "song on" : "song off");
                touched_global();
            }
            dirty = true;
            break;
        case IN_STEP: set_mode(MODE_STEP); break;
        case IN_NOTE: set_mode(MODE_NOTE); break;
        case IN_DRUM: set_mode(MODE_DRUM); break;
        case IN_REC: rec_arm = !rec_arm; dirty = true; break;
        case IN_GRID_L:
        case IN_GRID_R:
            if (mode == MODE_NOTE) {                /* octave (SHIFT: semitone) */
                int b = kb_base() + (idx == IN_GRID_R ? 1 : -1) * (fire_held(IN_SHIFT) ? 1 : 12);
                G.kb_base = (uint8_t)(b < 1 ? 1 : b > 96 ? 96 : b);
                touched_global();
            } else if (mode == MODE_STEP) {
                if (idx == IN_GRID_L && page > 0) page--;
                if (idx == IN_GRID_R && page < PAGES - 1) page++;
            }
            dirty = true;
            break;
        case IN_PAT_UP:
        case IN_PAT_DN:
            if (fire_held(IN_SHIFT)) {              /* SHIFT + PATTERN: scroll tracks a page */
                scroll_tracks(idx == IN_PAT_UP ? -VIEW_ROWS : VIEW_ROWS);
                break;
            }
            if (fire_held(IN_ALT)) { pattern_step(idx == IN_PAT_UP ? 1 : -1, true); break; }   /* ALT: copy, at once */
            pat_btn = idx; picker_used = false; pat_down_ms = ms_ticks;   /* hold = the picker; a quick press steps on release */
            picker_sel = song_active() && G.song_pat[song_slot] ? G.song_pat[song_slot] - 1 : CURPAT;
            dirty = true;
            break;
        case IN_PERFORM: perform_down_ms = ms_ticks; dirty = true; break;
        case IN_SHIFT: case IN_ALT: dirty = true; break;
        case IN_KNOBMODE:                           /* a step held: its knob page 1 / 2; nothing held: the next knob bank */
            if (held_t >= 0 && held_row < 0) { knob_page ^= 1; held_pad_turned = true; }   /* flipping the page is an edit: releasing must not erase the step */
            else { G.knob_bank = (uint8_t)((G.knob_bank + 1) & 3); touched_global(); char *ko = cat(status_line, "knobs "); cat_int(ko, G.knob_bank + 1); }
            dirty = true; break;
        default: break;
        }
    }
    int d = fire_encoder_take();
    if (d) {
        lt_input_ms = ms_ticks;
        if (menu_on) menu_turn(d);                  /* the menu: SELECT scrolls / changes the value */
        else if (lt_on && !fire_held(IN_SHIFT)) lights_next(d);   /* lights: SELECT = another show */
        else if (fire_held(IN_SHIFT)) {             /* SHIFT + SELECT: tempo (with ALT: x10) */
            int b = BPM + d * (fire_held(IN_ALT) ? 10 : 1);
            BPM = (uint16_t)(b < 40 ? 40 : b > 300 ? 300 : b);
            touched_global(); pop_kind = 1; pop_ms = ms_ticks;
        } else if (mode == MODE_NOTE && fire_held(IN_ALT)) {   /* NOTE mode: ALT + SELECT = that track's channel */
            int t = kb_track(), c = tchan(t) + d;
            G.m_chan[t - DTRACKS] = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c);
            edited_global();
        } else if (mode == MODE_FADER) {
            scroll_faders(d);                       /* SELECT: scroll faders */
        } else if (mode == MODE_NOTE) {             /* NOTE mode: SELECT = which melodic track it plays */
            int k = G.kb_track % MTRACKS + d;
            G.kb_track = (uint8_t)(k < 0 ? 0 : k > MTRACKS - 1 ? MTRACKS - 1 : k);
            touched_global();
        } else {
            scroll_tracks(d);                       /* SELECT: scroll tracks */
        }
        dirty = true;
    }
    check_hold();
    for (int k = 0; k < 4; k++) {
        int v = fire_knob_take(k);
        if (!v) continue;
        lt_input_ms = ms_ticks;
        if (menu_on) continue;                      /* the menu uses SELECT only */
        if (lt_on) { if (k == 0) lights_speed(v); continue; }   /* lights: knob 1 = speed */
        if (picker_on()) {                          /* picker: knob 1 = the picked pattern's colour */
            if (k == 0) { pat_recolour(picker_sel, v); picker_used = true; }
            continue;
        }
        if (song_edit && k == 0) {                  /* song editor: knob 1 = the selected slot's pattern colour */
            if (G.song_pat[song_sel]) pat_recolour(G.song_pat[song_sel] - 1, v);
            continue;
        }
        if (mode == MODE_FADER) {
            if (held_row >= 0) {                    /* setting up the held fader: CC, channel, colour */
                int f = held_row_t;
                if (f >= G.fad_n || k == 3) continue;
                held_row_turned = true;
                if (k == 0) { int c = G.fad_cc[f] + v; G.fad_cc[f] = (uint8_t)(c < 0 ? 0 : c > 127 ? 127 : c); }
                else if (k == 1) { int c = G.fad_ch[f] + v; G.fad_ch[f] = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c); }
                else { int c = G.fad_col[f] + v; G.fad_col[f] = (uint8_t)(c < 0 ? 0 : c > 15 ? 15 : c); }
                dirty = true; edited_global();
            } else if (fad_top + k < G.fad_n) fader_set(fad_top + k, G.fad_val[fad_top + k] + v);   /* knob k = row k's fader, fine */
            continue;
        }
        if (mode == MODE_DRUM && fire_held(IN_ALT) && k < 3) {   /* editing a group: channel, pad 1's note, map */
            int g = grid_sel;
            if (k == 0) { if (grid_mpc(g)) continue; int c = grid_chan(g) + v; G.grid_chan[g] = (uint8_t)(c < 1 ? 1 : c > 16 ? 16 : c); }   /* MPC pads: always ch 14 */
            else if (k == 1) { int b = grid_base(g) + v; G.grid_base[g] = (uint8_t)(b < 1 ? 1 : b > 112 ? 112 : b); G.grid_layout[g] = 0; }
            else G.grid_layout[g] = v > 0 ? 1 : 0;   /* clockwise = MPC pads, back = chromatic */
            dirty = true; edited_global();
            continue;
        }
        if (held_t >= 0 && held_row < 0) {
            /* holding a step. Page 1: knob 1 ratchet, 2 chance, 3 note (melodic), 4 gate / TIE / SLIDE.
               Page 2: knob 1 condition, 2 lock value, 3 chord (melodic). */
            step_t sv;
            if (!steps_read(CURPAT, held_t, held_s, &sv)) continue;
            if (k == 2 && !is_mel(held_t)) continue;   /* drum steps have no note or chord of their own */
            if (knob_page == 1 && k == 3) continue;
            held_pad_turned = true;
            uint8_t st = sv.b;
            if (knob_page == 0) {
                if (k == 0) { int r = step_ratchet(st) + v; r = r < 1 ? 1 : r > 4 ? 4 : r; sv.b = (uint8_t)((st & ~0x30) | ((r - 1) << 4)); }
                else if (k == 1) { int pr = step_prob(st) - v; pr = pr < 0 ? 0 : pr > 3 ? 3 : pr; sv.b = (uint8_t)((st & ~0x0C) | (pr << 2)); }  /* clockwise = more likely */
                else if (k == 2) {
                    int n = step_note(CURPAT, held_t, held_s) + v; n = n < 1 ? 1 : n > 127 ? 127 : n;
                    sv.note = (uint8_t)n; m_cur[held_t - DTRACKS] = (uint8_t)n;
                } else {                            /* gate 25 / 50 / 75 / 95 %, then TIE, SLIDE */
                    int hm = hold_of(sv.cond), pos = hm == H_TIE ? 4 : hm == H_SLIDE ? 5 : GATE_RANK[st >> 6 & 3];
                    pos += v; pos = pos < 0 ? 0 : pos > 5 ? 5 : pos;
                    if (pos <= 3) { sv.b = (uint8_t)((st & ~0xC0) | (GATE_CODE[pos] << 6)); sv.cond &= 0x3F; }
                    else sv.cond = (uint8_t)((sv.cond & 0x3F) | ((pos == 4 ? H_TIE : H_SLIDE) << 6));
                }
            } else {
                if (k == 0) { int c = cond_of(sv.cond) + v; c = c < 0 ? 0 : c > C_COUNT - 1 ? C_COUNT - 1 : c; sv.cond = (uint8_t)((sv.cond & 0xC0) | c); }
                else if (k == 1) {                  /* lock value: off <-> 0..127 (first turn up lands on 64) */
                    if (!G.lock_cc[held_t]) continue;   /* the track has no lock CC yet (hold its row + knob 3) */
                    int l = sv.lock;
                    if (!l && v > 0) l = 65 + (v - 1); else l += v;
                    sv.lock = (uint8_t)(l < 0 ? 0 : l > 128 ? 128 : l);
                } else {                            /* chord shape */
                    int ci = chord_index(sv.ch2, sv.ch3);
                    if (ci < 0) ci = 0;
                    ci += v; ci = ci < 0 ? 0 : ci > NCHORDS - 1 ? NCHORDS - 1 : ci;
                    sv.ch2 = CHORDS[ci].a; sv.ch3 = CHORDS[ci].b;
                }
            }
            steps_write(CURPAT, held_t, held_s, &sv);   /* a lit step changes in place: never needs room */
            dirty = true; edited_pattern(CURPAT);
            continue;
        }
        if (held_row >= 0) {
            if (k == 3) continue;                   /* knob 4 does nothing while a row is held */
            int t = held_row_t;
            held_row_turned = true;
            if (k == 2) {                           /* the CC this track's step locks send */
                int c = G.lock_cc[t] + v;
                G.lock_cc[t] = (uint8_t)(c < 0 ? 0 : c > 127 ? 127 : c);
                dirty = true; edited_global();
                continue;
            }
            if (k == 1 && is_mel(t)) { int n = m_cur[t - DTRACKS] + v; m_cur[t - DTRACKS] = (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n); dirty = true; continue; }
            if (k == 1) { int n = tnote(t) + v; G.note[t] = (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n); }
            else { int c = tchan(t) + v; c = c < 1 ? 1 : c > 16 ? 16 : c; if (is_mel(t)) G.m_chan[t - DTRACKS] = (uint8_t)c; else G.chan[t] = (uint8_t)c; }
            dirty = true; edited_global();
            continue;
        }
        int b = G.knob_bank & 3, f = kb_fn(b, k);   /* nothing held: the knob does what its bank says */
        if (f == KB_SWING) {
            int s = G.swing + v;
            G.swing = (uint8_t)(s < 50 ? 50 : s > 75 ? 75 : s);
            dirty = true; touched_global(); pop_kind = 2; pop_ms = ms_ticks;
            continue;
        }
        if (f == KB_TEMPO) {
            int t = (int)BPM + v;
            BPM = (uint16_t)(t < 40 ? 40 : t > 300 ? 300 : t);
            dirty = true; touched_global(); pop_kind = 1; pop_ms = ms_ticks;
            continue;
        }
        uint8_t ch = (uint8_t)(kb_chan(b, k) - 1);
        char *o;
        if (G.kb_abs[b][k]) {                       /* absolute: the knob moves a value 0-127 and sends it */
            int x = kb_val[b][k] + v;
            kb_val[b][k] = (uint8_t)(x < 0 ? 0 : x > 127 ? 127 : x);
            midi_send3((uint8_t)(0xB0 | ch), (uint8_t)f, kb_val[b][k]);
            o = cat(status_line, "CC"); o = cat_int(o, f); o = cat(o, "="); cat_int(o, kb_val[b][k]);
        } else {                                    /* relative: 64 +- steps (65 = one step clockwise) */
            if (v > 63) v = 63;
            if (v < -63) v = -63;
            midi_send3((uint8_t)(0xB0 | ch), (uint8_t)f, (uint8_t)(64 + v));
            o = cat(status_line, "CC"); o = cat_int(o, f);
            o = cat(o, v > 0 ? "+" : "-"); cat_int(o, v > 0 ? v : -v);
        }
        dirty = true;
    }
}

/* ---------- MIDI in ---------- */

static crash_info_t crash_boot;                     /* what the backup registers held at boot (the last crash, if any) */
static void crash_load(crash_info_t *ci)
{
    memset(ci, 0, sizeof *ci);
    if (!bk_valid()) return;
    ci->valid = 1; ci->cause = RTC->BKP1R & 0xFFFFu; ci->stage = RTC->BKP13R;
    ci->pc = RTC->BKP3R; ci->lr = RTC->BKP4R; ci->psr = RTC->BKP5R; ci->cfsr = RTC->BKP6R; ci->hfsr = RTC->BKP7R; ci->addr = RTC->BKP8R;
    ci->up_ms = RTC->BKP9R; ci->count = RTC->BKP10R; ci->total = RTC->BKP11R; ci->build = RTC->BKP14R;
    ci->sp = RTC->BKP15R; ci->stack_min = RTC->BKP16R;
}

static uint8_t sx[40];
static unsigned sx_len;
static bool sx_on;

/* Host protocol (the Pi -> the Fire). SysEx, the non-commercial id 7D, then "ORC" and a command:
     F0 7D 4F 52 43 <cmd> <data...> F7
     00                hello: the Fire answers F0 7D 4F 52 43 00 <version> F7 (so a host can find it)
     01 <16 x 0-127>   VU column levels (left to right). Bars jump up to a level and fall on their own, so a
                       host can send raw levels ~30 times a second. They only SHOW while host lights are on.
     02 <0|1>          host lights off / on (the VU bars take the pads; a touch on the Fire ends them, then the
                       host must ask again; they also end by themselves if levels stop for 3 s)
     03 <0-127>        light speed (64 = x1), saved like the menu setting
     04 <0-12>         show that effect (like Lights now); does not change the saved setting
     05 <text...>      scroll that message across the pads (printable ASCII, up to 32 characters, 3x4 font)
     06 [1]            the last crash on record (cause, PC, LR, stage ...: crash.h); 06 01 also clears it
   Anything else is ignored. */
#define HOST_VERSION 1
/* The host shows an effect / text of its own: it stays (CYCLE leaves it alone) until the Fire's user or the host changes it. */
static void host_pin(int cur)
{
    if (!lt_on) lights_on(true, false);
    lt_host = false; lt_manual = true; lt_cur = cur; lt_cycle_ms = ms_ticks;
    lt_by_host = lt_pinned = true;
}

#ifdef CRASH_TEST
static void __attribute__((noinline)) bench_smash(void)   /* overruns its own buffer: the canary must catch it */
{
    volatile char b[8];
    for (int i = 0; i < 40; i++) ((volatile char *)b)[i] = '0';
}
#endif

static void __attribute__((noinline)) host_cmd(const uint8_t *p, unsigned n)
{
#ifdef CRASH_TEST                                   /* BENCH BUILDS ONLY (never in the shipped image): 7E 00 = bus fault, 7E 01 = hang, 7E 02 = undefined instruction, 7E 03 = stack smash */
    if (n == 2 && p[0] == 0x7E) {
        if (p[1] == 0) { volatile uint32_t *bad = (volatile uint32_t *)0xFFFFFFF0u; (void)*bad; }
        if (p[1] == 1) { for (;;) {} }
        if (p[1] == 2) { __asm volatile("udf #0"); }
        if (p[1] == 3) bench_smash();
        if (p[1] == 5) {                            /* a CPU write to a watched word: DebugMon must record it */
            NVIC_DisableIRQ(TIM4_IRQn);             /* (TIM4 re-arms + disarms the same comparator) */
            volatile uint32_t x = 0;
            DWT->COMP0 = (uint32_t)&x; DWT->MASK0 = 2; DWT->FUNCTION0 = 6;
            x = 0x5A5A5A5Au;
            for (volatile int i = 0; i < 100; i++) {}
            DWT->FUNCTION0 = 0;
            NVIC_EnableIRQ(TIM4_IRQn);
        }
        if (p[1] == 4) {                            /* jump into RAM (two NOPs on the stack): the MPU must stop it at once */
            volatile uint16_t code[4] = {0xBF00, 0xBF00, 0x4770, 0xBF00};   /* nop, nop, bx lr */
            ((void (*)(void))((uint32_t)code | 1u))();
        }
        return;
    }
#endif
    ctl_host_t h = ctl_host_parse(p, n);
    switch (h.cmd) {
    case HC_HELLO:
        if (tud_midi_mounted()) {
            static const uint8_t R[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x00, HOST_VERSION, 0xF7};
            tud_midi_stream_write(0, R, sizeof R);
        }
        break;
    case HC_LEVELS:
        lights_host(h.data); lt_host_ms = ms_ticks;
        if (ctl_host_reengage(lt_on, lt_host, show_is_host())) { lt_host = true; lt_cur = LT_M_HOST; dirty = true; }   /* PI VU is back */
        break;
    case HC_LIGHTS:
        if (h.arg == 1) {
            lt_host_ms = ms_ticks;
            if (!lt_on) lights_on(true, true);
            else { lt_host = lt_manual = true; lt_cur = LT_M_HOST; }
            lt_by_host = true; lt_pinned = false;
        } else if (ctl_host_stop_ends(lt_on, lt_host, lt_by_host)) lights_off();
        break;
    case HC_SPEED:
        G.lt_speed = (uint8_t)h.arg; touched_global();
        break;
    case HC_EFFECT:
        host_pin(h.arg);
        break;
    case HC_TEXT:                                   /* a message: shown scrolling on the pads (up to 32 characters) */
        if (!h.len) break;                          /* an empty message would only blank the pads */
        lights_set_text((const char *)h.data, (int)h.len);
        host_pin(LT_M_TEXT);
        break;
    case HC_CRASH:                                  /* the last crash on record (06), and clear it (06 01) */
        if (tud_midi_mounted()) {
            uint8_t msg[CRASH_REPLY_LEN + 2];
            msg[0] = 0xF0;
            crash_reply(&crash_boot, msg + 1);
            msg[CRASH_REPLY_LEN + 1] = 0xF7;
            tud_midi_stream_write(0, msg, sizeof msg);
        }
        if (h.arg == 1) { RTC->BKP1R = 0; RTC->BKP11R = 0; memset(&crash_boot, 0, sizeof crash_boot); crash_snap.magic = 0; }
        break;
    case HC_PEEK:                                   /* read one word (09): RAM, flash, peripheral / core registers */
        if (tud_midi_mounted()) {
            uint8_t msg[18] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x09};
            memcpy(msg + 6, h.data, 5);             /* the address, as asked (so a reply can't be paired with the wrong ask) */
            crash_pack32(*(volatile const uint32_t *)(uint32_t)h.arg, msg + 11);
            msg[16] = 0; msg[17] = 0xF7;            /* (spare byte, 0) */
            tud_midi_stream_write(0, msg, sizeof msg);
        }
        break;
    case HC_COLORS:                                 /* 0A: pad colours for the surface (kept even while it is off) */
        for (unsigned i = 0; i < h.len; i++)
            for (int j = 0; j < 3; j++) surf_rgb[h.arg + (int)i][j] = h.data[3 * i + (unsigned)j];
        dirty = true;
        break;
    case HC_SURFACE:                                /* 0B: surface on (channel) / off */
        if (h.arg) {
            surf_ch = (uint8_t)(h.arg - 1); surf_known = true;
            if (!surface_on) { surface_on = true; lights_off(); song_edit = false; held_t = held_s = -1; held_pad = -1; }
        } else surface_on = false;
        dirty = true;
        break;
    case HC_STACK:                                  /* the stack around the last fault (08) */
        if (tud_midi_mounted()) {
            static uint8_t msg[CRASH_STACK_REPLY_LEN + 2];
            int ok = crash_boot.valid && crash_snap.magic == SNAP_MAGIC;
            msg[0] = 0xF0;
            crash_stack_reply(ok, crash_snap.base, crash_snap.w, msg + 1);
            msg[CRASH_STACK_REPLY_LEN + 1] = 0xF7;
            tud_midi_stream_write(0, msg, sizeof msg);
        }
        break;
    case HC_STATUS:                                 /* read only: what the pads / saver are doing (host tests, debugging) */
        if (tud_midi_mounted()) {
            uint32_t stack_depth = (uint32_t)&_estack - stack_low();
            if (stack_depth > 16383) stack_depth = 16383;
            const uint8_t R[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x07,
                lt_on, (uint8_t)(lt_on ? lt_cur & 127 : 127), lt_host, lt_by_host, lt_pinned,
                unsaved(), save_lazy_due != 0, saves_global,
                playing, ext_playing, (uint8_t)(G.lt_mode & 127), (uint8_t)(G.lt_speed & 127), menu_on,
                (uint8_t)(lt_frame & 127),          /* frames drawn (mod 128): moving = the lights really run */
                (uint8_t)(stack_depth >> 7 & 127), (uint8_t)(stack_depth & 127),   /* deepest stack use since boot, bytes */
                surface_on, surf_ch,                /* v0.19: the ORC surface and its channel (0-15) */
                0xF7};
            tud_midi_stream_write(0, R, sizeof R);
        }
        break;
    default: break;
    }
}

static void __attribute__((noinline)) midi_in_byte(uint8_t b, uint32_t now)
{
    if (b == 0xF8) {
        ext_clock = true; ext_last_ms = ms_ticks;
        if (ext_playing) {
            /* 12 ticks per pair of steps: the even step on tick 0, the odd (swung) one once its tick
               is reached. Once per pair, so turning swing mid-pair can't skip or double a step. */
            int pos = (int)(ext_ticks % 12);
            if (ext_tick_plays(pos, ext_late(), &ext_odd_due)) {
                if (pos == 0) { if (ext_even_at) ext_step_len = (now - ext_even_at) / 2; ext_even_at = now; }
                ext_step_at = now;
                play_step(gstep, now); gstep++;
            }
            ext_ticks++;
        }
        return;
    }
    if (b == 0xFA) {
        all_notes_off();                            /* a start without a stop first: clean slate */
        ext_clock = true; ext_last_ms = ms_ticks; playing = false; ext_playing = true; ext_ticks = 0; gstep = 0;
        ext_step_at = ext_even_at = ext_step_len = 0; ext_odd_due = false; rec_chord_t = -1;
        if (song_active()) song_goto(0);
        dirty = true; return;
    }
    if (b == 0xFB) { ext_playing = true; dirty = true; return; }
    if (b == 0xFC) { stop(false); return; }
    if (b == 0xF0) { sx_on = true; sx_len = 0; }
    if (sx_on) {
        if (sx_len < sizeof sx) sx[sx_len++] = b;
        if (b == 0xF7) {
            static const uint8_t DFU[] = {0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7};
            const uint8_t *hp; unsigned hn;
            if (sx_len == sizeof DFU && !memcmp(sx, DFU, sizeof DFU)) dfu_requested = true;
            else if (ctl_sysex_payload(sx, sx_len, sizeof sx, &hp, &hn))
                host_cmd(hp, hn);                   /* F0 7D 'O' 'R' 'C' <cmd + data> F7 (a message longer than the buffer is dropped) */
            sx_on = false;
        }
    }
}

static void __attribute__((noinline)) midi_task(uint32_t now)
{
    if (!tud_midi_mounted()) return;
    uint8_t pkt[4];
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    while (tud_midi_packet_read(pkt)) {
        uint8_t n = len[pkt[0] & 0xF];
        for (uint8_t i = 0; i < n; i++) midi_in_byte(pkt[1 + i], now);
    }
}

/* ---------- save / load ---------- */

static const int PAT_ID[PATTERNS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
static void pat_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { steps_record_read(*(const int *)ctx, off, dst, n); }

/* Erase the area and write the current state: the global record + one sparse record per pattern
   (old full-size records are dropped here). */
static bool compact_all(void)
{
    if (pat_stuck) return false;                    /* a pattern we couldn't load would be written empty: never */
    saving_kind = -1;
    G_snap = G;
    bool ok = store2_erase() && store2_append(S2_KIND_GLOBAL, &G_snap, sizeof G_snap);
    for (int p = 0; ok && p < PATTERNS; p++) {
        uint32_t sz = steps_pattern_size(p);
        bool blank = sz == 0;                       /* an empty pattern with default lengths needs no record */
        for (int t = 0; blank && t < TRACKS; t++) if (steps_len[p][t]) blank = false;
        if (!blank) ok = store2_append_src(KIND_PAT(p), steps_record_len(p), pat_reader, &PAT_ID[p]);
        if (ok) steps_saved[p] = (uint16_t)sz;
    }
    if (ok) { pat_dirty = 0; global_dirty = false; G_saved = G_snap; }
    return ok;
}

/* Background save: one record at a time, a few flash words per main-loop pass (see saver_poll). */

static void saver_start(void) { save_run = true; save_due = 0; save_forced_run = save_force; save_force = false; dirty = true; }

static void saver_poll(bool quiet_moment)
{
    if (store2_busy()) {
        if (quiet_moment) store2_step(8);           /* ~8 x 16 us of flash stall per pass */
        if (!store2_busy() && store2_last_failed()) {   /* write failed: mark it unsaved again, retry later */
            if (saving_kind == S2_KIND_GLOBAL) global_dirty = true; else if (saving_kind >= KIND_PAT(0)) pat_dirty |= (uint32_t)(1u << (saving_kind - KIND_PAT(0)));
            save_run = false; save_due = ms_ticks + 10000; if (!save_due) save_due = 1;
            if (save_forced_run) save_force = true;     /* a SAVE NOW that failed retries even with AUTOSAVE off */
        } else if (!store2_busy() && saving_kind == S2_KIND_GLOBAL) { G_saved = G_snap; saves_global = (saves_global + 1) & 127; }   /* what was written */
        else if (!store2_busy() && saving_kind >= KIND_PAT(0)) steps_saved[saving_kind - KIND_PAT(0)] = (uint16_t)saving_size;
        if (!store2_busy()) saving_kind = -1;
        return;
    }
    if (!save_run) return;
    if (global_dirty && !memcmp(&G, &G_saved, sizeof G)) global_dirty = false;   /* e.g. pattern 1 -> 2 -> 1: nothing new */
    int kind = -1, p = -1;
    if (global_dirty) kind = S2_KIND_GLOBAL;
    else for (p = 0; p < PATTERNS; p++) if ((pat_dirty & ~pat_stuck) >> p & 1) { kind = KIND_PAT(p); break; }
    if (kind < 0) { save_run = false; save_waiting_compact = false; dirty = true; return; }
    if (kind == S2_KIND_GLOBAL) G_snap = G;
    else saving_size = steps_pattern_size(p);
    bool begun = kind == S2_KIND_GLOBAL ? store2_begin(kind, &G_snap, sizeof G_snap)   /* from a snapshot */
                                        : store2_begin_src(kind, steps_record_len(p), pat_reader, &PAT_ID[p]);   /* live: edits cancel it */
    if (begun) {
        if (kind == S2_KIND_GLOBAL) global_dirty = false; else pat_dirty &= (uint32_t)~(1u << p);
        saving_kind = kind;
        return;
    }
    /* no room: erase + rewrite everything, but only while stopped (it stalls ~1-2 s) */
    if (!playing && !ext_playing) { if (!compact_all()) memcpy(status_line, "full", 5); save_run = false; save_waiting_compact = false; }
    else save_waiting_compact = true;
    dirty = true;
}

/* The starting fader bank: 16 volume faders (CC 7) on channels 1-16, rainbow colours, level 100. */
static void faders_default(void)
{
    G.fad_init = 1; G.fad_n = 16;
    for (int i = 0; i < MAXFADERS; i++) { G.fad_cc[i] = 7; G.fad_ch[i] = (uint8_t)(i % 16 + 1); G.fad_col[i] = (uint8_t)(i % 16); G.fad_val[i] = 100; }
}

static void set_defaults(void)
{
    memset(&G, 0, sizeof G);
    steps_clear_all();
    G.bpm = 120; G.swing = 50;
    faders_default();
    lights_defaults();
    for (int s = 0; s < 16; s += 4) steps_set(0, 0, s, LV_NORMAL, 0);
    steps_set(0, 1, 4, LV_NORMAL, 0); steps_set(0, 1, 12, LV_NORMAL, 0);
    for (int s = 2; s < 16; s += 4) steps_set(0, 2, s, LV_NORMAL, 0);
}

static bool area_blank(void)
{
    const uint32_t *w = (const uint32_t *)store_area;
    for (uint32_t i = 0; i < S2_AREA_SIZE / 4; i++) if (w[i] != 0xFFFFFFFFu) return false;
    return true;
}

static void load_state(void)
{
    set_defaults();
    store2_scan();
    if (store2_has_any()) {
        store2_read(S2_KIND_GLOBAL, &G, sizeof G);
        G_saved = G;                                /* as stored: if a clamp below changes G, the next save writes it */
        bool converted = false;
        for (int p = 0; p < PATTERNS; p++) {
            uint32_t len = 0;
            const uint8_t *r = store2_ptr(KIND_PAT(p), &len);
            if (r) {                                /* the current record (v0.11 on): never fall back past it */
                if (steps_record_load(p, r, len)) steps_saved[p] = (uint16_t)steps_pattern_size(p);
                else { pat_stuck |= (uint32_t)(1u << p); cat(status_line, "FULL"); }
                continue;
            }
            if (p >= 8) continue;                   /* older formats only ever had patterns 1-8 */
            r = store2_ptr(KIND_PAT_V1(p), &len);   /* v0.10's sparse record */
            if (r) {
                if (steps_record_load_v1(p, r, len)) { steps_saved[p] = (uint16_t)steps_pattern_size(p); pat_dirty |= (uint32_t)(1u << p); converted = true; }
                else { pat_stuck |= (uint32_t)(1u << p); cat(status_line, "FULL"); }
                continue;
            }
            r = store2_ptr(1 + p, &len);               /* else an old full-size one: convert it */
            if (!r) continue;
            if (!steps_convert_full(p, r, len)) { pat_stuck |= (uint32_t)(1u << p); cat(status_line, "FULL"); continue; }
            steps_saved[p] = (uint16_t)steps_pattern_size(p);
            pat_dirty |= (uint32_t)(1u << p);
            converted = true;
        }
        if (converted) { save_due = ms_ticks + 3000; save_force = true; }   /* write them in the new format soon (AUTOSAVE or not) */
        if (G.bpm < 40 || G.bpm > 300) G.bpm = 120;
        if (G.swing < 50 || G.swing > 75) G.swing = 50;
        G.cur_pat %= PATTERNS;
        if (!G.fad_init) faders_default();          /* a save from before v0.13 */
        if (!G.lt_init) lights_defaults();          /* a save from before the lights */
        if (!G.lt_idle_v18) {                       /* v0.18, once: the lights never start by themselves (the owner: not wanted) */
            G.lt_idle = 0; G.lt_idle_v18 = 1;
            global_dirty = true; save_due = ms_ticks + 3000; if (!save_due) save_due = 1; save_force = true;   /* keep it, AUTOSAVE or not */
        }
        G.lt_mode = (uint8_t)ctl_show_sanitize(G.lt_mode); G.lt_bars %= 5; lights_set_bar_style(G.lt_bars); G.lt_flip = 0; lights_set_heat_flip(0); G.lt_idle %= 6; G.lt_speed &= 127; G.lt_smooth &= 1; G.autosave_off &= 1;
        if (G.fad_n > MAXFADERS) G.fad_n = MAXFADERS;
        for (int i = 0; i < MAXFADERS; i++) {
            if (G.fad_ch[i] < 1 || G.fad_ch[i] > 16) G.fad_ch[i] = 1;
            G.fad_cc[i] &= 127; G.fad_col[i] &= 15; G.fad_val[i] &= 127;
        }
        return;
    }
    if (area_blank()) return;                       /* first run: defaults, nothing written yet */
    /* older firmware's saves (4 tracks x 16 steps): convert, then rewrite the area in the new format */
    store_data_t old;
    if (store_load(&old)) {
        steps_clear_all();
        G.bpm = (old.bpm >= 40 && old.bpm <= 300) ? old.bpm : 120;
        G.cur_pat = old.cur_pattern % PATTERNS;
        G.swing = (old.swing >= 50 && old.swing <= 75) ? old.swing : 50;
        G.clock_out = old.clock_out == 1;
        G.mute_bits = old.mute_bits & 0x0F;
        for (int t = 0; t < STORE_TRACKS; t++) {
            if (old.track_note[t]) G.note[t] = old.track_note[t] & 127;
            if (old.track_chan[t] >= 1 && old.track_chan[t] <= 16) G.chan[t] = old.track_chan[t];
        }
        for (int p = 0; p < STORE_PATTERNS; p++)
            for (int t = 0; t < STORE_TRACKS; t++)
                for (int s = 0; s < 16; s++) {
                    if (!(old.steps[p][t] >> s & 1)) continue;
                    steps_set(p, t, s, (old.accent[p][t] >> s & 1) ? LV_ACCENT : (old.soft[p][t] >> s & 1) ? LV_SOFT : LV_NORMAL, 0);
                }
        cat(status_line, "convrtd");
    }
    compact_all();                                  /* boot = stopped, so the erase is fine */
    for (int p = 0; p < PATTERNS; p++) steps_saved[p] = (uint16_t)steps_pattern_size(p);
}

int main(void)
{
    SCB->VTOR = 0x08008000u;
    bk_enable();
    uint32_t stage_before = RTC->BKP2R;             /* the stage the previous run was in (read before anything overwrites it) */
    bool wdog = (RCC->CSR & RCC_CSR_IWDGRSTF) != 0;
    RCC->CSR |= RCC_CSR_RMVF;
    if (wdog) {                                     /* a HANG: the watchdog reset us. Record it; we ARE the restarted app, so just run on. */
        bool dfu;
        crash_record(CRASH_WDOG, 0, 0, 0, 0, 0, 0, RTC->BKP12R, stage_before, &dfu);
        RTC->BKP15R = 0; RTC->BKP16R = stack_low();  /* the previous run's paint is still in RAM: its low point */
        crash_snap.magic = 0;                       /* a hang has no fault frame: an older snapshot must not pose as this one's */
        if (dfu) reboot_to_dfu();                   /* 3 early crashes in a row: update mode, so a bad image can be replaced */
    }
    RTC->BKP0R = 0u;
    mpu_ram_xn();                                   /* RAM is never code: a jump into it faults where it happens */
    dbgmon_init();                                  /* TIM4's canary watchpoint reports here */
    stack_paint();                                /* (after the watchdog record read the previous run's mark) */
    (void)*(volatile const char *)BUILD_TAG;        /* referenced, so --gc-sections keeps the tag in the image */
    crash_stage(ST_BOOT);
    IWDG->KR = 0x5555; IWDG->PR = 4; IWDG->RLR = 1000; IWDG->KR = 0xCCCC; IWDG->KR = 0xAAAA;

    clock_init();
    SysTick_Config(84000u);
    us_clock_init();
    usb_pins_init();
    NVIC_SetPriority(OTG_FS_IRQn, 2);
    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);
    oled_init();
    fire_hw_init();
    load_state();
    crash_load(&crash_boot);
    if (crash_boot.valid) cat(status_line, crash_boot.cause == CRASH_WDOG ? "WDOG RST" : "FAULT RST");   /* the header's status slot */

    uint32_t next_beat = 0;
    uint32_t next_knobs = 0, next_draw = 0, dfu_at = 0, next_anim = 0;
    for (;;) {
        IWDG->KR = 0xAAAA;
        uint32_t now = us_now();
        if ((int32_t)(ms_ticks - next_beat) >= 0) { next_beat = ms_ticks + 1000; RTC->BKP12R = ms_ticks; }   /* uptime, for a watchdog record */
        crash_stage(ST_USB);
        tud_task();
        midi_task(now);

        if (ext_clock && ms_ticks - ext_last_ms > 500) { ext_clock = false; if (ext_playing) stop(false); dirty = true; }

        crash_stage(ST_SEQ);
        if (playing) {
            /* step boundaries on the straight grid; swing delays only odd steps' notes;
               6 clock ticks per step anchored to each boundary (no drift) */
            if ((int32_t)(now - grid_us) >= 0) {
                if (pending >= 0) { play_step((uint32_t)pending, now); }
                step_start_us = grid_us;
                grid_us += step_us();
                if ((int32_t)(now - grid_us) > 0) { step_start_us = now; grid_us = now + step_us(); }
                pending = (int)gstep;
                gstep++;
                clock_k = 0;
            }
            if (pending >= 0 && (int32_t)(now - (step_start_us + ((pending & 1) ? swing_us() : 0))) >= 0) {
                play_step((uint32_t)pending, now);
                pending = -1;
            }
            if (G.clock_out && clock_k < 6 && (int32_t)(now - (step_start_us + (uint32_t)clock_k * step_us() / 6)) >= 0) {
                midi_send1(0xF8);
                clock_k++;
            }
        }
        crash_stage(ST_QUEUES);
        for (int i = 0; i < NOFFS; i++)
            if (offs[i].live && (int32_t)(now - offs[i].at_us) >= 0) { batch_add((uint8_t)(0x80 | offs[i].chan), offs[i].note, 0); offs[i].live = false; }
        for (int i = 0; i < NRATQ; i++)
            if (ratq[i].live && (int32_t)(now - ratq[i].at_us) >= 0) {
                seq_note_on(ratq[i].chan, ratq[i].note, ratq[i].vel);
                add_off(ratq[i].note, ratq[i].chan, now + ratq[i].gate_us);
                ratq[i].live = false;
            }
        batch_flush();

        crash_stage(ST_EVENTS);
        handle_events(now);

        crash_stage(ST_GATES);
        /* lights: the idle ones end when playing starts; with the sequencer stopped and untouched for the idle time, start */
        {
            bool run = playing || ext_playing;
            if (run) lt_input_ms = ms_ticks;        /* the idle time counts from when playing STOPS, not from the last touch */
            if (lt_on && !lt_manual && run) lights_off();
            else if (!lt_on) {
                bool live_held = false;
                for (int i = 0; i < 64 && !live_held; i++) live_held = live[i].on;
                ctl_idle_t in = {
                    .idle_s = LT_IDLE_S[G.lt_idle % 6], .lt_on = lt_on, .menu_on = menu_on,
                    .playing = playing, .ext_playing = ext_playing,
                    .held = held_t >= 0 || held_row >= 0 || held_pad >= 0 || pat_btn >= 0 || live_held,
                    .editing = song_edit || picker_on() || rec_arm || surface_on,
                    .modifier = fire_held(IN_SHIFT) || fire_held(IN_ALT),
                    .now_ms = ms_ticks, .last_input_ms = lt_input_ms,
                };
                if (ctl_idle_should_start(&in)) lights_on(false, false);
            }
        }

        if (ctl_lazy_due(save_lazy_due, ms_ticks) && !playing && !ext_playing) {   /* settings changed while stopped */
            save_lazy_due = 0;
            if ((global_dirty || (pat_dirty & ~pat_stuck)) && !save_due && !save_run) save_due = ms_ticks ? ms_ticks : 1;
        }
        if (save_due && (int32_t)(ms_ticks - save_due) >= 0 && G.autosave_off && !save_force)
            save_due = 0;                           /* AUTOSAVE off: the change stays marked (*) until SAVE NOW */
        if ((save_due && (int32_t)(ms_ticks - save_due) >= 0) || (save_waiting_compact && !playing && !ext_playing && !save_run))
            saver_start();
        {
            /* flash writes stall the CPU briefly (and a lights slice takes time): never right before a step or a swung note is due */
            bool quiet = true;
            if (playing) {
                uint32_t nxt = grid_us;
                if (pending >= 0) { uint32_t d2 = step_start_us + ((pending & 1) ? swing_us() : 0); if ((int32_t)(d2 - nxt) < 0) nxt = d2; }
                quiet = (int32_t)(nxt - now) > 1500;
            } else if (ext_playing && ext_step_len) {   /* external clock: the odd step comes at (swing: after) half a pair, the next even one a pair on */
                uint32_t nxt = ext_even_at + (ext_odd_due ? ext_step_len : 2 * ext_step_len);
                quiet = (int32_t)(nxt - now) > 1500;    /* (an odd step that is due but swung late: not quiet until it plays) */
            }
            crash_stage(ST_SAVER);
            saver_poll(quiet);
            crash_stage(ST_LIGHTS);
            bool lq = quiet;                        /* lights also keep clear of a ratchet or a note-off that is about to be due */
            for (int i = 0; lq && i < NOFFS; i++) if (offs[i].live && (int32_t)(offs[i].at_us - now) < 1000) lq = false;
            for (int i = 0; lq && i < NRATQ; i++) if (ratq[i].live && (int32_t)(ratq[i].at_us - now) < 1000) lq = false;
            lights_task(now, lq);
        }
        crash_stage(ST_KNOBS);
        if ((int32_t)(ms_ticks - next_knobs) >= 0) { next_knobs = ms_ticks + 5; fire_knobs_poll(); }

        if ((int32_t)(ms_ticks - next_anim) >= 0) { next_anim = ms_ticks + (mode == MODE_DRUM ? 40 : 150); dirty = true; }   /* pulses + blinks */
        crash_stage(ST_PAINT);
        if (dirty) paint();
        crash_stage(ST_RENDER);
        oled_busy();
        if (dirty && (int32_t)(ms_ticks - next_draw) >= 0) {
            render();
            if (oled_flush()) { dirty = false; next_draw = ms_ticks + 33; }
        }

        crash_stage(ST_TAIL);
        if (dfu_requested && !dfu_at) dfu_at = ms_ticks + 50;
        if (dfu_at && (int32_t)(ms_ticks - dfu_at) >= 0) reboot_to_dfu();
    }
}
